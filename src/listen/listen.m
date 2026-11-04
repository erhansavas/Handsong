// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "listen/listen.h"
#import "link/link.h"
#import "listen/capture.h"
#import "listen/output_device.h"
#import "listen/resampler.h"
#import "listen/speaker_chain.h"
#import <Foundation/Foundation.h>
#import "link/realtime.h"

static const double kHandsLateMs = 110;      // the actuators answer this long after a block is sent. Measured, and fixed
static const double kVideoDelayMs = 40;      // under -125 ms a late sound is not noticed; the hands trail a little instead
static const double kIdleStopMs = 120000;    // silence before the stream ends, so the controller may sleep
static const double kNoSoundWarningMs = 8000;
static const double kTrustMs = 20000;

static _Atomic bool g_feel, g_speaker, g_aside, g_video, g_quit, g_started, g_restart;
static _Atomic int g_state;
static _Atomic float g_left, g_right, g_hit, g_speech;
static _Atomic bool g_no_sound, g_heard;
static _Atomic double g_hold_off_until;
static hs_capture *_Atomic g_capture;
static pthread_t g_thread;
static dispatch_queue_t g_control;

// Everything below runs on the listen thread, except where noted.
static hs_feel g_feel_state;
static hs_speaker_chain g_chain;
static hs_resampler g_resampler;
static double g_resampler_rate;

// Fills a block at 45 kHz from whatever rate the Mac runs at. The capture's clock is followed by nudging the ratio, so
// the distance to the newest sound stays small without any jump.
static void fill_block(hs_capture *capture, float *block) {
  static const double kTargetMs = 30, kLimitMs = 160;
  double rate = hs_capture_rate(capture);
  if (rate != g_resampler_rate) {
    hs_resampler_init(&g_resampler, rate, HS_FEEL_RATE);
    g_resampler_rate = rate;
  }
  size_t available = hs_capture_available(capture);
  double available_ms = (double)available * 1000 / rate;
  if (available_ms > kLimitMs) hs_capture_skip_to_newest(capture, (size_t)(kTargetMs * rate / 1000));
  double nominal = rate / HS_FEEL_RATE;
  g_resampler.step = nominal * (1 + fmax(-0.002, fmin(0.002, (available_ms - kTargetMs) * 0.00005)));
  for (int i = 0; i < HS_FEEL_FRAMES; i++) {
    hs_resampler_value(&g_resampler, &block[2 * i], &block[2 * i + 1]);
    for (int need = hs_resampler_advance(&g_resampler); need > 0; need--) {
      float frame[2] = {0, 0};
      hs_capture_read(capture, frame, 1);
      hs_resampler_push(&g_resampler, frame[0], frame[1]);
    }
  }
}

static double wanted_delay_ms(void) {
  if (hs_output_is_bluetooth() || hs_output_is_selected()) return 0;
  if (atomic_load(&g_video)) return kVideoDelayMs;
  return fmax(0, kHandsLateMs - hs_output_latency_ms());
}

// On the control queue, because these calls can take a while and must never hold up the listen thread.
static void set_capture_for(bool feel, bool speaker_ready) {
  static bool running, running_muted;
  static double running_delay;
  bool want = (feel || speaker_ready) && !atomic_load(&g_aside);
  // The Mac's own sound is touched only once this tap has really heard it. Without the system's permission a tap hears nothing, and taking
  // the Mac's sound away then would leave no sound anywhere.
  bool heard = atomic_load(&g_heard);
  bool muted = speaker_ready && heard;
  double delay = speaker_ready || !feel || !heard ? 0 : wanted_delay_ms();
  bool restart = atomic_exchange(&g_restart, false);
  if (running && (restart || !want || muted != running_muted || (delay == 0) != (running_delay == 0))) {
    hs_capture *old = atomic_exchange(&g_capture, NULL);
    usleep(30000);    // the listen thread finishes its block before the capture goes
    hs_capture_stop(old);
    running = false;
  }
  if (want && !running) {
    atomic_store(&g_capture, hs_capture_start(muted, delay));
    running = atomic_load(&g_capture) != NULL;
    running_muted = muted;
    running_delay = delay;
  } else if (running && delay != running_delay) {
    hs_capture_set_delay(atomic_load(&g_capture), delay);
    running_delay = delay;
  }
}

static void *listen_thread(void *argument) {
  (void)argument;
  pthread_setname_np("listen");
  hs_thread_promise(HS_STREAM_PERIOD_MS, 3);
  hs_feel_init(&g_feel_state);
  double next = hs_now_ms(), started = 0, last_control = 0, last_signal_seen = 0;
  static float block[HS_FEEL_FRAMES * 2];
  int8_t hands[HS_WAVE_BYTES];
  uint8_t packets[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES];
  bool stepped = false, speaking = false;
  while (!atomic_load(&g_quit)) {
    double now = hs_now_ms();
    if (next > now) { usleep((useconds_t)((next - now) * 1000)); continue; }
    next = hs_next_tick(next, now);

    bool feel = atomic_load(&g_feel), speaker = atomic_load(&g_speaker), aside = atomic_load(&g_aside);
    bool wide = hs_link_wide(), connected = hs_link_state() == HS_CONTROLLER_READY;
    bool speaker_ready = speaker && wide && connected;
    if (now - last_control > 250) {
      last_control = now;
      bool feel_now = feel, ready_now = speaker_ready;
      dispatch_async(g_control, ^{ set_capture_for(feel_now, ready_now); });
    }
    if (aside) {
      if (!stepped) { hs_speaker_set_open(&g_chain, false); stepped = true; speaking = false; }
      atomic_store(&g_state, HS_LISTEN_STEPPED_ASIDE);
      continue;
    }
    stepped = false;
    if (hs_now_ms() < atomic_load(&g_hold_off_until)) continue;
    hs_capture *capture = atomic_load(&g_capture);
    if (!capture || !connected || (!feel && !speaker_ready)) {
      if (hs_link_mode() != HS_MODE_YIELD && hs_link_mode() != HS_MODE_IDLE) hs_link_set_mode(HS_MODE_IDLE);
      atomic_store(&g_state, speaker && !wide ? HS_LISTEN_SPEAKER_NEEDS_ROOM : (feel || speaker) ? HS_LISTEN_WAITING : HS_LISTEN_OFF);
      atomic_store(&g_left, 0.0f);
      atomic_store(&g_right, 0.0f);
      speaking = false;
      started = 0;
      continue;
    }
    if (started == 0) started = now;
    fill_block(capture, block);
    double signal = hs_capture_last_signal_ms(capture);
    if (signal > 0) {
      last_signal_seen = signal;
      if (!atomic_exchange(&g_heard, true)) NSLog(@"listening: the Mac's sound is reaching the app");
    }
    atomic_store(&g_no_sound, last_signal_seen == 0 && now - started > kNoSoundWarningMs);
    // Long silence may be a quiet Mac, or a permission taken away. Either way the Mac gets its own sound back until something is heard again.
    if (last_signal_seen > 0 && now - last_signal_seen > kTrustMs && atomic_exchange(&g_heard, false)) last_signal_seen = 0;
    bool playing = last_signal_seen > 0 && now - last_signal_seen < kIdleStopMs;
    if (!playing) {
      hs_link_set_mode(HS_MODE_IDLE);
      atomic_store(&g_state, HS_LISTEN_WAITING);
      continue;
    }
    if (speaker_ready) {
      if (!speaking) { hs_speaker_set_open(&g_chain, true); speaking = true; }
      hs_link_set_mode(HS_MODE_SPEAKER);
      // A Mac's music arrives close to full scale. On a driver this small that only ever sounds pushed, so it starts well below the ceiling.
      static float trimmed[HS_FEEL_FRAMES * 2];
      for (int i = 0; i < HS_FEEL_FRAMES * 2; i++) trimmed[i] = block[i] * 0.4f;
      if (hs_speaker_process(&g_chain, trimmed, packets)) hs_link_push_speaker((const uint8_t (*)[HS_SPEAKER_PACKET_BYTES])packets);
      hs_feel_process(&g_feel_state, block, hands);
      atomic_store(&g_state, HS_LISTEN_SPEAKER);
    } else {
      if (speaking) { hs_speaker_set_open(&g_chain, false); speaking = false; }
      hs_feel_process(&g_feel_state, block, hands);
      if (wide) {
        hs_link_set_mode(HS_MODE_WAVEFORM);
        hs_link_push_waveform(hands);
        atomic_store(&g_state, HS_LISTEN_FEELING);
      } else {
        uint8_t big, small;
        hs_feel_rumble_bytes(&g_feel_state, &big, &small);
        hs_link_set_mode(HS_MODE_RUMBLE);
        hs_link_set_rumble(big, small);
        atomic_store(&g_state, HS_LISTEN_FEELING_SOFT);
      }
    }
    atomic_store(&g_left, g_feel_state.view.left);
    atomic_store(&g_right, g_feel_state.view.right);
    atomic_store(&g_hit, g_feel_state.view.hit);
    atomic_store(&g_speech, g_feel_state.view.speech);
  }
  return NULL;
}

void hs_listen_start(void) {
  if (atomic_exchange(&g_started, true)) return;
  g_control = dispatch_queue_create("listen control", DISPATCH_QUEUE_SERIAL);
  hs_speaker_init(&g_chain);
  uint8_t silence[HS_SPEAKER_PACKET_BYTES];
  if (hs_speaker_silence_packet(silence)) hs_link_set_speaker_silence(silence);
  atomic_store(&g_quit, false);
  pthread_create(&g_thread, NULL, listen_thread, NULL);
}

void hs_listen_stop(void) {
  if (!atomic_exchange(&g_started, false)) return;
  atomic_store(&g_quit, true);
  pthread_join(g_thread, NULL);
  dispatch_sync(g_control, ^{
    hs_capture *old = atomic_exchange(&g_capture, NULL);
    hs_capture_stop(old);
  });
  hs_speaker_destroy(&g_chain);
  hs_link_set_mode(HS_MODE_IDLE);
}

void hs_listen_set_feel(bool on) { atomic_store(&g_feel, on); }
void hs_listen_set_speaker(bool on) { atomic_store(&g_speaker, on); }
void hs_listen_step_aside(bool aside) { atomic_store(&g_aside, aside); }
void hs_listen_set_video_in_front(bool video) { atomic_store(&g_video, video); }
void hs_listen_output_changed(void) { atomic_store(&g_restart, true); }

void hs_listen_hold_off(double ms) { atomic_store(&g_hold_off_until, hs_now_ms() + ms); }

hs_listen_status hs_listen_get_status(void) {
  return (hs_listen_status){(hs_listen_state)atomic_load(&g_state), atomic_load(&g_left), atomic_load(&g_right), atomic_load(&g_hit),
                            atomic_load(&g_speech), atomic_load(&g_speaker), atomic_load(&g_no_sound)};
}
