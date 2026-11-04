// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// A stereo windowed-sinc resampler with no allocation, safe on a realtime thread. The ratio may be nudged while running,
// which is how the capture's clock is followed without ever jumping.
#pragma once
#include <math.h>
#include <string.h>

enum { HS_RESAMPLE_TAPS = 32, HS_RESAMPLE_PHASES = 128 };

typedef struct {
  float table[HS_RESAMPLE_PHASES + 1][HS_RESAMPLE_TAPS];
  float history[HS_RESAMPLE_TAPS * 2];   // the newest samples, stereo
  double phase;                          // 0 to 1, where the next output lies between the last two inputs
  double step;                           // input samples per output sample
} hs_resampler;

static inline double hs_resampler_bessel(double x) {
  double sum = 1, term = 1;
  for (int k = 1; k < 24; k++) {
    term *= (x / (2 * k)) * (x / (2 * k));
    sum += term;
  }
  return sum;
}

static inline void hs_resampler_init(hs_resampler *r, double in_rate, double out_rate) {
  memset(r, 0, sizeof *r);
  r->step = in_rate / out_rate;
  const double cutoff = 0.96 * fmin(1.0, out_rate / in_rate), beta = 9.0, denominator = hs_resampler_bessel(beta);
  for (int p = 0; p <= HS_RESAMPLE_PHASES; p++) {
    double fraction = (double)p / HS_RESAMPLE_PHASES, sum = 0;
    for (int t = 0; t < HS_RESAMPLE_TAPS; t++) {
      double x = t - (HS_RESAMPLE_TAPS / 2 - 1) - fraction;
      double sinc = fabs(x) < 1e-9 ? 1 : sin(M_PI * cutoff * x) / (M_PI * cutoff * x);
      double u = x / (HS_RESAMPLE_TAPS / 2.0), window = fabs(u) >= 1 ? 0 : hs_resampler_bessel(beta * sqrt(1 - u * u)) / denominator;
      r->table[p][t] = (float)(sinc * window);
      sum += sinc * window;
    }
    for (int t = 0; t < HS_RESAMPLE_TAPS; t++) r->table[p][t] = (float)(r->table[p][t] / sum);
  }
}

static inline void hs_resampler_push(hs_resampler *r, float left, float right) {
  memmove(r->history, r->history + 2, sizeof r->history - 2 * sizeof(float));
  r->history[HS_RESAMPLE_TAPS * 2 - 2] = left;
  r->history[HS_RESAMPLE_TAPS * 2 - 1] = right;
}

static inline void hs_resampler_value(const hs_resampler *r, float *left, float *right) {
  double scaled = r->phase * HS_RESAMPLE_PHASES;
  int p = (int)scaled;
  if (p >= HS_RESAMPLE_PHASES) p = HS_RESAMPLE_PHASES - 1;
  float mix = (float)(scaled - p), l = 0, rr = 0;
  const float *a = r->table[p], *b = r->table[p + 1];
  for (int t = 0; t < HS_RESAMPLE_TAPS; t++) {
    float weight = a[t] + (b[t] - a[t]) * mix;
    l += weight * r->history[2 * t];
    rr += weight * r->history[2 * t + 1];
  }
  *left = l;
  *right = rr;
}

// How many input frames to supply before the next output frame.
static inline int hs_resampler_advance(hs_resampler *r) {
  r->phase += r->step;
  int whole = (int)r->phase;
  r->phase -= whole;
  return whole;
}
