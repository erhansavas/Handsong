// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The trigger's wall, seen coming. The hands answer 110 ms after they are told, so the blow must leave before the pedal arrives. From how
// fast the finger comes and how much it is already slowing, this says whether it will really get there within that time. A finger
// slowing down short of the wall gets no blow: a false hit in the hand is worse than a late one.
#pragma once
#include <math.h>
#include <stdbool.h>

typedef struct { float speed, slowing, last; } hs_wall_predictor;

static inline bool hs_wall_will_hit(hs_wall_predictor *p, float value, float dt, float wall, float lead) {
  float rate = (value - p->last) / dt;
  float change = (rate - p->speed) / dt;
  p->last = value;
  p->speed += (rate - p->speed) * 0.6f;
  p->slowing += (change - p->slowing) * 0.35f;
  float need = wall - value;
  // Not before most of the travel is done, later the slower it comes: a pull that stops short is the one thing that cannot be told
  // apart any sooner.
  if (p->speed <= 0.5f || need <= 0 || value < 0.85f - 0.04f * p->speed) return false;
  float a = p->slowing < -1.0f ? p->slowing : 0;
  float root = p->speed * p->speed + 2 * a * need;
  if (root <= 0) return false;
  float seconds = a < 0 ? (p->speed - sqrtf(root)) / -a : need / p->speed;
  return seconds <= lead;
}
