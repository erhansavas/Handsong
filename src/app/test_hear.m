// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "app/app_delegate.h"
#import "app/test_modes.h"
#import "experience/director.h"
#import "experience/experience.h"
#import "sound/sound.h"
#import <AppKit/AppKit.h>
#import "link/link.h"
#import "listen/capture.h"
#import "experience/voice_out.h"
#import "experience/stage.h"
#include "experience/mic_gate.h"
#include <math.h>
#include <stdio.h>
#include <unistd.h>

// ---------------------------------------------------------------------------- Does a tap hear the Mac, the way the app opens one?
int hs_test_hear(const char *out_path) {
  FILE *out = fopen(out_path, "w");
  if (!out) return 1;
  hs_capture *capture = hs_capture_start(false, 0);
  fprintf(out, "tap %s\n", capture ? "opened" : "refused");
  fflush(out);
  if (!capture) { fclose(out); return 1; }
  double start = hs_now_ms();
  while (hs_now_ms() - start < 9000 && hs_capture_last_signal_ms(capture) == 0) usleep(50000);
  fprintf(out, "%s after %.1f s, rate %.0f, frames %zu\n", hs_capture_last_signal_ms(capture) > 0 ? "heard sound" : "heard nothing", (hs_now_ms() - start) / 1000, hs_capture_rate(capture), hs_capture_available(capture));
  fclose(out);
  hs_capture_stop(capture);
  return 0;
}

// ---------------------------------------------------------------------------- The microphone's judgement, on a real room's trace and on the Mac's own music
static float to_db(float power) { return 10 * log10f(power); }
static float from_db(float db) { return powf(10, db / 10); }

int hs_test_mic_gate(void) {
  int failures = 0;
#define MIC_CHECK(name, ok) do { bool passed = (ok); printf("%s microphone: %s\n", passed ? "pass" : "FAIL", name); if (!passed) failures++; } while (0)
  static float room[2048];
  int frames = 0;
  FILE *f = fopen("test/fixtures/mic_room.txt", "r");
  if (f) { while (frames < 2048 && fscanf(f, "%f", &room[frames]) == 1) frames++; fclose(f); }
  MIC_CHECK("the room's trace is here", frames > 200);
  if (frames == 0) return failures;
  // 1. A real room, nobody doing anything, nothing playing.
  hs_mic_gate gate = {0};
  float most = 0;
  int moved = 0;
  for (int i = 0; i < frames; i++) { float v = hs_mic_gate_step(&gate, room[i], -120); most = fmaxf(most, v); moved += v > 0; }
  printf("real room: moved %d of %d buffers, most %.2f\n", moved, frames, most);
  MIC_CHECK("a real room moves almost nothing", moved * 20 <= frames && most < 0.15f);
  // 2. The same room with the Mac's music reaching the microphone: slow swells and single hits, 25 dB down on what is playing.
  hs_mic_gate with_music = {0};
  most = 0;
  moved = 0;
  float playing[2048];
  for (int i = 0; i < frames * 2 && i < 2048; i++) {
    float t = (float)i * 0.043f;
    float out = -32 + 10 * sinf(t * 0.7f) + (fmodf(t, 3.1f) < 0.1f ? 14.0f : 0.0f);
    playing[i] = out;
    float mic = to_db(from_db(room[i % frames]) + from_db(out - 25));
    float v = hs_mic_gate_step(&with_music, mic, out);
    if (i > 80) { most = fmaxf(most, v); moved += v > 0; }      // once it has heard the music come and go
  }
  printf("with the music playing: moved %d buffers, most %.2f\n", moved, most);
  MIC_CHECK("the Mac's own music is not a breath", most < 0.2f);
  // 3. A hum and a breath, in the quiet and over the music, are.
  for (int music = 0; music < 2; music++) {
    hs_mic_gate g = {0};
    for (int i = 0; i < 300; i++) hs_mic_gate_step(&g, music ? to_db(from_db(room[i % frames]) + from_db(playing[i] - 25)) : room[i % frames], music ? playing[i] : -120);
    float hum = 0, breath = 0, after = 1;
    for (int i = 0; i < 15; i++) hum = hs_mic_gate_step(&g, -30, music ? -32 : -120);
    for (int i = 0; i < 15; i++) breath = hs_mic_gate_step(&g, -18, music ? -32 : -120);
    for (int i = 0; i < 25; i++) after = hs_mic_gate_step(&g, room[i], music ? -32 : -120);
    printf("%s: a hum %.2f, a breath %.2f, after %.2f\n", music ? "over the music" : "in the quiet", hum, breath, after);
    MIC_CHECK("a hum moves it", hum > 0.2f);
    MIC_CHECK("a breath moves it strongly", breath > 0.6f);
    MIC_CHECK("and it settles again", after < 0.1f);
  }
  return failures;
#undef MIC_CHECK
}

// ---------------------------------------------------------------------------- What the real microphone gives in this room, opened as the chapter opens it
int hs_test_mic_level(double seconds) {
  if (hs_mic_get_state() == HS_MIC_NOT_ASKED) hs_mic_request();
  double wait = hs_now_ms();
  while (hs_mic_get_state() != HS_MIC_GRANTED && hs_now_ms() - wait < 20000) {
    if (hs_mic_get_state() == HS_MIC_DENIED) { printf("microphone refused\n"); return 1; }
    usleep(100000);
  }
  hs_mic_set_listening(true);
  double start = hs_now_ms(), most = 0;
  while (hs_now_ms() - start < seconds * 1000) {
    float level, pitch;
    hs_mic_read(&level, &pitch);
    if (level > most) most = level;
    printf("%.2f s level %.3f db %.1f\n", (hs_now_ms() - start) / 1000, level, hs_mic_decibels());
    fflush(stdout);
    usleep(43000);
  }
  printf("most %.3f\n", most);
  hs_mic_stop();
  usleep(300000);
  return 0;
}

// ---------------------------------------------------------------------------- The real Breath chapter in this room, with its music coming out of the Mac: does silence stay silent?
int hs_test_breath_real(double seconds) {
  hs_link_start();
  NSApplication *app = NSApplication.sharedApplication;
  [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
  hs_app_install_menu();
  hs_experience_open(HS_CH_BREATH);
  double start = hs_now_ms();
  [NSTimer scheduledTimerWithTimeInterval:0.043 repeats:YES block:^(NSTimer *t) {
    (void)t;
    float level, pitch;
    hs_mic_read(&level, &pitch);
    printf("%.2f s level %.3f db %.1f out %.1f chapter %d\n", (hs_now_ms() - start) / 1000, level, hs_mic_decibels(), hs_sound_output_db(), (int)hs_director_chapter());
    fflush(stdout);
    if (hs_now_ms() - start > seconds * 1000) { hs_experience_close(); exit(0); }
  }];
  [app run];
  return 0;
}
