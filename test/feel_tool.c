// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Runs raw float stereo at 48 kHz through the same chain as the app. Usage: feel_tool in.raw hands.raw view.txt
#include "listen/feel.c"
#include "listen/resampler.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 4) return 64;
  FILE *in = fopen(argv[1], "rb"), *hands = fopen(argv[2], "wb"), *view = fopen(argv[3], "w");
  if (!in || !hands || !view) return 66;
  static hs_feel feel;
  static hs_resampler resampler;
  hs_feel_init(&feel);
  hs_resampler_init(&resampler, 48000, HS_FEEL_RATE);
  float block[HS_FEEL_FRAMES * 2], frame[2];
  int8_t out[HS_WAVE_BYTES];
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
    hs_feel_process(&feel, block, out);
    fwrite(out, 1, sizeof out, hands);
    uint8_t big, small;
    hs_feel_rumble_bytes(&feel, &big, &small);
    fprintf(view, "%.4f %.4f %.4f %.4f %.4f %.3f %d %d\n", feel.view.body_peak, feel.view.hit, feel.view.left, feel.view.right, feel.view.speech, feel.view.gain, big, small);
  }
  fclose(in); fclose(hands); fclose(view);
  return 0;
}
