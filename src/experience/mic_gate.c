// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "experience/mic_gate.h"
#include <math.h>
#include <stdbool.h>

static float median_of(const float *values, int count) {
  float sorted[5];
  for (int i = 0; i < count; i++) {
    int at = i;
    while (at > 0 && sorted[at - 1] > values[i]) { sorted[at] = sorted[at - 1]; at--; }
    sorted[at] = values[i];
  }
  return sorted[count / 2];
}

static float power_sum(float a_db, float b_db) { return 10 * log10f(powf(10, a_db / 10) + powf(10, b_db / 10)); }
static float toward(float value, float target, float step) { return value + fmaxf(-step, fminf(step, target - value)); }

float hs_mic_gate_step(hs_mic_gate *gate, float microphone_db, float playing_db) {
  // Its first two buffers are the microphone's own start-up thump. For the next half second it listens to the room and moves nothing.
  enum { kThump = 2, kSettled = 14 };
  // Measured here: the room alone stays within 12 dB of its own middle for 99 buffers in 100, and the modelled music within 9.
  const float kOverRoom = 14, kOverMusic = 12, kSpan = 20, kMostLeak = 12;
  gate->seen += gate->seen < kSettled;
  if (gate->seen <= kThump) { gate->out = playing_db; return 0; }
  // A breath or a voice lasts; a click, a knock or a single hit in the music does not. The middle of the last five buffers (about 200 ms) only moves for what lasts.
  int have = gate->seen - kThump < 5 ? gate->seen - kThump : 5;
  for (int i = 4; i > 0; i--) gate->recent[i] = gate->recent[i - 1];
  gate->recent[0] = microphone_db;
  float heard = median_of(gate->recent, have);
  // What is playing reaches the microphone a moment later and rings in the room: rises at once, falls over half a second.
  float was_out = gate->out;
  gate->out = fmaxf(playing_db, gate->out - 0.6f);
  if (gate->seen < kSettled) {
    gate->quiet = gate->seen == kThump + 1 ? heard : gate->quiet + (heard - gate->quiet) * 0.25f;
    return 0;
  }
  if (!gate->leak_known) {       // from what was just heard: on the safe side, and it comes down
    gate->leak_known = true;
    gate->leak = gate->out > -70 ? fminf(kMostLeak, heard - gate->out) : -40.0f;
  }
  float from_mac = gate->out + gate->leak;
  float expected = power_sum(gate->quiet, from_mac);
  float over = heard - expected;
  // Learned only from what is not a breath. If it stays over for long, the room or the volume has really changed, and that is followed slowly.
  bool mac_is_louder = from_mac > gate->quiet - 3;
  float starts = mac_is_louder ? kOverMusic : kOverRoom;
  if (gate->out > was_out + 1.5f && over > 0) gate->leak = fminf(kMostLeak, gate->leak + 0.2f);      // the music rose and the microphone with it: that was the music
  else if (over < starts) {
    if (mac_is_louder) gate->leak = fminf(kMostLeak, toward(gate->leak, heard - gate->out, 0.1f));
    else gate->quiet = toward(gate->quiet, heard, 0.1f);
  } else {
    if (mac_is_louder) gate->leak = fminf(kMostLeak, gate->leak + 0.01f);
    else gate->quiet += 0.01f;
  }
  return fmaxf(0.0f, fminf(1.0f, (over - starts) / kSpan));
}
