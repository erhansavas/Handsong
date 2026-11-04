// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "experience/voice_out.h"
#include "experience/stage.h"
#include <math.h>
#include <stdatomic.h>

enum { kVoices = 12 };

typedef struct { float re, im, cos_step, sin_step, decay; bool on; } Tine;
typedef struct { float hertz, gain; } Pluck;

static const int16_t *g_piece;
static long g_piece_frames;
static long g_at;
static _Atomic bool g_active;
static _Atomic float g_volume = 1, g_level_out;
static float g_volume_now, g_level;
static Tine g_tines[kVoices * 2];
static int g_next;
static Pluck g_plucks[32];
static _Atomic unsigned g_pluck_head, g_pluck_tail;

void hs_voice_load(const int16_t *mono_45k, long frames) {
  g_piece = mono_45k;
  g_piece_frames = frames;
}

void hs_voice_start(void) {
  g_at = 0;
  g_volume_now = 0;
  atomic_store(&g_active, true);
}

void hs_voice_stop(void) { atomic_store(&g_active, false); }
void hs_voice_set_volume(float gain) { atomic_store(&g_volume, gain); }
float hs_voice_level(void) { return atomic_load_explicit(&g_level_out, memory_order_relaxed); }
bool hs_voice_active(void) { return atomic_load(&g_active); }

void hs_speaker_pluck(float hertz, float gain) {
  // Two writers, the input thread and the frame, neither of them the real-time one: they take turns for this one short moment.
  static atomic_flag writing = ATOMIC_FLAG_INIT;
  while (atomic_flag_test_and_set_explicit(&writing, memory_order_acquire)) {}
  unsigned head = atomic_load_explicit(&g_pluck_head, memory_order_relaxed);
  if (head - atomic_load_explicit(&g_pluck_tail, memory_order_acquire) < 32) {
    g_plucks[head % 32] = (Pluck){hertz, gain};
    atomic_store_explicit(&g_pluck_head, head + 1, memory_order_release);
  }
  atomic_flag_clear_explicit(&writing, memory_order_release);
}

static void start_tine(float hertz, float gain, float seconds) {
  Tine *v = &g_tines[g_next];
  g_next = (g_next + 1) % (kVoices * 2);
  float w = 2 * (float)M_PI * hertz / HS_SPEAKER_CLOCK;
  v->re = 0;
  v->im = gain;
  v->cos_step = cosf(w);
  v->sin_step = sinf(w);
  v->decay = expf(-1.0f / (seconds * HS_SPEAKER_CLOCK));
  v->on = true;
}

void hs_voice_block(float *stereo) {
  unsigned tail = atomic_load_explicit(&g_pluck_tail, memory_order_relaxed), head = atomic_load_explicit(&g_pluck_head, memory_order_acquire);
  for (; tail != head; tail++) {
    const Pluck *p = &g_plucks[tail % 32];
    start_tine(p->hertz, p->gain, 0.5f);
    start_tine(p->hertz * 2.0f, p->gain * 0.10f, 0.12f);     // the same soft upper mode as the written tines
  }
  atomic_store_explicit(&g_pluck_tail, tail, memory_order_release);
  float want = atomic_load_explicit(&g_volume, memory_order_relaxed), peak = 0;
  for (int f = 0; f < HS_SPEAKER_PACKET_FRAMES * 2; f++) {
    float x = 0;
    if (g_piece && g_piece_frames > 0) {
      x = (float)g_piece[g_at] / 32768.0f;
      g_at = (g_at + 1) % g_piece_frames;
    }
    for (int i = 0; i < kVoices * 2; i++) {
      Tine *v = &g_tines[i];
      if (!v->on) continue;
      float re = v->re * v->cos_step - v->im * v->sin_step, im = v->re * v->sin_step + v->im * v->cos_step;
      v->re = re * v->decay;
      v->im = im * v->decay;
      x += v->re;
      if (fabsf(v->re) + fabsf(v->im) < 1e-4f) v->on = false;
    }
    g_volume_now += (want - g_volume_now) * 0.0006f;
    x *= g_volume_now;
    if (fabsf(x) > peak) peak = fabsf(x);
    stereo[f * 2] = stereo[f * 2 + 1] = x;
  }
  g_level = fmaxf(peak, g_level * 0.85f);
  atomic_store_explicit(&g_level_out, g_level, memory_order_relaxed);
}
