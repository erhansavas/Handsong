// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Calibration from the controller held still in six poses. The accelerometer must read 1 g in every direction, which
// gives each axis its zero and its gain. Then the gyro, integrated from one pose to the next, must carry gravity from
// where it was to where it was next measured, which gives each gyro axis its scale. Kept free of the app so it can be tested.
#pragma once
#include <math.h>
#include <simd/simd.h>
#include <stdbool.h>

typedef struct { simd_float3 offset, gain, gyroScale; } PadCalibration;
static const PadCalibration kPadCalibrationNone = {{0, 0, 0}, {1, 1, 1}, {1, 1, 1}};
// How far the numbers can be off before anything has been measured: 0.01 g, and 1 percent, as the factory's own calibration leaves them.
static const PadCalibration kPadCalibrationUnknown = {{1e-4f, 1e-4f, 1e-4f}, {1e-4f, 1e-4f, 1e-4f}, {1e-4f, 1e-4f, 1e-4f}};
typedef struct { float rate[3], dt; } PadCalSample;        // radians per second, gyro zero already removed
typedef struct { const PadCalSample *samples; int count; simd_float3 from, to; } PadCalMove;

static inline simd_float3 pad_cal_felt(const PadCalibration *c, simd_float3 raw) { return (raw - c->offset) * c->gain; }

// The six poses, in the order they are asked for, each a quarter turn from the last: down on the desk, stood up facing
// you, then round like a steering wheel, and last turned over. What is up in the controller's own frame, and how the
// controller stands in the world for that, with you at +z looking at it.
enum { kPadCalPoses = 6 };
static const simd_float3 kPadCalAim[kPadCalPoses] = {{0, 1, 0}, {0, 0, -1}, {-1, 0, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};

static inline simd_quatf pad_cal_pose(int pose) {
  const float q = (float)M_PI / 2;
  const simd_float3 X = {1, 0, 0}, Z = {0, 0, 1};
  simd_quatf upright = simd_quaternion(q, X);
  switch (pose) {
    case 0: return simd_quaternion(0.0f, X);
    case 1: return upright;
    case 2: return simd_mul(simd_quaternion(-q, Z), upright);
    case 3: return simd_mul(simd_quaternion(-2 * q, Z), upright);
    case 4: return simd_mul(simd_quaternion(q, Z), upright);
    default: return simd_quaternion(2 * q, Z);
  }
}

// How one pose is being held, from the sensor reports as they arrive. A hand always trembles and now and then twitches,
// so a moment of unrest only pauses the count, and only leaving the pose takes it back. Only quiet samples are averaged.
typedef struct {
  simd_float3 slow, rateSlow, feltSum, rateSum;
  float progress;     // 0 to 1
  float closeness;    // 0 far from the pose, 1 in it
  int count;
  bool started, aimed, still;
} PadCalHold;

static const float kPadCalInPose = 0.80f, kPadCalOutOfPose = 0.74f;    // cosines: about 37 degrees in, 42 out

static inline void pad_cal_hold_reset(PadCalHold *h) {
  h->progress = 0; h->count = 0; h->aimed = h->still = false;
  h->feltSum = h->rateSum = simd_make_float3(0, 0, 0);
}

// rate in degrees per second, zero already removed unless it is the desk pose, which learns it. True once held long enough.
static inline bool pad_cal_hold_step(PadCalHold *h, int pose, simd_float3 raw, simd_float3 rate, simd_float3 zero, float dt) {
  const bool desk = pose == 0;          // the gyro's zero comes from here, so this one must truly be left alone
  const float needs = desk ? 3.0f : 1.5f;
  if (!h->started) { h->started = true; h->slow = raw; h->rateSlow = rate; }
  h->slow += (raw - h->slow) * fminf(dt / 0.12f, 1);
  h->rateSlow += (rate - h->rateSlow) * fminf(dt / 0.5f, 1);
  float along = simd_dot(simd_normalize(raw), kPadCalAim[pose]);
  h->closeness = fminf(1, fmaxf(0, (along - 0.30f) / (kPadCalInPose - 0.30f)));
  h->aimed = along > (h->aimed ? kPadCalOutOfPose : kPadCalInPose);
  float turning = desk ? simd_length(rate - h->rateSlow) : simd_length(rate - zero), shaken = simd_length(raw - h->slow);
  h->still = turning < (desk ? 3.0f : 30.0f) && shaken < (desk ? 0.03f : 0.09f);
  if (!h->aimed) {
    h->progress = fmaxf(0, h->progress - dt / 0.5f);
    if (h->progress == 0) { h->count = 0; h->feltSum = h->rateSum = simd_make_float3(0, 0, 0); }
    return false;
  }
  if (!h->still) return false;
  // Settled somewhere else within the pose: what was gathered no longer describes this reading.
  if (h->count > 0 && simd_length(raw - h->feltSum / (float)h->count) > 0.16f) { h->progress = 0; h->count = 0; h->feltSum = h->rateSum = simd_make_float3(0, 0, 0); }
  h->feltSum += raw; h->rateSum += rate; h->count++;
  h->progress = fminf(1, h->progress + dt / needs);
  return h->progress >= 1;
}

static inline bool pad_cal_solve(double a[6][7], double out[6]) {
  for (int i = 0; i < 6; i++) {
    int best = i;
    for (int r = i + 1; r < 6; r++) if (fabs(a[r][i]) > fabs(a[best][i])) best = r;
    if (fabs(a[best][i]) < 1e-7) return false;
    for (int c = 0; c < 7; c++) { double t = a[i][c]; a[i][c] = a[best][c]; a[best][c] = t; }
    for (int r = 0; r < 6; r++) {
      if (r == i) continue;
      double k = a[r][i] / a[i][i];
      for (int c = i; c < 7; c++) a[r][c] -= k * a[i][c];
    }
  }
  for (int i = 0; i < 6; i++) out[i] = a[i][6] / a[i][i];
  return true;
}

// One calibration in a hand is a noisy reading of numbers that hardly ever change. A 90 degree turn that ends 2.5 degrees
// off says nothing finer than 3 percent about that axis. So each reading is weighed against all that came before by how far
// each can be trusted, and the result settles from launch to launch instead of wandering. A little doubt always remains, so a
// sensor that really does change is still followed.
static inline void pad_cal_weigh(simd_float3 *mean, simd_float3 *variance, simd_float3 reading, simd_float3 noiseVariance) {
  simd_float3 k = *variance / (*variance + noiseVariance);
  *mean += (reading - *mean) * k;
  *variance = simd_max(*variance * (1 - k), simd_make_float3(2e-6f, 2e-6f, 2e-6f));
}

// Six still readings, well spread. False when they do not make a believable sensor.
static inline bool pad_cal_accel(const simd_float3 m[6], simd_float3 *offset, simd_float3 *gain) {
  double a[6][7], p[6];
  for (int i = 0; i < 6; i++) {
    double x = m[i].x, y = m[i].y, z = m[i].z;
    a[i][0] = x * x; a[i][1] = y * y; a[i][2] = z * z; a[i][3] = x; a[i][4] = y; a[i][5] = z; a[i][6] = 1;
  }
  if (!pad_cal_solve(a, p) || p[0] <= 0 || p[1] <= 0 || p[2] <= 0) return false;
  double o[3] = {-p[3] / (2 * p[0]), -p[4] / (2 * p[1]), -p[5] / (2 * p[2])};
  double r = 1 + p[0] * o[0] * o[0] + p[1] * o[1] * o[1] + p[2] * o[2] * o[2];
  if (r <= 0) return false;
  simd_float3 off = simd_make_float3((float)o[0], (float)o[1], (float)o[2]);
  simd_float3 g = simd_make_float3((float)sqrt(p[0] / r), (float)sqrt(p[1] / r), (float)sqrt(p[2] / r));
  if (simd_length(off) > 0.12f || simd_reduce_min(g) < 0.93f || simd_reduce_max(g) > 1.07f) return false;
  *offset = off; *gain = g;
  return true;
}

static inline double pad_cal_error(const PadCalMove *moves, int count, simd_float3 scale) {
  double sum = 0;
  for (int i = 0; i < count; i++) {
    simd_float3 g = simd_normalize(moves[i].from);
    for (int s = 0; s < moves[i].count; s++) {
      const PadCalSample *q = &moves[i].samples[s];
      simd_float3 w = simd_make_float3(q->rate[0], q->rate[1], q->rate[2]) * scale;
      g = simd_normalize(g + simd_cross(g, w) * q->dt);    // gravity stands still, so in the shell's own frame it turns the other way
    }
    sum += simd_length_squared(g - simd_normalize(moves[i].to));
  }
  return sum;
}

// Gauss-Newton on the three scales. False if it would not be an improvement, or would be unbelievable.
// noise: how far each scale found here can be trusted, as a variance, from what is left unexplained and how much that axis turned.
static inline bool pad_cal_gyro(const PadCalMove *moves, int count, simd_float3 *scale, double *before, double *after, simd_float3 *noise) {
  simd_float3 s = simd_make_float3(1, 1, 1);
  *before = *after = pad_cal_error(moves, count, s);
  for (int iteration = 0; iteration < 24; iteration++) {
    double base = pad_cal_error(moves, count, s), grad[3], hess[3];
    for (int a = 0; a < 3; a++) {
      const float h = 0.004f;
      simd_float3 up = s, down = s;
      up[a] += h; down[a] -= h;
      double eu = pad_cal_error(moves, count, up), ed = pad_cal_error(moves, count, down);
      grad[a] = (eu - ed) / (2 * h);
      hess[a] = (eu - 2 * base + ed) / (h * h);
    }
    bool better = false;
    for (float step = 0.8f; step > 0.01f && !better; step *= 0.5f) {     // the axes pull on each other, so a full step can overshoot
      simd_float3 next = s;
      for (int a = 0; a < 3; a++) if (hess[a] > 1e-6) next[a] -= (float)(step * grad[a] / hess[a]);
      next = simd_clamp(next, simd_make_float3(0.9f, 0.9f, 0.9f), simd_make_float3(1.1f, 1.1f, 1.1f));
      if (pad_cal_error(moves, count, next) < base) { s = next; better = true; }
    }
    if (!better) break;
  }
  *after = pad_cal_error(moves, count, s);
  if (*after >= *before || simd_reduce_min(s) <= 0.9001f || simd_reduce_max(s) >= 1.0999f) return false;
  *scale = s;
  // Each move ends on the unit sphere, so two numbers each, less the three being fitted. A floor on what is left over: however
  // well it happened to fit, a hand cannot show gravity's direction much better than about half a degree.
  double left = fmax(*after, count * 1e-4) / fmax(2 * count - 3, 1);
  for (int a = 0; a < 3; a++) {
    simd_float3 up = s, down = s;
    const float h = 0.004f;
    up[a] += h; down[a] -= h;
    double curve = (pad_cal_error(moves, count, up) - 2 * *after + pad_cal_error(moves, count, down)) / (h * h);
    (*noise)[a] = (float)(curve > 1e-6 ? 2 * left / curve : 1.0);
  }
  return true;
}
