// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Second-order filters, from Robert Bristow-Johnson's published formulas. One design and one way of running them for everything that listens.
#pragma once
#include <math.h>

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } hs_biquad;

typedef enum { HS_FILTER_LOW_PASS, HS_FILTER_HIGH_PASS, HS_FILTER_BUMP, HS_FILTER_TOP_SHELF } hs_filter_kind;

static inline hs_biquad hs_filter_design(hs_filter_kind kind, double rate, double hertz, double q, double db) {
  double level = pow(10, db / 40), angle = 2 * M_PI * hertz / rate, co = cos(angle), si = sin(angle), width = si / (2 * q);
  double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
  switch (kind) {
    case HS_FILTER_LOW_PASS:
      b0 = b2 = (1 - co) / 2;
      b1 = 1 - co;
      a0 = 1 + width;
      a1 = -2 * co;
      a2 = 1 - width;
      break;
    case HS_FILTER_HIGH_PASS:
      b0 = b2 = (1 + co) / 2;
      b1 = -(1 + co);
      a0 = 1 + width;
      a1 = -2 * co;
      a2 = 1 - width;
      break;
    case HS_FILTER_BUMP:
      b0 = 1 + width * level;
      b1 = a1 = -2 * co;
      b2 = 1 - width * level;
      a0 = 1 + width / level;
      a2 = 1 - width / level;
      break;
    case HS_FILTER_TOP_SHELF: {
      double slope = sqrt(2.0 * level) * si, up = level + 1, down = level - 1;
      b0 = level * (up + down * co + slope);
      b1 = -2 * level * (down + up * co);
      b2 = level * (up + down * co - slope);
      a0 = up - down * co + slope;
      a1 = 2 * (down - up * co);
      a2 = up - down * co - slope;
      break;
    }
  }
  return (hs_biquad){(float)(b0 / a0), (float)(b1 / a0), (float)(b2 / a0), (float)(a1 / a0), (float)(a2 / a0), 0, 0};
}

static inline float hs_filter_run(hs_biquad *f, float in) {
  float out = in * f->b0 + f->z1;
  f->z1 = in * f->b1 - out * f->a1 + f->z2;
  f->z2 = in * f->b2 - out * f->a2;
  return out;
}
