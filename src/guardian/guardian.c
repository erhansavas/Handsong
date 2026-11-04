// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Stays silent while the app lives. If the app dies without saying goodbye, this lets the triggers go, turns the controller's
// speaker off and returns the Mac's sound output to what it was, because a killed process can do none of that itself.
// The app holds the other end of standard input, and writes 'q' only when it leaves cleanly.
#include "link/protocol.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void release_controller(void) {
  IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  int vendor = HS_VENDOR, product = HS_PRODUCT_STANDARD;
  CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vendor), p = CFNumberCreate(NULL, kCFNumberIntType, &product);
  const void *keys[2] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)}, *values[2] = {v, p};
  CFDictionaryRef match = CFDictionaryCreate(NULL, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  IOHIDManagerSetDeviceMatching(manager, match);
  IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
  CFSetRef devices = IOHIDManagerCopyDevices(manager);
  if (devices) {
    CFIndex count = CFSetGetCount(devices);
    const void **list = calloc((size_t)count, sizeof *list);
    CFSetGetValues(devices, list);
    for (CFIndex i = 0; i < count; i++) {
      IOHIDDeviceRef device = (IOHIDDeviceRef)list[i];
      if (IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone) != kIOReturnSuccess) continue;
      uint8_t report[HS_CONTROL_LEN];
      hs_control off = {.set_right = true, .set_left = true, .right = hs_trigger_off(), .left = hs_trigger_off(), .route = true};
      hs_build_control(report, &off, 0);
      IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, HS_REPORT_CONTROL, report, HS_CONTROL_LEN);
      IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    }
    free(list);
    CFRelease(devices);
  }
  CFRelease(match);
  CFRelease(v);
  CFRelease(p);
  CFRelease(manager);
}

// The app writes the sound output to return to as the guardian's first argument's file, while its own output is selected.
static void restore_sound_output(const char *path) {
  if (!path) return;
  FILE *f = fopen(path, "r");
  if (!f) return;
  char uid[256] = "";
  bool read = fgets(uid, sizeof uid, f) != NULL;
  fclose(f);
  unlink(path);
  if (!read) return;
  uid[strcspn(uid, "\n")] = 0;
  CFStringRef name = CFStringCreateWithCString(NULL, uid, kCFStringEncodingUTF8);
  AudioObjectPropertyAddress translate = {kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof device;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &translate, sizeof name, &name, &size, &device) == noErr && device != kAudioObjectUnknown) {
    AudioObjectPropertyAddress output = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectSetPropertyData(kAudioObjectSystemObject, &output, 0, NULL, sizeof device, &device);
  }
  CFRelease(name);
}

int main(int argc, char **argv) {
  char byte;
  while (read(STDIN_FILENO, &byte, 1) > 0) {
    if (byte == 'q') return 0;
  }
  release_controller();
  restore_sound_output(argc > 1 ? argv[1] : NULL);
  return 0;
}
