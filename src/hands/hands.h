// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What the hands receive during the experience: struck modes and continuous layers, rendered in blocks of 64 stereo frames at
// 3 kHz. Contrast is the design: every chapter has its own vocabulary and its own true silence, and the output is exact zero
// when nothing is sounding, not a low hum.
#pragma once
#include "link/protocol.h"
#include <stdbool.h>

// A struck mode, from the frame thread or (with _rt) from the block thread. pan is -1 left to 1 right.
void hs_hands_hit(float hertz, float seconds, float strength, float pan);
void hs_hands_hit_rt(float hertz, float seconds, float strength, float pan);
void hs_hands_hit_now(float hertz, float seconds, float strength, float pan);   // from the input thread

typedef enum {
  HS_LAYER_BODY_LEFT, HS_LAYER_BODY_RIGHT,   // a low tone in one hand: level, and the frequency
  HS_LAYER_SHEEN,                            // fine band noise: level, centre frequency, pan
  HS_LAYER_GRAVEL,                           // random small impacts: rate per second, strength, pan
  HS_LAYER_SWELL,                            // a slow wave on a low tone, for water: level, speed, pan
  HS_LAYER_COUNT,
} hs_hand_layer;

void hs_hands_set(hs_hand_layer layer, float level, float tone, float pan);

void hs_hands_init(void);
void hs_hands_silence(void);     // every layer and mode, gently, over one block
void hs_hands_block(int8_t out[HS_WAVE_BYTES]);   // the block thread, or a test
void hs_hands_view(float *left, float *right);    // recent movement of each actuator, -1 to 1, for the coils on screen
void hs_hands_log_to(const char *path);           // keeps every block, for the gate. Not for use during a real run's block
