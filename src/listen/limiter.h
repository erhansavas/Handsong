// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// A look-ahead peak limiter. Every sample is held back a little; the level comes down before a loud one plays and rises again slowly. It
// only ever scales the signal, so it adds nothing to the sound. All memory is inside the struct, so it can run on a realtime thread.
#pragma once
#include <math.h>
#include <string.h>

enum { kLimiterLongest = 128 };

typedef struct {
  float top, rise_rate, level;
  int span, cursor;
  // Oldest to lowest: the samples still in view that could set the level, so the lowest one in view is found at once.
  int queue[kLimiterLongest], queue_first, queue_size;
  float demand[kLimiterLongest];
  // What the level was over the span; their mean is what plays, so a fall reaches its full depth just as the peak comes out.
  float levels[kLimiterLongest];
  double levels_sum;
  float delayed[kLimiterLongest][2];
} Limiter;

static inline void limiter_init(Limiter *limiter, double rate, double look_ms, double rise_ms, float top) {
  memset(limiter, 0, sizeof *limiter);
  long span = lround(rate * look_ms / 1000);
  limiter->span = span < 2 ? 2 : span > kLimiterLongest ? kLimiterLongest : (int)span;
  limiter->top = top;
  limiter->rise_rate = (float)(1 - exp(-1000.0 / (rise_ms * rate)));
  limiter->level = 1;
  for (int i = 0; i < limiter->span; i++) limiter->levels[i] = 1;
  limiter->levels_sum = limiter->span;
}

// A stereo pair in; the pair from one span ago, scaled, out.
static inline void limiter_run(Limiter *limiter, float *left, float *right) {
  float l = fabsf(*left), r = fabsf(*right), biggest = l > r ? l : r;
  float asked = biggest > limiter->top ? limiter->top / biggest : 1.0f;
  int span = limiter->span, now = limiter->cursor;
  limiter->demand[now] = asked;
  // Whatever asks for no less than this one, and is older, can never matter again.
  while (limiter->queue_size > 0) {
    int newest = (limiter->queue_first + limiter->queue_size - 1) % span;
    if (limiter->demand[limiter->queue[newest]] < asked) break;
    limiter->queue_size--;
  }
  limiter->queue[(limiter->queue_first + limiter->queue_size) % span] = now;
  limiter->queue_size++;
  // The one that fell out of view this time.
  if (limiter->queue_size > 0 && limiter->queue[limiter->queue_first] == (now + 1) % span) {
    limiter->queue_first = (limiter->queue_first + 1) % span;
    limiter->queue_size--;
    if (limiter->queue_size == 0) {
      limiter->queue[limiter->queue_first] = now;
      limiter->queue_size = 1;
    }
  }
  float lowest = limiter->demand[limiter->queue[limiter->queue_first]];
  limiter->level = lowest < limiter->level ? lowest : limiter->level + (lowest - limiter->level) * limiter->rise_rate;
  limiter->levels_sum += (double)limiter->level - (double)limiter->levels[now];
  limiter->levels[now] = limiter->level;
  int oldest = (now + 1) % span;
  float scale = (float)(limiter->levels_sum / span);
  float out_left = limiter->delayed[oldest][0] * scale, out_right = limiter->delayed[oldest][1] * scale;
  limiter->delayed[now][0] = *left;
  limiter->delayed[now][1] = *right;
  limiter->cursor = oldest;
  if (oldest == 0) {      // once per span the sum is taken afresh, so that rounding cannot grow in it
    limiter->levels_sum = 0;
    for (int i = 0; i < span; i++) limiter->levels_sum += limiter->levels[i];
  }
  *left = out_left;
  *right = out_right;
}
