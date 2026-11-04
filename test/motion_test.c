// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Calibration with no ritual: a controller with known errors, resting in many directions, must be found out.
#include "motion/quiet_fit.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  simd_float3 offset = simd_make_float3(0.012f, -0.008f, 0.005f), gain = simd_make_float3(1.006f, 0.993f, 1.004f);
  hs_quiet_fit fit;
  hs_quiet_fit_init(&fit);
  srand(7);
  int added = 0;
  const float directions[12][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0.7f, 0.7f, 0}, {-0.7f, 0.7f, 0}, {0, 0.7f, 0.7f}, {0, 0.7f, -0.7f}, {0.7f, 0, 0.7f}, {-0.7f, 0, -0.7f}};
  for (int d = 0; d < 12; d++) {
    simd_float3 g = simd_normalize(simd_make_float3(directions[d][0], directions[d][1], directions[d][2]));
    for (int i = 0; i < 200; i++) {
      simd_float3 noise = simd_make_float3((float)rand() / (float)RAND_MAX - 0.5f, (float)rand() / (float)RAND_MAX - 0.5f, (float)rand() / (float)RAND_MAX - 0.5f) * 0.002f;
      added += hs_quiet_fit_add(&fit, g / gain + offset + noise);
    }
  }
  simd_float3 found_offset, found_gain;
  bool ok = hs_quiet_fit_solve(&fit, &found_offset, &found_gain);
  if (!ok) { printf("FAIL no solution from %d directions\n", added); return 1; }
  float offset_error = simd_length(found_offset - offset), gain_error = simd_length(found_gain - gain);
  printf("offset error %.5f g, gain error %.5f\n", offset_error, gain_error);
  if (offset_error > 0.001f || gain_error > 0.002f) { printf("FAIL\n"); return 1; }
  hs_quiet_fit few;
  hs_quiet_fit_init(&few);
  for (int i = 0; i < 200; i++) hs_quiet_fit_add(&few, simd_make_float3(0, 1, 0));
  if (hs_quiet_fit_solve(&few, &found_offset, &found_gain)) { printf("FAIL solved from one direction\n"); return 1; }
  printf("motion_test: pass\n");
  return 0;
}
