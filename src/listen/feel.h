// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Turns whatever the Mac is playing into what the hands should feel. Pure signal processing, so it runs the same in the app
// and in the tests. There is one design and no settings: music, speech and film are told apart by the signal itself.
#pragma once
#include "link/protocol.h"
#include "listen/filters.h"
#include "listen/limiter.h"

enum { HS_FEEL_RATE = 45000, HS_FEEL_FRAMES = 960, HS_FEEL_DECIMATE = 15, HS_FEEL_HISTORY = 94 };

typedef struct {
  float body_peak;      // what the two hands share, 0 to 1
  float hit;            // how far this moment stands above what has been going on, 0 to 1
  float left, right;    // the level each hand really receives, 0 to 1
  float speech;         // how much this sounds like speech, 0 to 1
  float gain;           // the slow level follower, for tests
} hs_feel_view;

typedef struct {
  hs_biquad high35, low140a, low140b, sub_low60, harmonic_low;
  hs_biquad attack_high[2], attack_low[2];
  hs_biquad speech_high, speech_low, low_band;
  float fast, slow;
  float level, program;
  int quiet_blocks, settling_blocks, held_blocks;
  float speech_history[HS_FEEL_HISTORY], speech_power[HS_FEEL_HISTORY], low_power[HS_FEEL_HISTORY];
  int history_at, history_count;
  float speech_score, speech_cut;
  float sub_mean;
  float rumble_big, rumble_small;
  Limiter limiter;
  hs_feel_view view;
} hs_feel;

void hs_feel_init(hs_feel *feel);

// One block: 960 stereo frames at 45 kHz in, 64 stereo frames at 3 kHz out, 21.33 ms.
void hs_feel_process(hs_feel *feel, const float *stereo, int8_t out[HS_WAVE_BYTES]);

// What a narrow link can carry instead: the firmware's rumble bytes, from the same analysis.
void hs_feel_rumble_bytes(const hs_feel *feel, uint8_t *big, uint8_t *small);

// A narrow link carries a stream at about 70 percent of its rate, with gaps between the blocks. Each block then starts and
// ends at rest, so a gap is a soft pause and not a click. Returns false when the block is silent and need not be sent.
