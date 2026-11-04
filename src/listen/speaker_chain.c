// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "listen/speaker_chain.h"
#include "opus.h"
#include <math.h>
#include <string.h>

static const double kRate = HS_SPEAKER_CLOCK;
// The driver stays linear to -3 dBFS, and the codec can overshoot dense material by over a decibel, so this leaves room.
static const float kCeiling = 0.447f;       // -7 dBFS: well inside where this driver stays clean
static const float kBassHeadroom = 1.41f;   // what removing the bass it cannot play gives back

// Pure CELT, the one form this controller plays cleanly: the restricted low-delay application, at a constant 50.4 kbit/s, which is
// exactly 63 bytes for each packet of 480 samples at the rate the encoder is told, and the lightest search it offers.
enum { kCeltBitsPerSecond = 50400, kCeltCodingRate = 48000 };

static OpusEncoder *open_celt(void) {
  int status = OPUS_INTERNAL_ERROR;
  OpusEncoder *celt = opus_encoder_create(kCeltCodingRate, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &status);
  if (status != OPUS_OK) return NULL;
  opus_encoder_ctl(celt, OPUS_SET_COMPLEXITY(0));
  opus_encoder_ctl(celt, OPUS_SET_VBR(0));
  opus_encoder_ctl(celt, OPUS_SET_BITRATE(kCeltBitsPerSecond));
  return celt;
}

bool hs_speaker_init(hs_speaker_chain *chain) {
  memset(chain, 0, sizeof *chain);
  // Measured on this speaker: nothing useful low down, so that is cut hard, as a fourth-order Butterworth in two stages. A little warmth
  // just above where it starts to speak, and its hot top turned down.
  chain->low[0] = hs_filter_design(HS_FILTER_HIGH_PASS, kRate, 300, 0.5412, 0);
  chain->low[1] = hs_filter_design(HS_FILTER_HIGH_PASS, kRate, 300, 1.3066, 0);
  chain->warmth = hs_filter_design(HS_FILTER_BUMP, kRate, 520, 1.0, 3.0);
  chain->top = hs_filter_design(HS_FILTER_TOP_SHELF, kRate, 4200, 1.0, -6.0);
  limiter_init(&chain->limiter, kRate, 2.0, 140.0, kCeiling);
  chain->encoder = open_celt();
  return chain->encoder != NULL;
}

void hs_speaker_destroy(hs_speaker_chain *chain) {
  if (chain->encoder) opus_encoder_destroy(chain->encoder);
  chain->encoder = NULL;
}

void hs_speaker_set_open(hs_speaker_chain *chain, bool open) { chain->door_wanted = open ? 1 : 0; }

// One sample through everything before the codec: one channel, the driver's own curve, the ceiling, and the door that opens over 40 ms.
static inline float prepare_sample(hs_speaker_chain *chain, float left, float right) {
  static const float kDoorStep = 1.0f / 1800;
  float gap = chain->door_wanted - chain->door;
  chain->door += gap > kDoorStep ? kDoorStep : gap < -kDoorStep ? -kDoorStep : gap;
  float sample = (left + right) * 0.5f * kBassHeadroom;
  for (int stage = 0; stage < 2; stage++) sample = hs_filter_run(&chain->low[stage], sample);
  sample = hs_filter_run(&chain->warmth, sample);
  sample = hs_filter_run(&chain->top, sample);
  float copy = sample;
  limiter_run(&chain->limiter, &sample, &copy);
  return sample * chain->door;
}

bool hs_speaker_process(hs_speaker_chain *chain, const float *stereo, uint8_t out[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES]) {
  int16_t both_sides[HS_SPEAKER_PACKET_FRAMES * 2];
  chain->peak_view = 0;
  for (int packet = 0; packet < HS_SPEAKER_PACKETS; packet++) {
    const float *in = stereo + packet * HS_SPEAKER_PACKET_FRAMES * 2;
    for (int n = 0; n < HS_SPEAKER_PACKET_FRAMES; n++) {
      float sample = prepare_sample(chain, in[2 * n], in[2 * n + 1]);
      chain->peak_view = fmaxf(chain->peak_view, fabsf(sample));
      sample = sample > 1 ? 1 : sample < -1 ? -1 : sample;
      both_sides[2 * n] = both_sides[2 * n + 1] = (int16_t)lrintf(sample * 32767);
    }
    if (opus_encode(chain->encoder, both_sides, HS_SPEAKER_PACKET_FRAMES, out[packet], HS_SPEAKER_PACKET_BYTES) != HS_SPEAKER_PACKET_BYTES) return false;
  }
  return true;
}

bool hs_speaker_silence_packet(uint8_t out[HS_SPEAKER_PACKET_BYTES]) {
  OpusEncoder *encoder = open_celt();
  if (!encoder) return false;
  static const int16_t silence[HS_SPEAKER_PACKET_FRAMES * 2] = {0};
  bool ok = opus_encode(encoder, silence, HS_SPEAKER_PACKET_FRAMES, out, HS_SPEAKER_PACKET_BYTES) == HS_SPEAKER_PACKET_BYTES;
  opus_encoder_destroy(encoder);
  return ok;
}
