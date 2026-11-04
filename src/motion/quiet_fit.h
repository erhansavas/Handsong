// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Calibration with no ritual. Whenever the controller rests, the accelerometer reads gravity, which must measure exactly 1 g
// in every direction. Enough quiet moments, in enough different directions, give each axis its zero and its gain.
#pragma once
#include <math.h>
#include <simd/simd.h>
#include <stdbool.h>
#include <string.h>

enum { HS_FIT_BINS = 26, HS_FIT_MIN_BINS = 8 };

typedef struct {
  simd_float3 sum[HS_FIT_BINS];
  float count[HS_FIT_BINS];
  simd_float3 last;
} hs_quiet_fit;

static inline void hs_quiet_fit_init(hs_quiet_fit *f) { memset(f, 0, sizeof *f); }

// Which of the 26 directions, faces, edges and corners of a cube, this reading falls in.
static inline int hs_quiet_bin(simd_float3 d) {
  int x = d.x > 0.5f ? 1 : d.x < -0.5f ? -1 : 0, y = d.y > 0.5f ? 1 : d.y < -0.5f ? -1 : 0, z = d.z > 0.5f ? 1 : d.z < -0.5f ? -1 : 0;
  int index = (x + 1) * 9 + (y + 1) * 3 + (z + 1);
  return index > 13 ? index - 1 : index == 13 ? -1 : index;
}

// Returns true when a new direction has just reached enough samples, so a new solve is worthwhile.
static inline bool hs_quiet_fit_add(hs_quiet_fit *f, simd_float3 reading) {
  int bin = hs_quiet_bin(reading);
  if (bin < 0) return false;
  f->sum[bin] += reading;
  f->count[bin] += 1;
  return f->count[bin] == 120;   // about a second at the narrow link's report rate
}

// Fits an axis-aligned ellipsoid through the mean of each direction, by Gauss-Newton on (|gain * (r - offset)| - 1).
// Keeps the answer only if it is believable: small corrections, and it really fits.
static inline bool hs_quiet_fit_solve(const hs_quiet_fit *f, simd_float3 *offset_out, simd_float3 *gain_out) {
  simd_float3 points[HS_FIT_BINS];
  int n = 0;
  for (int i = 0; i < HS_FIT_BINS; i++) if (f->count[i] >= 120) points[n++] = f->sum[i] / f->count[i];
  if (n < HS_FIT_MIN_BINS) return false;
  double p[6] = {0, 0, 0, 1, 1, 1};
  for (int iteration = 0; iteration < 12; iteration++) {
    double normal[6][7];
    memset(normal, 0, sizeof normal);
    for (int k = 0; k < n; k++) {
      double v[3], size = 0;
      for (int a = 0; a < 3; a++) {
        v[a] = (points[k][a] - p[a]) * p[a + 3];
        size += v[a] * v[a];
      }
      size = sqrt(size);
      double row[6];
      for (int a = 0; a < 3; a++) {
        row[a] = -p[a + 3] * v[a] / size;
        row[a + 3] = (points[k][a] - p[a]) * v[a] / size;
      }
      double residual = size - 1;
      for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) normal[i][j] += row[i] * row[j];
        normal[i][6] -= row[i] * residual;
      }
    }
    for (int i = 0; i < 6; i++) normal[i][i] += 1e-6;
    for (int i = 0; i < 6; i++) {
      int pivot = i;
      for (int r = i + 1; r < 6; r++) if (fabs(normal[r][i]) > fabs(normal[pivot][i])) pivot = r;
      if (fabs(normal[pivot][i]) < 1e-12) return false;
      if (pivot != i) for (int c = 0; c < 7; c++) { double t = normal[i][c]; normal[i][c] = normal[pivot][c]; normal[pivot][c] = t; }
      for (int r = 0; r < 6; r++) {
        if (r == i) continue;
        double factor = normal[r][i] / normal[i][i];
        for (int c = i; c < 7; c++) normal[r][c] -= factor * normal[i][c];
      }
    }
    for (int i = 0; i < 6; i++) p[i] += normal[i][6] / normal[i][i];
  }
  double worst = 0;
  for (int k = 0; k < n; k++) {
    double size = 0;
    for (int a = 0; a < 3; a++) { double v = (points[k][a] - p[a]) * p[a + 3]; size += v * v; }
    worst = fmax(worst, fabs(sqrt(size) - 1));
  }
  for (int a = 0; a < 3; a++) if (fabs(p[a]) > 0.05 || fabs(p[a + 3] - 1) > 0.05) return false;
  if (worst > 0.01) return false;
  *offset_out = simd_make_float3((float)p[0], (float)p[1], (float)p[2]);
  *gain_out = simd_make_float3((float)p[3], (float)p[4], (float)p[5]);
  return true;
}
