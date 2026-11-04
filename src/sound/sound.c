// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "sound/sound.h"
#include "listen/limiter.h"
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

enum { kModes = 6, kVoices = 96, kRingSize = 512, kCrossfades = 2 };

// Each material's partials, from the literature's loss factors: metal rings on, wood and paper die at once, and the rate at which
// highs decay tells the ear the material before anything else does.
typedef struct { float ratio[kModes], decay[kModes], level[kModes]; float attack_noise; } Material;
static const Material kMaterials[HS_MAT_SOUND_COUNT] = {
  [HS_MAT_SOUND_BRASS] = {{1, 2.32f, 3.76f, 5.41f, 7.13f, 9.02f}, {0.55f, 0.34f, 0.22f, 0.15f, 0.09f, 0.06f}, {1, 0.55f, 0.42f, 0.26f, 0.16f, 0.09f}, 0.15f},
  [HS_MAT_SOUND_STEEL] = {{1, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f}, {0.30f, 0.20f, 0.13f, 0.08f, 0.05f, 0.03f}, {1, 0.60f, 0.40f, 0.25f, 0.14f, 0.08f}, 0.25f},
  [HS_MAT_SOUND_CERAMIC] = {{1, 1.98f, 3.21f, 4.42f, 6.8f, 9.1f}, {0.05f, 0.035f, 0.024f, 0.017f, 0.011f, 0.008f}, {1, 0.7f, 0.5f, 0.35f, 0.2f, 0.1f}, 0.35f},
  [HS_MAT_SOUND_GLASS] = {{1, 2.14f, 3.57f, 5.12f, 7.42f, 10.2f}, {0.65f, 0.42f, 0.27f, 0.17f, 0.10f, 0.06f}, {1, 0.45f, 0.5f, 0.28f, 0.2f, 0.1f}, 0.1f},
  [HS_MAT_SOUND_WOOD] = {{1, 2.42f, 3.89f, 6.3f, 9.0f, 12.0f}, {0.045f, 0.028f, 0.016f, 0.010f, 0.006f, 0.004f}, {1, 0.5f, 0.3f, 0.16f, 0.08f, 0.04f}, 0.4f},
  [HS_MAT_SOUND_PAPER] = {{1, 1.73f, 2.61f, 3.8f, 5.3f, 7.7f}, {0.018f, 0.012f, 0.009f, 0.007f, 0.005f, 0.003f}, {1, 0.8f, 0.6f, 0.4f, 0.3f, 0.2f}, 0.8f},
  [HS_MAT_SOUND_BELL] = {{0.56f, 1, 1.19f, 1.5f, 2.0f, 2.51f}, {2.2f, 1.6f, 1.2f, 0.8f, 0.5f, 0.3f}, {0.7f, 1, 0.5f, 0.4f, 0.3f, 0.15f}, 0.05f},
};

typedef struct { float re, im, cos_step, sin_step, decay, left, right; bool on; } Mode;
typedef struct { hs_sound_material material; float hertz, gain, pan; hs_sound_timing timing; } Strike;
typedef struct { Strike items[kRingSize]; _Atomic unsigned head, tail; } StrikeRing;
typedef struct { Strike strike; long at; bool waiting; } Delayed;
typedef struct { _Atomic float level, tone; } Param;

typedef struct {
  int stem;
  double at;
  float gain, step;
  int left;
  float target;
} Fade;

typedef struct {
  Fade a, b;                 // old stem fading out, new stem fading in
  _Atomic int want_stem;
  _Atomic float want_gain, want_seconds;
  _Atomic bool want_loop;
  _Atomic unsigned want_generation;
  unsigned seen_generation;
  bool loop;
} Slot;

static StrikeRing g_frame_strikes, g_block_strikes, g_input_strikes;
static Param g_params[HS_SOUND_LAYER_COUNT];
static Slot g_slots[HS_STEM_SLOTS];
static const hs_stem *g_stems;
static int g_stem_count;
static _Atomic int g_room_latency = 1024;
static _Atomic bool g_paused;
static _Atomic float g_master_want = 1, g_master_seconds = 0.05f;

// Owned by the audio thread.
static Mode g_modes[kVoices];
static int g_next_mode;
static Delayed g_delayed[128];
static long g_clock;
static float g_level[HS_SOUND_LAYER_COUNT], g_phase[HS_SOUND_LAYER_COUNT], g_lp[HS_SOUND_LAYER_COUNT], g_bp[HS_SOUND_LAYER_COUNT];
static float g_pause_mix, g_lp_left, g_lp_right, g_master = 1;
static uint32_t g_random = 12345;
static Limiter g_limiter;
static long g_blocks, g_reduced_blocks;

static inline float noise(void) {
  g_random ^= g_random << 13;
  g_random ^= g_random >> 17;
  g_random ^= g_random << 5;
  return (float)(int32_t)g_random / 2147483648.0f;
}

static void push(StrikeRing *ring, Strike s) {
  unsigned head = atomic_load_explicit(&ring->head, memory_order_relaxed);
  if (head - atomic_load_explicit(&ring->tail, memory_order_acquire) >= kRingSize) return;
  ring->items[head % kRingSize] = s;
  atomic_store_explicit(&ring->head, head + 1, memory_order_release);
}

void hs_sound_strike(hs_sound_material material, float hertz, float gain, float pan, hs_sound_timing timing) {
  push(&g_frame_strikes, (Strike){material, hertz, gain, pan, timing});
}

void hs_sound_strike_rt(hs_sound_material material, float hertz, float gain, float pan) {
  push(&g_block_strikes, (Strike){material, hertz, gain, pan, HS_TIMING_ALIGNED});
}

void hs_sound_strike_now(hs_sound_material material, float hertz, float gain, float pan) {
  push(&g_input_strikes, (Strike){material, hertz, gain, pan, HS_TIMING_DIRECT});
}

void hs_sound_strike_with_hand(hs_sound_material material, float hertz, float gain, float pan) {
  push(&g_input_strikes, (Strike){material, hertz, gain, pan, HS_TIMING_ALIGNED});
}

void hs_sound_set(hs_sound_layer layer, float level, float tone) {
  atomic_store_explicit(&g_params[layer].level, level, memory_order_relaxed);
  atomic_store_explicit(&g_params[layer].tone, tone, memory_order_relaxed);
}

void hs_sound_set_stems(const hs_stem *stems, int count) {
  g_stems = stems;
  g_stem_count = count;
}

void hs_sound_score(int slot, int stem, float gain, float seconds, bool loop) {
  Slot *s = &g_slots[slot];
  atomic_store(&s->want_stem, stem);
  atomic_store(&s->want_gain, gain);
  atomic_store(&s->want_seconds, seconds);
  atomic_store(&s->want_loop, loop);
  atomic_fetch_add(&s->want_generation, 1);
}

void hs_sound_score_gain(int slot, float gain, float seconds) {
  hs_sound_score(slot, -2, gain, seconds, false);
}

void hs_sound_set_room_latency(int frames) { atomic_store(&g_room_latency, frames); }
void hs_sound_set_pause(bool paused) { atomic_store(&g_paused, paused); }
void hs_sound_set_master(float gain, float seconds) {
  atomic_store(&g_master_want, gain);
  atomic_store(&g_master_seconds, seconds);
}

void hs_sound_init(void) {
  memset(g_modes, 0, sizeof g_modes);
  memset(g_delayed, 0, sizeof g_delayed);
  for (int i = 0; i < HS_STEM_SLOTS; i++) {
    Slot *s = &g_slots[i];
    s->a = (Fade){.stem = -1};
    s->b = (Fade){.stem = -1};
    atomic_store(&s->want_stem, -1);
    s->seen_generation = atomic_load(&s->want_generation);
  }
  for (int i = 0; i < HS_SOUND_LAYER_COUNT; i++) hs_sound_set((hs_sound_layer)i, 0, 0);
  atomic_store(&g_frame_strikes.tail, atomic_load(&g_frame_strikes.head));
  atomic_store(&g_block_strikes.tail, atomic_load(&g_block_strikes.head));
  atomic_store(&g_input_strikes.tail, atomic_load(&g_input_strikes.head));
  // Ceiling a little under -3 dBFS: true peaks between samples stay below -2 dBTP.
  limiter_init(&g_limiter, HS_SOUND_RATE, 1.5, 120.0, 0.70f);
  g_clock = 0;
  g_master = 1;
  g_pause_mix = 0;
  atomic_store(&g_master_want, 1.0f);     // the last session faded itself out on leaving; this one starts with its sound
  g_blocks = g_reduced_blocks = 0;
}

static FILE *g_event_log;
void hs_sound_log_events_to(const char *path) { g_event_log = fopen(path, "w"); }

static void start_strike(const Strike *s) {
  if (g_event_log) fprintf(g_event_log, "%ld\n", g_clock);
  const Material *m = &kMaterials[s->material];
  float angle = (s->pan + 1) * (float)M_PI / 4;
  float left = cosf(angle), right = sinf(angle);
  for (int k = 0; k < kModes; k++) {
    float hz = s->hertz * m->ratio[k];
    if (hz > HS_SOUND_RATE * 0.45f) continue;
    Mode *v = &g_modes[g_next_mode];
    g_next_mode = (g_next_mode + 1) % kVoices;
    float w = 2 * (float)M_PI * hz / HS_SOUND_RATE;
    v->re = 0;
    v->im = s->gain * m->level[k];
    v->cos_step = cosf(w);
    v->sin_step = sinf(w);
    v->decay = expf(-1.0f / (m->decay[k] * HS_SOUND_RATE));
    v->left = left;
    v->right = right;
    v->on = true;
  }
}

static void take_strikes(StrikeRing *ring, long due) {
  unsigned tail = atomic_load_explicit(&ring->tail, memory_order_relaxed), head = atomic_load_explicit(&ring->head, memory_order_acquire);
  for (; tail != head; tail++) {
    const Strike *s = &ring->items[tail % kRingSize];
    if (s->timing == HS_TIMING_DIRECT) { start_strike(s); continue; }
    for (int i = 0; i < 128; i++) {
      if (g_delayed[i].waiting) continue;
      g_delayed[i] = (Delayed){*s, due, true};
      break;
    }
  }
  atomic_store_explicit(&ring->tail, tail, memory_order_release);
}

static inline void mix_stem(Fade *f, bool loop, float *l, float *r) {
  if (f->stem < 0 || f->stem >= g_stem_count) return;
  const hs_stem *stem = &g_stems[f->stem];
  long at = (long)f->at;
  if (at >= stem->frames) {
    if (!loop) { f->stem = -1; return; }
    at -= stem->frames;
    f->at = (double)at;
  }
  float gain = f->gain;
  if (f->left > 0) {
    f->gain += f->step;
    f->left--;
  } else if (f->gain != f->target) {
    f->gain = f->target;
  }
  *l += gain * (float)stem->samples[at * 2] / 32768.0f;
  *r += gain * (float)stem->samples[at * 2 + 1] / 32768.0f;
  f->at += 1;
}

static void update_slots(int frames) {
  (void)frames;
  for (int i = 0; i < HS_STEM_SLOTS; i++) {
    Slot *s = &g_slots[i];
    unsigned generation = atomic_load_explicit(&s->want_generation, memory_order_acquire);
    if (generation == s->seen_generation) continue;
    s->seen_generation = generation;
    int stem = atomic_load(&s->want_stem);
    float gain = atomic_load(&s->want_gain), seconds = atomic_load(&s->want_seconds);
    int count = (int)fmaxf(1.0f, seconds * HS_SOUND_RATE);
    if (stem == -2) {        // the same stem at a new level
      s->b.target = gain;
      s->b.step = (gain - s->b.gain) / (float)count;
      s->b.left = count;
      continue;
    }
    // Whatever is playing becomes the one fading out, from the level it has now, so nothing jumps.
    s->a = s->b;
    s->a.target = 0;
    s->a.step = -s->a.gain / (float)count;
    s->a.left = count;
    if (g_event_log) fprintf(g_event_log, "%ld\n", g_clock);
    s->b = (Fade){.stem = stem, .at = 0, .gain = 0, .target = gain, .step = gain / (float)count, .left = count};
    s->loop = atomic_load(&s->want_loop);
  }
}

static const float kOverall = 1.32f;      // the whole mix, once its balance is right, to where a Mac's speakers carry it
static const float kMusicLevel = 0.72f, kMusicDuck = 0.6f, kFrontWeight = 10.0f;
static const float kFrontAttack = 0.02f, kFrontRelease = 0.00012f;
static float g_front, g_music_gain = 0.72f, g_music_on = 1;
void hs_sound_mute_music(void) { g_music_on = 0; }

// What is coming out of the Mac now, for the microphone to tell it from a breath.
static _Atomic float g_output_db = -120;
float hs_sound_output_db(void) { return atomic_load_explicit(&g_output_db, memory_order_relaxed); }

void hs_sound_render(float *stereo, int frames) {
  float played_power = 0;
  long now = g_clock;
  long delay = (long)(HS_SOUND_RATE * 0.110) - atomic_load_explicit(&g_room_latency, memory_order_relaxed);
  if (delay < 0) delay = 0;
  take_strikes(&g_frame_strikes, now + delay);
  take_strikes(&g_block_strikes, now + delay);
  take_strikes(&g_input_strikes, now + delay);
  for (int i = 0; i < 128; i++) {
    if (g_delayed[i].waiting && g_delayed[i].at <= now + frames) {
      start_strike(&g_delayed[i].strike);
      g_delayed[i].waiting = false;
    }
  }
  update_slots(frames);
  float level[HS_SOUND_LAYER_COUNT], tone[HS_SOUND_LAYER_COUNT];
  for (int i = 0; i < HS_SOUND_LAYER_COUNT; i++) {
    level[i] = atomic_load_explicit(&g_params[i].level, memory_order_relaxed);
    tone[i] = atomic_load_explicit(&g_params[i].tone, memory_order_relaxed);
  }
  float pause_target = atomic_load_explicit(&g_paused, memory_order_relaxed) ? 1.0f : 0.0f;
  float master_want = atomic_load_explicit(&g_master_want, memory_order_relaxed);
  float master_step = 1 - expf(-1.0f / (fmaxf(atomic_load_explicit(&g_master_seconds, memory_order_relaxed), 0.002f) * HS_SOUND_RATE));
  float most = 0;
  for (int f = 0; f < frames; f++) {
    float l = 0, r = 0, music_l = 0, music_r = 0;
    for (int i = 0; i < kVoices; i++) {
      Mode *v = &g_modes[i];
      if (!v->on) continue;
      float re = v->re * v->cos_step - v->im * v->sin_step, im = v->re * v->sin_step + v->im * v->cos_step;
      v->re = re * v->decay;
      v->im = im * v->decay;
      l += v->re * v->left;
      r += v->re * v->right;
      if (fabsf(v->re) + fabsf(v->im) < 2e-5f) v->on = false;
    }
    // The first two slots are the music; the others are events and belong in the front with everything else.
    for (int i = 0; i < HS_STEM_SLOTS; i++) {
      mix_stem(&g_slots[i].a, false, i < 2 ? &music_l : &l, i < 2 ? &music_r : &r);
      mix_stem(&g_slots[i].b, g_slots[i].loop, i < 2 ? &music_l : &l, i < 2 ? &music_r : &r);
    }
    // Sounds that follow the hands.
    const float smooth = 0.0012f;    // about 17 ms
    for (int k = 0; k < HS_SOUND_LAYER_COUNT; k++) g_level[k] += (level[k] - g_level[k]) * smooth;
    float n = noise(), layered = 0;
    if (g_level[HS_SOUND_RUB] > 1e-4f) {           // a finger on glass: a narrow singing band
      float w = fminf(0.9f, 2 * (float)M_PI * (900 + 2600 * tone[HS_SOUND_RUB]) / HS_SOUND_RATE);
      g_bp[HS_SOUND_RUB] += w * g_lp[HS_SOUND_RUB];
      g_lp[HS_SOUND_RUB] += w * (n * 0.05f - g_bp[HS_SOUND_RUB] - 0.06f * g_lp[HS_SOUND_RUB]);
      layered += g_lp[HS_SOUND_RUB] * g_level[HS_SOUND_RUB] * 0.9f;
    }
    if (g_level[HS_SOUND_ROLL] > 1e-4f) {          // a rolling ball: low rumble
      float w = fminf(0.5f, 2 * (float)M_PI * (120 + 900 * tone[HS_SOUND_ROLL]) / HS_SOUND_RATE);
      g_lp[HS_SOUND_ROLL] += (n - g_lp[HS_SOUND_ROLL]) * w;
      layered += g_lp[HS_SOUND_ROLL] * g_level[HS_SOUND_ROLL] * 1.6f;
    }
    if (g_level[HS_SOUND_LOAD] > 1e-4f) {          // the arm under load: gear whine that rises with the pull
      g_phase[HS_SOUND_LOAD] += 2 * (float)M_PI * (70 + 170 * tone[HS_SOUND_LOAD]) / HS_SOUND_RATE;
      if (g_phase[HS_SOUND_LOAD] > 2 * (float)M_PI) g_phase[HS_SOUND_LOAD] -= 2 * (float)M_PI;
      float p = g_phase[HS_SOUND_LOAD];
      layered += (sinf(p) + 0.45f * sinf(2 * p) + 0.2f * sinf(3 * p) + 0.08f * sinf(7 * p)) * g_level[HS_SOUND_LOAD] * 0.5f;
    }
    if (g_level[HS_SOUND_STEAM] > 1e-4f || g_level[HS_SOUND_BELLOWS] > 1e-4f || g_level[HS_SOUND_HISS] > 1e-4f) {
      float steam_w = fminf(0.9f, 2 * (float)M_PI * (1500 + 5000 * tone[HS_SOUND_STEAM]) / HS_SOUND_RATE);
      g_lp[HS_SOUND_STEAM] += (n - g_lp[HS_SOUND_STEAM]) * steam_w;
      layered += (n - g_lp[HS_SOUND_STEAM]) * g_level[HS_SOUND_STEAM] * 0.35f;
      float bellows_w = fminf(0.5f, 2 * (float)M_PI * (240 + 900 * tone[HS_SOUND_BELLOWS]) / HS_SOUND_RATE);
      g_lp[HS_SOUND_BELLOWS] += (n - g_lp[HS_SOUND_BELLOWS]) * bellows_w;
      layered += g_lp[HS_SOUND_BELLOWS] * g_level[HS_SOUND_BELLOWS] * 0.8f;
      float hiss_w = fminf(0.95f, 2 * (float)M_PI * (5000 + 7000 * tone[HS_SOUND_HISS]) / HS_SOUND_RATE);
      g_lp[HS_SOUND_HISS] += (n - g_lp[HS_SOUND_HISS]) * hiss_w;
      layered += (n - g_lp[HS_SOUND_HISS]) * g_level[HS_SOUND_HISS] * 0.04f;
    }
    if (g_level[HS_SOUND_DRONE] > 1e-4f) {
      g_phase[HS_SOUND_DRONE] += 2 * (float)M_PI * (36 + 90 * tone[HS_SOUND_DRONE]) / HS_SOUND_RATE;
      if (g_phase[HS_SOUND_DRONE] > 2 * (float)M_PI) g_phase[HS_SOUND_DRONE] -= 2 * (float)M_PI;
      float p = g_phase[HS_SOUND_DRONE];
      layered += (sinf(p) + 0.3f * sinf(2 * p + 0.3f) + 0.14f * sinf(3 * p + 1.1f)) * g_level[HS_SOUND_DRONE] * 0.5f;
    }
    l += layered;
    r += layered;
    // One thing at a time: what you just did stands in front, and the music steps back under it and returns slowly, instead of all of it competing.
    float front = fmaxf(fabsf(l), fabsf(r));
    g_front += (front - g_front) * (front > g_front ? kFrontAttack : kFrontRelease);
    float room_for_music = g_music_on * kMusicLevel * (1 - fminf(1, g_front * kFrontWeight) * kMusicDuck);
    g_music_gain += (room_for_music - g_music_gain) * 0.002f;
    l += music_l * g_music_gain;
    r += music_r * g_music_gain;
    // A pause is a filter closing and the level falling, and it opens again the same way.
    g_pause_mix += (pause_target - g_pause_mix) * 0.00025f;
    if (g_pause_mix > 1e-3f) {
      float k = 1 - g_pause_mix * 0.93f;
      g_lp_left += (l - g_lp_left) * k;
      g_lp_right += (r - g_lp_right) * k;
      float down = 1 - g_pause_mix * 0.75f;
      l = g_lp_left * down;
      r = g_lp_right * down;
    } else {
      g_lp_left = l;
      g_lp_right = r;
    }
    g_master += (master_want - g_master) * master_step;
    l *= g_master * kOverall;
    r *= g_master * kOverall;
    float in = fmaxf(fabsf(l), fabsf(r));
    limiter_run(&g_limiter, &l, &r);
    played_power += l * l + r * r;
    if (in > most) most = in;
    stereo[f * 2] = l;
    stereo[f * 2 + 1] = r;
  }
  atomic_store_explicit(&g_output_db, 10 * log10f(played_power / (float)(frames * 2) + 1e-12f), memory_order_relaxed);
  g_clock += frames;
  g_blocks++;
  if (most > 0.70f * 1.122f) g_reduced_blocks++;    // more than 1 dB of reduction
}

long hs_sound_clipped_blocks(void) { return g_reduced_blocks; }
long hs_sound_blocks(void) { return g_blocks; }
