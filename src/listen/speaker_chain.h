// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Prepares sound for the controller's own speaker and encodes it the one way the controller plays cleanly: pure CELT.
// Measured on this speaker: nothing useful below 600 Hz, 6 to 12 dB too much from 4 to 6.3 kHz, clean up to -3 dBFS.
#pragma once
#include "link/protocol.h"
#include "listen/feel.h"
#include "listen/limiter.h"

typedef struct OpusEncoder OpusEncoder;

typedef struct {
  hs_biquad low[2], warmth, top;
  Limiter limiter;
  OpusEncoder *encoder;
  float door, door_wanted;      // how open the output is, so that a start or a stop never clicks
  float peak_view;
} hs_speaker_chain;

bool hs_speaker_init(hs_speaker_chain *chain);
void hs_speaker_destroy(hs_speaker_chain *chain);
void hs_speaker_set_open(hs_speaker_chain *chain, bool open);     // fades over 40 ms, so a start or a stop never clicks

// 960 stereo frames at 45 kHz in, two 63-byte packets out.
bool hs_speaker_process(hs_speaker_chain *chain, const float *stereo, uint8_t out[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES]);

// A packet of true silence from a fresh encoder, sent whenever nothing is queued.
bool hs_speaker_silence_packet(uint8_t out[HS_SPEAKER_PACKET_BYTES]);
