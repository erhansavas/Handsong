// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "link/link.h"
#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDManager.h>
#import "link/realtime.h"
#include <time.h>

static hs_link_core g_core;
static pthread_t g_thread;
static _Atomic bool g_quit, g_started, g_simulated;
static _Atomic int g_state, g_in_flight;
static IOHIDDeviceRef g_device;          // link thread only
static IOHIDManagerRef g_manager;        // link thread only
static uint8_t g_report_buffer[256];

// The newest input, written by the link thread and read by anyone: a reader retries if a write crossed it.
static _Atomic unsigned g_input_version;
static hs_input g_input;
static _Atomic double g_input_rate;
static _Atomic double g_last_input_ms;
static double g_rate_window_start;
static long g_rate_window_count;

static _Atomic(hs_input_handler) g_handler;
static void *_Atomic g_handler_context;
static hs_imu_calibration g_calibration;
static _Atomic unsigned g_calibration_version;

double hs_now_ms(void) { return (double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e6; }

static void publish_input(const hs_input *input, double now) {
  atomic_fetch_add_explicit(&g_input_version, 1, memory_order_acq_rel);
  g_input = *input;
  atomic_fetch_add_explicit(&g_input_version, 1, memory_order_acq_rel);
  atomic_store(&g_last_input_ms, now);
  hs_input_handler handler = atomic_load(&g_handler);
  if (handler) handler(input, now, atomic_load(&g_handler_context));
}

void hs_link_snapshot(hs_input *out) {
  unsigned before, after;
  do {
    before = atomic_load_explicit(&g_input_version, memory_order_acquire);
    *out = g_input;
    after = atomic_load_explicit(&g_input_version, memory_order_acquire);
  } while (before != after || (before & 1));
}

static void note_rate(double now) {
  if (g_rate_window_start == 0) g_rate_window_start = now;
  g_rate_window_count++;
  double elapsed = now - g_rate_window_start;
  if (elapsed < 500) return;
  double rate = g_rate_window_count * 1000.0 / elapsed;
  atomic_store(&g_input_rate, rate);
  atomic_store(&g_core.link_wide, rate > 100);   // measured: 65 a second with a narrow link, 132 with a wide one
  g_rate_window_start = now;
  g_rate_window_count = 0;
}

static void on_input_report(void *context, IOReturn result, void *sender, IOHIDReportType type, uint32_t report_id,
                            uint8_t *report, CFIndex length) {
  (void)context; (void)result; (void)sender; (void)type;
  if (report_id != HS_REPORT_INPUT) return;
  hs_input input;
  if (!hs_parse_input(report, (size_t)length, &input)) return;
  double now = hs_now_ms();
  note_rate(now);
  publish_input(&input, now);
}

static void one_report_left(void *context, IOReturn outcome, void *sender, IOHIDReportType kind, uint32_t id, uint8_t *bytes, CFIndex size) {
  (void)context; (void)outcome; (void)sender; (void)kind; (void)id; (void)bytes; (void)size;
  atomic_fetch_sub(&g_in_flight, 1);
}

// How the state machine reaches the device. With no device a simulated link says yes and goes nowhere; a real one counts what it has queued.
static bool hand_to_system(void *unused, uint8_t id, const uint8_t *bytes, size_t size) {
  (void)unused;
  if (g_device == NULL) return atomic_load(&g_simulated);
  atomic_fetch_add(&g_in_flight, 1);
  bool taken = IOHIDDeviceSetReportWithCallback(g_device, kIOHIDReportTypeOutput, id, bytes, (CFIndex)size, 1.0, one_report_left, NULL) == kIOReturnSuccess;
  if (!taken) atomic_fetch_sub(&g_in_flight, 1);
  return taken;
}

static void read_calibration(IOHIDDeviceRef device) {
  uint8_t report[HS_CALIBRATION_LEN] = {HS_CALIBRATION_REPORT};
  CFIndex size = sizeof report;
  hs_imu_calibration calibration = hs_imu_nominal();
  if (IOHIDDeviceGetReport(device, kIOHIDReportTypeFeature, HS_CALIBRATION_REPORT, report, &size) == kIOReturnSuccess)
    calibration = hs_parse_calibration(report, (size_t)size);
  atomic_fetch_add(&g_calibration_version, 1);
  g_calibration = calibration;
  atomic_fetch_add(&g_calibration_version, 1);
}

hs_imu_calibration hs_link_calibration(void) {
  hs_imu_calibration out;
  unsigned before, after;
  do {
    before = atomic_load(&g_calibration_version);
    out = g_calibration;
    after = atomic_load(&g_calibration_version);
  } while (before != after || (before & 1));
  return out;
}

static void forget_device(void) {
  if (!g_device) return;
  IOHIDDeviceRegisterInputReportCallback(g_device, g_report_buffer, sizeof g_report_buffer, NULL, NULL);
  IOHIDDeviceUnscheduleFromRunLoop(g_device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  IOHIDDeviceClose(g_device, kIOHIDOptionsTypeNone);
  CFRelease(g_device);
  g_device = NULL;
  hs_core_disconnected(&g_core);
  atomic_store(&g_in_flight, 0);
  atomic_store(&g_input_rate, 0.0);
  atomic_store(&g_core.link_wide, false);
  g_rate_window_start = 0;
  g_rate_window_count = 0;
  atomic_store(&g_state, HS_CONTROLLER_NONE);
}

static void on_device_removed(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
  (void)context; (void)result; (void)sender;
  if (device == g_device) forget_device();
  else if (!g_device && atomic_load(&g_state) == HS_CONTROLLER_ON_CABLE) atomic_store(&g_state, HS_CONTROLLER_NONE);
}

static void on_device_found(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
  (void)context; (void)result; (void)sender;
  if (g_device) return;      // the first controller stays; another is noted by the app, never written to
  CFTypeRef transport = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDTransportKey));
  bool bluetooth = transport && CFEqual(transport, CFSTR("Bluetooth"));
  CFNumberRef productRef = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
  int product = 0;
  if (productRef) CFNumberGetValue(productRef, kCFNumberIntType, &product);
  if (!bluetooth) {
    atomic_store(&g_state, HS_CONTROLLER_ON_CABLE);
    return;
  }
  IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  if (IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
    IOHIDDeviceUnscheduleFromRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    return;
  }
  g_device = device;
  CFRetain(g_device);
  atomic_store(&g_in_flight, 0);
  read_calibration(device);
  IOHIDDeviceRegisterInputReportCallback(device, g_report_buffer, sizeof g_report_buffer, on_input_report, NULL);
  hs_core_connected(&g_core, hs_now_ms());
  atomic_store(&g_state, product == HS_PRODUCT_EDGE ? HS_CONTROLLER_UNTESTED : HS_CONTROLLER_READY);
}

static void *link_thread(void *argument) {
  (void)argument;
  pthread_setname_np("link");
  @autoreleasepool {
    if (!atomic_load(&g_simulated)) {
      g_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
      NSArray *matches = @[@{@kIOHIDVendorIDKey: @(HS_VENDOR), @kIOHIDProductIDKey: @(HS_PRODUCT_STANDARD)},
                           @{@kIOHIDVendorIDKey: @(HS_VENDOR), @kIOHIDProductIDKey: @(HS_PRODUCT_EDGE)}];
      IOHIDManagerSetDeviceMatchingMultiple(g_manager, (__bridge CFArrayRef)matches);
      IOHIDManagerRegisterDeviceMatchingCallback(g_manager, on_device_found, NULL);
      IOHIDManagerRegisterDeviceRemovalCallback(g_manager, on_device_removed, NULL);
      IOHIDManagerScheduleWithRunLoop(g_manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
      IOHIDManagerOpen(g_manager, kIOHIDOptionsTypeNone);
    } else {
      hs_core_connected(&g_core, hs_now_ms());
      atomic_store(&g_state, HS_CONTROLLER_READY);
    }
    hs_thread_promise(HS_STREAM_PERIOD_MS, 2);
    double next = hs_now_ms();
    while (!atomic_load(&g_quit)) {
      double remaining = next - hs_now_ms();
      if (remaining > 0) CFRunLoopRunInMode(kCFRunLoopDefaultMode, remaining / 1000.0, false);
      double now = hs_now_ms();
      if (now < next) continue;
      hs_core_tick(&g_core, now, atomic_load(&g_in_flight), hand_to_system, NULL);
      next = hs_next_tick(next, now);
    }
    hs_core_release(&g_core, hand_to_system, NULL);
    // The last reports (no force, speaker off) must really leave before the device goes, but never hold up quitting for long.
    for (int wait = 0; wait < 30 && atomic_load(&g_in_flight) > 0; wait++) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
    forget_device();
    if (g_manager) {
      IOHIDManagerUnscheduleFromRunLoop(g_manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
      IOHIDManagerClose(g_manager, kIOHIDOptionsTypeNone);
      CFRelease(g_manager);
      g_manager = NULL;
    }
  }
  return NULL;
}

bool hs_link_triggers_are_off(void) {
  hs_trigger_effect off = hs_trigger_off();
  return !memcmp(&g_core.trigger_right, &off, sizeof off) && !memcmp(&g_core.trigger_left, &off, sizeof off);
}

void hs_link_simulate(void) { atomic_store(&g_simulated, true); }

void hs_link_start(void) {
  if (atomic_exchange(&g_started, true)) return;
  hs_core_init(&g_core);
  g_calibration = hs_imu_nominal();
  g_input.left_x = g_input.left_y = g_input.right_x = g_input.right_y = 128;
  atomic_store(&g_quit, false);
  pthread_create(&g_thread, NULL, link_thread, NULL);
}

void hs_link_stop(void) {
  if (!atomic_exchange(&g_started, false)) return;
  atomic_store(&g_quit, true);
  pthread_join(g_thread, NULL);
}

hs_controller_state hs_link_state(void) { return (hs_controller_state)atomic_load(&g_state); }
double hs_link_input_rate(void) { return atomic_load(&g_input_rate); }
bool hs_link_wide(void) { return atomic_load(&g_core.link_wide); }

void hs_link_set_input_handler(hs_input_handler handler, void *context) {
  atomic_store(&g_handler, NULL);
  atomic_store(&g_handler_context, context);
  atomic_store(&g_handler, handler);
}

void hs_link_set_mode(hs_mode mode) { atomic_store(&g_core.wanted_mode, (int)mode); }
hs_mode hs_link_mode(void) { return (hs_mode)atomic_load(&g_core.wanted_mode); }
void hs_link_set_triggers(const hs_trigger_effect *right, const hs_trigger_effect *left) { hs_core_set_triggers(&g_core, right, left); }
void hs_link_set_rumble(uint8_t big, uint8_t small) { atomic_store(&g_core.rumble, (uint32_t)big << 8 | small); }

void hs_link_set_lights_show(bool on, uint8_t red, uint8_t green, uint8_t blue, uint8_t player_lights, uint8_t mic_light) {
  uint64_t packed = on ? (uint64_t)red | (uint64_t)green << 8 | (uint64_t)blue << 16 | (uint64_t)player_lights << 24 | (uint64_t)mic_light << 32 | 1ull << 40 : 0;
  atomic_store(&g_core.lights_show, packed);
}

void hs_link_set_lights_quiet(bool quiet, bool light_bar_too) {
  atomic_store(&g_core.lights_quiet, quiet);
  atomic_store(&g_core.light_bar_quiet, light_bar_too);
}

void hs_link_set_speaker_volume(int volume) { atomic_store(&g_core.speaker_volume, volume); }

void hs_link_set_speaker_silence(const uint8_t packet[HS_SPEAKER_PACKET_BYTES]) {
  memcpy(g_core.speaker_silence, packet, HS_SPEAKER_PACKET_BYTES);
  atomic_store(&g_core.silence_ready, true);
}

bool hs_link_push_waveform(const int8_t samples[HS_WAVE_BYTES]) { return hs_core_push_waveform(&g_core, samples); }
bool hs_link_push_speaker(const uint8_t opus[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES]) { return hs_core_push_speaker(&g_core, opus); }
hs_link_stats hs_link_get_stats(void) { return g_core.stats; }

