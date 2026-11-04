// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "listen/feel.h"
#include "listen/filters.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// How the three things the hands get are weighed. All of these were set by measuring the test signals, and are finished by hand.
enum { kQuietBlocksForNewSong = 24, kSettlingBlocks = 140, kHoldBlocks = 33 };
static const float kOverallDrive = 1.25f;
static const float kWhileHeld = 0.42f;          // a held bass note, against the same note as a hit
static const float kEdgeStrength = 0.45f;       // the top of a hit, and the only stereo part
static const float kOctaveUp = 0.55f;           // these actuators hardly move under 50 Hz, so the deepest bass also brings its octave
static const float kWhileTalking = 0.15f;       // what stays of the low body during speech

static inline float rate_per_sample(float milliseconds) { return 1 - expf(-1000.0f / (milliseconds * HS_FEEL_RATE)); }

static inline float eased_between(float from, float to, float x) {
  float t = (x - from) / (to - from);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

static inline float follow(float now, float target, float rise, float fall) { return now + (target > now ? rise : fall) * (target - now); }

void hs_feel_init(hs_feel *feel) {
  memset(feel, 0, sizeof *feel);
  const double rate = HS_FEEL_RATE;
  feel->high35 = hs_filter_design(HS_FILTER_HIGH_PASS, rate, 35, 0.707, 0);
  feel->low140a = hs_filter_design(HS_FILTER_LOW_PASS, rate, 140, 0.54, 0);
  feel->low140b = hs_filter_design(HS_FILTER_LOW_PASS, rate, 140, 1.31, 0);
  feel->sub_low60 = hs_filter_design(HS_FILTER_LOW_PASS, rate, 60, 0.707, 0);
  feel->harmonic_low = hs_filter_design(HS_FILTER_LOW_PASS, rate, 160, 0.707, 0);
  feel->speech_high = hs_filter_design(HS_FILTER_HIGH_PASS, rate, 300, 0.707, 0);
  feel->speech_low = hs_filter_design(HS_FILTER_LOW_PASS, rate, 3000, 0.707, 0);
  feel->low_band = hs_filter_design(HS_FILTER_LOW_PASS, rate, 100, 0.707, 0);
  for (int side = 0; side < 2; side++) {
    feel->attack_high[side] = hs_filter_design(HS_FILTER_HIGH_PASS, rate, 150, 0.707, 0);
    feel->attack_low[side] = hs_filter_design(HS_FILTER_LOW_PASS, rate, 420, 0.707, 0);
  }
  feel->level = 0.02f;
  feel->quiet_blocks = kQuietBlocksForNewSong;
  feel->speech_cut = 1;
  limiter_init(&feel->limiter, HS_WAVE_RATE, 2.0, 45.0, 0.97f);
}

// Speech comes and goes about four times a second and has little under 100 Hz; music with a bass line does neither. Both are judged over
// two seconds, because between two syllables there is nothing to judge.
static void judge_speech(hs_feel *feel, float voice_power, float low_power) {
  int slot = feel->history_at;
  feel->speech_history[slot] = 10 * log10f(voice_power + 1e-9f);
  feel->speech_power[slot] = voice_power;
  feel->low_power[slot] = low_power;
  feel->history_at = (slot + 1) % HS_FEEL_HISTORY;
  if (feel->history_count < HS_FEEL_HISTORY) feel->history_count++;
  float level_sum = 0, level_squares = 0, voice_sum = 0, low_sum = 0;
  int count = feel->history_count;
  for (int i = 0; i < count; i++) {
    level_sum += feel->speech_history[i];
    level_squares += feel->speech_history[i] * feel->speech_history[i];
    voice_sum += feel->speech_power[i];
    low_sum += feel->low_power[i];
  }
  float average = level_sum / (float)count;
  float swing = sqrtf(fmaxf(0, level_squares / (float)count - average * average));
  float low_against_voice = 10 * log10f((low_sum + 1e-9f) / (voice_sum + 1e-9f));
  float verdict = voice_sum / (float)count > 1e-7f ? eased_between(4.0f, 8.0f, swing) * eased_between(-4.0f, -14.0f, low_against_voice) : 0;
  feel->speech_score = follow(feel->speech_score, verdict, 0.06f, 0.04f);
  feel->speech_cut = 1 - (1 - kWhileTalking) * eased_between(0.25f, 0.75f, feel->speech_score);
}

typedef struct {
  float body[HS_FEEL_FRAMES], hit_share[HS_FEEL_FRAMES], edges[HS_FEEL_FRAMES * 2];
  float body_peak, full_peak, voice_power, low_power;
} Block;

// Sample by sample at the input's rate: the low body in one channel, how much of this moment stands above the last, the stereo top.
static void analyse(hs_feel *feel, const float *stereo, Block *block) {
  const float quick_rise = rate_per_sample(1), quick_fall = rate_per_sample(25), slow_rise = rate_per_sample(70), slow_fall = rate_per_sample(350);
  block->body_peak = block->full_peak = block->voice_power = block->low_power = 0;
  for (int n = 0; n < HS_FEEL_FRAMES; n++) {
    float left = stereo[2 * n], right = stereo[2 * n + 1], one = 0.5f * (left + right);
    float rid_of_rumble = hs_filter_run(&feel->high35, one);
    block->full_peak = fmaxf(block->full_peak, fabsf(rid_of_rumble));
    float low = hs_filter_run(&feel->low140b, hs_filter_run(&feel->low140a, rid_of_rumble));
    // Rectifying the very lowest part doubles its frequency: what these actuators cannot play gets an octave they can.
    float deepest = fabsf(hs_filter_run(&feel->sub_low60, rid_of_rumble));
    feel->sub_mean += (deepest - feel->sub_mean) * 0.0005f;
    low += kOctaveUp * hs_filter_run(&feel->harmonic_low, deepest - feel->sub_mean);
    block->body[n] = low;
    float size = fabsf(low);
    block->body_peak = fmaxf(block->body_peak, size);
    feel->fast = follow(feel->fast, size, quick_rise, quick_fall);
    feel->slow = follow(feel->slow, size, slow_rise, slow_fall);
    block->hit_share[n] = fminf(1, fmaxf(0, feel->fast - feel->slow * 1.15f) / fmaxf(feel->slow, 1e-4f) * 1.2f);
    block->edges[2 * n] = hs_filter_run(&feel->attack_low[0], hs_filter_run(&feel->attack_high[0], left));
    block->edges[2 * n + 1] = hs_filter_run(&feel->attack_low[1], hs_filter_run(&feel->attack_high[1], right));
    float voice = hs_filter_run(&feel->speech_low, hs_filter_run(&feel->speech_high, one));
    block->voice_power += voice * voice;
    float under = hs_filter_run(&feel->low_band, rid_of_rumble);
    block->low_power += under * under;
  }
  block->voice_power /= HS_FEEL_FRAMES;
  block->low_power /= HS_FEEL_FRAMES;
}

// The level follows very slowly, so that quiet and loud masters feel alike and it never pumps with the music. In silence it holds, so a
// pause changes nothing. After half a second of silence a new song may start at any level: for three seconds it looks for it quickly.
static void follow_the_master(hs_feel *feel, const Block *block) {
  if (block->full_peak < 0.003f) {
    if (feel->quiet_blocks < kQuietBlocksForNewSong) feel->quiet_blocks++;
    return;
  }
  if (feel->quiet_blocks >= kQuietBlocksForNewSong) {
    feel->settling_blocks = kSettlingBlocks;
    feel->held_blocks = kHoldBlocks;
    feel->level = fmaxf(feel->level, block->body_peak);
  }
  feel->quiet_blocks = 0;
  float rise = 0.05f, fall = 0.002f;
  if (feel->settling_blocks > 0) {
    feel->settling_blocks--;
    rise = 0.25f;
    if (block->body_peak > feel->level * 0.6f) feel->held_blocks = kHoldBlocks;
    else if (feel->held_blocks > 0) feel->held_blocks--;
    else fall = 0.04f;
  }
  feel->level = follow(feel->level, block->body_peak, rise, fall);
  feel->program = follow(feel->program, block->full_peak, rise, fall);
}

static const float pair_ceiling = 118;

void hs_feel_process(hs_feel *feel, const float *stereo, int8_t out[HS_WAVE_BYTES]) {
  Block block;
  analyse(feel, stereo, &block);
  judge_speech(feel, block.voice_power, block.low_power);
  follow_the_master(feel, &block);
  // Everything that scales this block as a whole, worked out once: how loud the master runs, whether anything is there at all (true silence
  // stays still instead of buzzing), and whether someone is talking. A sudden low blow in a film gets through even then; a syllable's start
  // is a hit too, but has no weight.
  float drive = kOverallDrive / fmaxf(feel->level, fmaxf(0.02f, feel->program * 0.25f));
  float present = fminf(1, block.body_peak / 0.004f);
  bool low_blow = block.low_power > block.voice_power * 2 && block.body_peak > feel->slow * 1.5f;
  float amount = drive * present * (low_blow ? 1.0f : feel->speech_cut);
  float top_room = fminf(1, block.body_peak / fmaxf(block.full_peak * 0.25f, 1e-4f));      // the top of a hit needs a hit under it
  float highest[3] = {0, 0, 0};       // left, right, how much of a hit
  int8_t *next_out = out;
  for (int n = 0; n < HS_FEEL_FRAMES; n += HS_FEEL_DECIMATE) {
    float share = block.hit_share[n], both = block.body[n] * (kWhileHeld + (1 - kWhileHeld) * share);
    float pair[2];
    for (int side = 0; side < 2; side++) pair[side] = amount * (both + block.edges[2 * n + side] * kEdgeStrength * share * top_room);
    limiter_run(&feel->limiter, &pair[0], &pair[1]);
    for (int side = 0; side < 2; side++) {
      // Small values are hardly felt on these actuators (measured), so they are brought up, and zero stays zero.
      float felt = copysignf((1 - expf(-fabsf(pair[side]) * 2.2f)) / (1 - expf(-2.2f)), pair[side]);
      *next_out++ = (int8_t)lrintf(felt * pair_ceiling);
      highest[side] = fmaxf(highest[side], fabsf(pair[side]));
    }
    highest[2] = fmaxf(highest[2], share);
  }
  // The narrow link's fallback: the same body and hit, as the firmware's two rumble motors understand them.
  float body_now = block.body_peak * drive * present;
  float heavy = body_now * (kWhileHeld + (1 - kWhileHeld) * highest[2]) * feel->speech_cut;
  feel->rumble_big = fmaxf(fminf(1, heavy), feel->rumble_big * 0.82f);
  feel->rumble_small = fmaxf(fminf(1, highest[2] * top_room * feel->speech_cut), feel->rumble_small * 0.6f);
  feel->view = (hs_feel_view){fminf(1, body_now), highest[2], highest[0], highest[1], feel->speech_score, drive};
}

void hs_feel_rumble_bytes(const hs_feel *feel, uint8_t *big, uint8_t *small) {
  float heavy = feel->rumble_big, light = feel->rumble_small;
  *big = (uint8_t)lrintf(powf(heavy, 1.3f) * 255);
  *small = (uint8_t)lrintf(powf(light, 1.3f) * 200);
}

