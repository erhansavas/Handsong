// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// From how loud the microphone is to how much it should move anything. A room is never still, and the Mac's own speakers are in it:
// neither the room's wandering, nor a click, nor the music coming out of this very Mac may look like a breath.
#pragma once
#include <stdbool.h>

typedef struct {
  float recent[5];
  float quiet, leak, out;
  int seen;
  bool leak_known;
} hs_mic_gate;

// One buffer of about 43 ms: the microphone, and what the Mac is playing right now, both in dB. Gives 0 to 1. A zeroed gate is one just opened.
float hs_mic_gate_step(hs_mic_gate *gate, float microphone_db, float playing_db);
