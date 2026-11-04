// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The speaker chain offline: raw float stereo at 48 kHz in, what the driver would be sent out, and what survives the codec.
// Usage: speaker_tool in.raw before_codec.raw after_codec.raw
#include "listen/resampler.h"
#include "listen/speaker_chain.h"
#include "opus.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 4) return 64;
  FILE *in = fopen(argv[1], "rb"), *before = fopen(argv[2], "wb"), *after = fopen(argv[3], "wb");
  if (!in || !before || !after) return 66;
  static hs_speaker_chain chain;
  static hs_resampler resampler;
  if (!hs_speaker_init(&chain)) return 70;
  hs_speaker_set_open(&chain, true);
  hs_resampler_init(&resampler, 48000, HS_SPEAKER_CLOCK);
  int error = 0;
  OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
  if (!decoder) return 70;
  static float block[HS_FEEL_FRAMES * 2];
  float frame[2];
  uint8_t packets[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES];
  bool done = false;
  while (!done) {
    for (int i = 0; i < HS_FEEL_FRAMES && !done; i++) {
      hs_resampler_value(&resampler, &block[2 * i], &block[2 * i + 1]);
      for (int need = hs_resampler_advance(&resampler); need > 0; need--) {
        if (fread(frame, sizeof frame, 1, in) != 1) { done = true; break; }
        hs_resampler_push(&resampler, frame[0], frame[1]);
      }
    }
    if (done) break;
    if (!hs_speaker_process(&chain, block, packets)) return 71;
    float peak = chain.peak_view;
    fwrite(&peak, sizeof peak, 1, before);
    for (int p = 0; p < HS_SPEAKER_PACKETS; p++) {
      float decoded[HS_SPEAKER_PACKET_FRAMES];
      if (opus_decode_float(decoder, packets[p], HS_SPEAKER_PACKET_BYTES, decoded, HS_SPEAKER_PACKET_FRAMES, 0) != HS_SPEAKER_PACKET_FRAMES) return 72;
      fwrite(decoded, sizeof(float), HS_SPEAKER_PACKET_FRAMES, after);
    }
  }
  return 0;
}
