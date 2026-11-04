// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "hands/hands.h"
#include "listen/limiter.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

enum { kVoices = 48, kRing = 256 };

typedef struct { float hertz, seconds, strength, pan; } Hit;
typedef struct { Hit items[kRing]; _Atomic unsigned head, tail; } HitRing;
typedef struct { float re, im, cos_step, sin_step, decay, left, right; bool on; } Voice;
typedef struct { _Atomic float level, tone, pan; } Layer;

static HitRing g_frame_hits, g_block_hits, g_input_hits;
static Layer g_layers[HS_LAYER_COUNT];

// Owned by the block thread.
static Voice g_voices[kVoices];
static int g_next_voice;
static float g_phase[2], g_level[HS_LAYER_COUNT];
static float g_noise_lp, g_noise_hp, g_swell_phase, g_swell_tone;
static uint32_t g_random = 22222;
static Limiter g_limiter;
static float g_view[2];
static _Atomic float g_view_out[2];
static FILE *g_log;

static inline float next_noise(void) {
  g_random ^= g_random << 13;
  g_random ^= g_random >> 17;
  g_random ^= g_random << 5;
  return (float)(int32_t)g_random / 2147483648.0f;
}

static void push(HitRing *ring, float hertz, float seconds, float strength, float pan) {
  unsigned head = atomic_load_explicit(&ring->head, memory_order_relaxed);
  if (head - atomic_load_explicit(&ring->tail, memory_order_acquire) >= kRing) return;
  ring->items[head % kRing] = (Hit){hertz, seconds, strength, pan};
  atomic_store_explicit(&ring->head, head + 1, memory_order_release);
}

void hs_hands_hit(float hertz, float seconds, float strength, float pan) { push(&g_frame_hits, hertz, seconds, strength, pan); }
void hs_hands_hit_rt(float hertz, float seconds, float strength, float pan) { push(&g_block_hits, hertz, seconds, strength, pan); }
void hs_hands_hit_now(float hertz, float seconds, float strength, float pan) { push(&g_input_hits, hertz, seconds, strength, pan); }

void hs_hands_set(hs_hand_layer layer, float level, float tone, float pan) {
  atomic_store_explicit(&g_layers[layer].level, level, memory_order_relaxed);
  atomic_store_explicit(&g_layers[layer].tone, tone, memory_order_relaxed);
  atomic_store_explicit(&g_layers[layer].pan, pan, memory_order_relaxed);
}

void hs_hands_init(void) {
  for (int i = 0; i < kVoices; i++) g_voices[i].on = false;
  for (int i = 0; i < HS_LAYER_COUNT; i++) { hs_hands_set((hs_hand_layer)i, 0, 60, 0); g_level[i] = 0; }
  atomic_store(&g_frame_hits.tail, atomic_load(&g_frame_hits.head));
  atomic_store(&g_block_hits.tail, atomic_load(&g_block_hits.head));
  atomic_store(&g_input_hits.tail, atomic_load(&g_input_hits.head));
  g_swell_tone = 70;
  limiter_init(&g_limiter, HS_WAVE_RATE, 3.0, 60.0, 0.97f);
}

void hs_hands_silence(void) {
  for (int i = 0; i < HS_LAYER_COUNT; i++) atomic_store_explicit(&g_layers[i].level, 0, memory_order_relaxed);
}

// High notes hardly move these actuators, so everything written above their working range is folded down into it, keeping its order.
static float felt_hertz(float hertz) { return hertz <= 100 ? fmaxf(hertz, 45) : 100 + (hertz - 100) * 0.3f; }

static void take_hits(HitRing *ring) {
  unsigned tail = atomic_load_explicit(&ring->tail, memory_order_relaxed), head = atomic_load_explicit(&ring->head, memory_order_acquire);
  for (; tail != head; tail++) {
    const Hit *h = &ring->items[tail % kRing];
    Voice *v = &g_voices[g_next_voice];
    g_next_voice = (g_next_voice + 1) % kVoices;
    float w = 2 * (float)M_PI * felt_hertz(h->hertz) / HS_WAVE_RATE;
    v->re = 0;
    v->im = h->strength;
    v->cos_step = cosf(w);
    v->sin_step = sinf(w);
    // Long enough for a couple of swings: a click shorter than one swing of the actuator never reaches the hand.
    v->decay = exp2f(-1.0f / fmaxf(fmaxf(h->seconds * 0.12f, 0.012f) * HS_WAVE_RATE, 1.0f));
    // Equal power across the two hands; a centred hit is felt in both as one.
    float angle = (h->pan + 1) * (float)M_PI / 4;
    v->left = cosf(angle) * (float)M_SQRT2;
    v->right = sinf(angle) * (float)M_SQRT2;
    v->on = true;
  }
  atomic_store_explicit(&ring->tail, tail, memory_order_release);
}

static const float kFeltCurve = 3.2f, kFeltFloor = 0.012f;

void hs_hands_block(int8_t out[HS_WAVE_BYTES]) {
  take_hits(&g_frame_hits);
  take_hits(&g_block_hits);
  take_hits(&g_input_hits);
  float target[HS_LAYER_COUNT], tone[HS_LAYER_COUNT], pan[HS_LAYER_COUNT];
  for (int i = 0; i < HS_LAYER_COUNT; i++) {
    target[i] = atomic_load_explicit(&g_layers[i].level, memory_order_relaxed);
    tone[i] = atomic_load_explicit(&g_layers[i].tone, memory_order_relaxed);
    pan[i] = atomic_load_explicit(&g_layers[i].pan, memory_order_relaxed);
  }
  float sheen_angle = (pan[HS_LAYER_SHEEN] + 1) * (float)M_PI / 4, gravel_angle = (pan[HS_LAYER_GRAVEL] + 1) * (float)M_PI / 4;
  float swell_angle = (pan[HS_LAYER_SWELL] + 1) * (float)M_PI / 4;
  float sheen_l = cosf(sheen_angle) * (float)M_SQRT2, sheen_r = sinf(sheen_angle) * (float)M_SQRT2;
  float gravel_l = cosf(gravel_angle) * (float)M_SQRT2, gravel_r = sinf(gravel_angle) * (float)M_SQRT2;
  float swell_l = cosf(swell_angle) * (float)M_SQRT2, swell_r = sinf(swell_angle) * (float)M_SQRT2;
  float sheen_w = fminf(0.9f, 2 * (float)M_PI * felt_hertz(tone[HS_LAYER_SHEEN]) / HS_WAVE_RATE);
  bool anything = false;
  for (int i = 0; i < HS_LAYER_COUNT; i++) if (target[i] > 0 || g_level[i] > 1e-4f) anything = true;
  float view_l = 0, view_r = 0;
  for (int f = 0; f < HS_WAVE_FRAMES; f++) {
    float l = 0, r = 0;
    for (int i = 0; i < kVoices; i++) {
      Voice *v = &g_voices[i];
      if (!v->on) continue;
      float re = v->re * v->cos_step - v->im * v->sin_step, im = v->re * v->sin_step + v->im * v->cos_step;
      v->re = re * v->decay;
      v->im = im * v->decay;
      l += v->re * v->left;
      r += v->re * v->right;
      if (fabsf(v->re) + fabsf(v->im) < 1e-4f) v->on = false;
      else anything = true;
    }
    if (anything) {
      const float smooth = 0.018f;      // about 18 ms: no layer starts or stops with a click
      for (int k = 0; k < HS_LAYER_COUNT; k++) g_level[k] += (target[k] - g_level[k]) * smooth;
      for (int side = 0; side < 2; side++) {
        g_phase[side] += 2 * (float)M_PI * tone[HS_LAYER_BODY_LEFT + side] / HS_WAVE_RATE;
        if (g_phase[side] > 2 * (float)M_PI) g_phase[side] -= 2 * (float)M_PI;
      }
      l += sinf(g_phase[0]) * g_level[HS_LAYER_BODY_LEFT];
      r += sinf(g_phase[1]) * g_level[HS_LAYER_BODY_RIGHT];
      if (g_level[HS_LAYER_SHEEN] > 1e-4f) {
        float n = next_noise();
        g_noise_lp += (n - g_noise_lp) * sheen_w;
        g_noise_hp += (g_noise_lp - g_noise_hp) * sheen_w * 0.45f;
        float band = (g_noise_lp - g_noise_hp) * 2.2f * g_level[HS_LAYER_SHEEN];
        l += band * sheen_l;
        r += band * sheen_r;
      }
      if (g_level[HS_LAYER_GRAVEL] > 1e-4f && (next_noise() * 0.5f + 0.5f) < tone[HS_LAYER_GRAVEL] / HS_WAVE_RATE) {
        Voice *v = &g_voices[g_next_voice];
        g_next_voice = (g_next_voice + 1) % kVoices;
        float w = 2 * (float)M_PI * (62 + 70 * (next_noise() * 0.5f + 0.5f)) / HS_WAVE_RATE;
        v->re = 0;
        v->im = g_level[HS_LAYER_GRAVEL] * (0.35f + 0.65f * (next_noise() * 0.5f + 0.5f));
        v->cos_step = cosf(w);
        v->sin_step = sinf(w);
        v->decay = 0.985f;
        v->left = gravel_l;
        v->right = gravel_r;
        v->on = true;
      }
      if (g_level[HS_LAYER_SWELL] > 1e-4f) {
        g_swell_phase += 2 * (float)M_PI * tone[HS_LAYER_SWELL] / HS_WAVE_RATE;
        if (g_swell_phase > 2 * (float)M_PI) g_swell_phase -= 2 * (float)M_PI;
        g_swell_tone += 2 * (float)M_PI * 62.0f / HS_WAVE_RATE;
        if (g_swell_tone > 2 * (float)M_PI) g_swell_tone -= 2 * (float)M_PI;
        float wave = 0.5f + 0.5f * sinf(g_swell_phase);
        float s = sinf(g_swell_tone) * wave * wave * g_level[HS_LAYER_SWELL];
        l += s * swell_l;
        r += s * swell_r;
      }
    }
    // Measured with the controller's own sensor: these actuators move most near 60 to 70 Hz, and small numbers are barely felt at all. So what
    // is written as quiet still arrives as something a hand notices, and true zero stays true zero.
    // The last of a dying tail is let go of before the curve, or it would turn into a long faint buzz where silence was written.
    l = fabsf(l) < kFeltFloor ? 0 : copysignf((1 - expf(-fabsf(l) * kFeltCurve)) / (1 - expf(-kFeltCurve)), l);
    r = fabsf(r) < kFeltFloor ? 0 : copysignf((1 - expf(-fabsf(r) * kFeltCurve)) / (1 - expf(-kFeltCurve)), r);
    limiter_run(&g_limiter, &l, &r);
    int a = (int)lrintf(l * 118), b = (int)lrintf(r * 118);
    a = a > 118 ? 118 : a < -118 ? -118 : a;
    b = b > 118 ? 118 : b < -118 ? -118 : b;
    out[f * 2] = (int8_t)a;
    out[f * 2 + 1] = (int8_t)b;
    view_l += fabsf(l);
    view_r += fabsf(r);
  }
  // A coil on screen moves with the actuator it stands for.
  view_l /= HS_WAVE_FRAMES;
  view_r /= HS_WAVE_FRAMES;
  g_view[0] += (view_l - g_view[0]) * 0.5f;
  g_view[1] += (view_r - g_view[1]) * 0.5f;
  atomic_store_explicit(&g_view_out[0], g_view[0], memory_order_relaxed);
  atomic_store_explicit(&g_view_out[1], g_view[1], memory_order_relaxed);
  if (g_log) fwrite(out, 1, HS_WAVE_BYTES, g_log);
}

void hs_hands_view(float *left, float *right) {
  *left = atomic_load_explicit(&g_view_out[0], memory_order_relaxed);
  *right = atomic_load_explicit(&g_view_out[1], memory_order_relaxed);
}

void hs_hands_log_to(const char *path) {
  if (g_log) fclose(g_log);
  g_log = path ? fopen(path, "wb") : NULL;
}
