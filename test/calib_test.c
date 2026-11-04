// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Feeds the calibration a controller with known errors, seen through six poses and the moves between them.
#include "motion/calibration.h"
#include <stdio.h>
#include <stdlib.h>
static double gauss(void) { double a = (rand() + 1.0) / (RAND_MAX + 2.0), b = rand() / (double)RAND_MAX; return sqrt(-2 * log(a)) * cos(2 * M_PI * b); }
int main(void) {
  int failures = 0, trials = 400;
  const simd_float3 *poses = kPadCalAim;
  double worstOffset = 0, worstGain = 0, worstScale = 0, worstAfter = 0;
  for (int t = 0; t < trials; t++) {
    srand(t + 1);
    simd_float3 off = {0.02f * (float)gauss(), 0.02f * (float)gauss(), 0.02f * (float)gauss()};
    simd_float3 gain = {1 + 0.012f * (float)gauss(), 1 + 0.012f * (float)gauss(), 1 + 0.012f * (float)gauss()};
    simd_float3 scale = {1 + 0.02f * (float)gauss(), 1 + 0.02f * (float)gauss(), 1 + 0.02f * (float)gauss()};
    simd_float3 truth[6], seen[6];
    for (int i = 0; i < 6; i++) {
      // Held by hand: anywhere inside what the screen accepts as the pose, with what is left of a hand's unrest after averaging.
      float offAxis = acosf(kPadCalInPose) * sqrtf((float)(rand() / (double)RAND_MAX)), around = 6.2832f * (float)(rand() / (double)RAND_MAX);
      simd_float3 a = fabsf(poses[i].x) > 0.5f ? simd_make_float3(0, 1, 0) : simd_make_float3(1, 0, 0), b = simd_cross(poses[i], a);
      truth[i] = simd_normalize(poses[i] * cosf(offAxis) + (a * cosf(around) + b * sinf(around)) * sinf(offAxis));
      simd_float3 noise = {0.002f * (float)gauss(), 0.002f * (float)gauss(), 0.002f * (float)gauss()};
      seen[i] = truth[i] / gain + off + noise;
    }
    simd_float3 foundOff, foundGain;
    if (!pad_cal_accel(seen, &foundOff, &foundGain)) { failures++; continue; }
    // With six readings and six unknowns the fit is exact: what matters is that every direction reads 1 g afterwards.
    for (int i = 0; i < 6; i++) {
      double e = fabs(simd_length((seen[i] - foundOff) * foundGain) - 1);
      if (e > worstOffset) worstOffset = e;
    }
    simd_float3 trial = simd_normalize(simd_make_float3(0.6f, 0.5f, -0.62f)), read = trial / gain + off;
    double e = fabs(simd_length((read - foundOff) * foundGain) - 1);
    if (e > worstGain) worstGain = e;
    // Moves: turn gravity from one pose to the next by the shortest way, at about 90 degrees a second, 132 samples a second.
    static PadCalSample store[5][400];
    PadCalMove moves[5];
    for (int m = 0; m < 5; m++) {
      simd_float3 a = truth[m], b = truth[m + 1];
      simd_float3 axis = simd_normalize(simd_cross(a, b));
      float angle = acosf(simd_clamp(simd_dot(a, b), -1.0f, 1.0f));
      int n = (int)(angle / (M_PI / 2) * 132) + 2;
      float dt = 1.0f / 132, speed = angle / (n * dt);
      simd_float3 g = a;
      // dg/dt = g x w, so a rotation of g about the axis needs w along minus the axis.
      simd_float3 w = -axis * speed;
      for (int s = 0; s < n; s++) {
        simd_float3 measured = w / scale + simd_make_float3(0.002f * (float)gauss(), 0.002f * (float)gauss(), 0.002f * (float)gauss());
        store[m][s] = (PadCalSample){{measured.x, measured.y, measured.z}, dt};
        g = simd_normalize(g + simd_cross(g, w) * dt);
      }
      moves[m] = (PadCalMove){store[m], n, a, g};
    }
    simd_float3 found;
    double before, after;
    simd_float3 noise;
    if (!pad_cal_gyro(moves, 5, &found, &before, &after, &noise)) {
      if (simd_reduce_max(simd_abs(scale - 1)) > 0.002f) failures++;
      continue;
    }
    if (after > worstAfter) worstAfter = after;
    double se = simd_reduce_max(simd_abs(found - scale));
    if (se > worstScale) worstScale = se;
    if (se > 0.006) failures++;
  }
  printf("%d trials, %d failures; 1 g error at poses %.5f, elsewhere %.5f; gyro scale error %.4f, residual %.6f\n", trials, failures, worstOffset, worstGain, worstScale, worstAfter);

  // The poses themselves: each must put its own up where the world's up is, and be a quarter turn from the one before.
  const simd_float3 up = {0, 1, 0};
  for (int i = 0; i < kPadCalPoses; i++) {
    double off = simd_length(simd_act(pad_cal_pose(i), kPadCalAim[i]) - up);
    if (off > 1e-5) { printf("pose %d does not stand as asked (%.5f)\n", i, off); failures++; }
    if (i && fabs(simd_dot(kPadCalAim[i], kPadCalAim[i - 1])) > 1e-5) { printf("poses %d and %d are not a quarter turn apart\n", i - 1, i); failures++; }
  }
  // From the front of the controller, the steering wheel turns one way: top edge to your right first.
  if (simd_act(pad_cal_pose(2), simd_make_float3(0, 0, -1)).x < 0.99f) { printf("the wheel turns the wrong way\n"); failures++; }

  // Holding a pose, at the controller's report rate.
  const float dt = 1.0f / 132;
  const simd_float3 zero = {0.4f, -0.3f, 0.2f};
  #define RUN(seconds, pose, off, noiseG, noiseDeg, twitchEvery) ({ \
    bool done = false; int n = (int)((seconds) / dt), at = -1; \
    for (int s = 0; s < n && !done; s++) { \
      simd_float3 raw = simd_normalize(kPadCalAim[pose] + (off)) + simd_make_float3((noiseG) * (float)gauss(), (noiseG) * (float)gauss(), (noiseG) * (float)gauss()); \
      simd_float3 rate = zero + simd_make_float3((noiseDeg) * (float)gauss(), (noiseDeg) * (float)gauss(), (noiseDeg) * (float)gauss()); \
      if ((twitchEvery) && s % (twitchEvery) == 0) { rate += simd_make_float3(60, 0, 0); raw += simd_make_float3(0.15f, 0, 0); } \
      done = pad_cal_hold_step(&h, pose, raw, rate, zero, dt); if (done) at = s; \
    } \
    done ? at * dt : -1.0f; })
  srand(7);
  PadCalHold h = {0};
  float took = RUN(4.0f, 0, simd_make_float3(0.05f, 0, 0.3f), 0.0006f, 0.15f, 0);
  printf("on the desk, resting on its grips: accepted in %.2f s\n", took);
  if (took < 2.5f || took > 3.3f) failures++;
  for (int pose = 1; pose < kPadCalPoses; pose++) {
    pad_cal_hold_reset(&h); h.started = false;
    // An ordinary trembling hand, twitching four times a second: each twitch may pause it, none may restart it.
    took = RUN(4.0f, pose, simd_make_float3(0.12f, 0.1f, 0.08f), 0.006f, 4.0f, 33);
    printf("pose %d in a trembling hand: accepted in %.2f s, %d samples averaged\n", pose, took, h.count);
    if (took < 1.3f || took > 2.6f) failures++;
  }
  pad_cal_hold_reset(&h); h.started = false;
  took = RUN(6.0f, 0, simd_make_float3(0.05f, 0, 0.1f), 0.006f, 4.0f, 0);
  printf("flat, but still in the hand: %s\n", took < 0 ? "not taken for the desk" : "wrongly accepted");
  if (took >= 0) failures++;
  pad_cal_hold_reset(&h); h.started = false;
  RUN(0.6f, 1, simd_make_float3(0, 0, 0), 0.002f, 1.0f, 0);
  float partway = h.progress;
  RUN(0.3f, 1, simd_make_float3(0, 0, 2.0f), 0.002f, 1.0f, 0);    // out of the pose for a moment
  printf("leaving the pose: %.2f falls to %.2f, %s\n", partway, h.progress, h.aimed ? "still aimed" : "no longer aimed");
  if (h.aimed || h.progress >= partway) failures++;
  pad_cal_hold_reset(&h); h.started = false;
  took = RUN(4.0f, 2, simd_make_float3(0, 1.0f, 0), 0.002f, 1.0f, 0);   // 45 degrees off
  printf("45 degrees off: %s, closeness %.2f\n", took < 0 ? "not accepted" : "wrongly accepted", h.closeness);
  if (took >= 0 || h.closeness < 0.5f || h.closeness >= 1) failures++;

  // Launch after launch, with a hand that leaves 2.5 degrees unexplained at the end of each 90 degree turn, as measured on the
  // real controller. What is kept must start near the truth, come closer, and not wander.
  {
    double squaredRaw = 0, squaredFirst = 0, squaredLast = 0, worstStep = 0;
    const int runs = 400;
    for (int t = 0; t < runs; t++) {
      srand(1000 + t);
      simd_float3 truth = {1 + 0.006f * (float)gauss(), 1 + 0.006f * (float)gauss(), 1 + 0.006f * (float)gauss()};
      simd_float3 mean = {1, 1, 1}, variance = kPadCalibrationUnknown.gyroScale;
      for (int launch = 0; launch < 8; launch++) {
        const double spread[3] = {0.027, 0.0156, 0.027};     // what one launch can say: x and z turn one quarter, y three
        simd_float3 reading = truth + simd_make_float3((float)(spread[0] * gauss()), (float)(spread[1] * gauss()), (float)(spread[2] * gauss()));
        simd_float3 noise = {(float)(spread[0] * spread[0]), (float)(spread[1] * spread[1]), (float)(spread[2] * spread[2])}, was = mean;
        pad_cal_weigh(&mean, &variance, reading, noise);
        double step = simd_reduce_max(simd_abs(mean - was));
        if (launch && step > worstStep) worstStep = step;
        if (launch == 0) { squaredRaw += simd_length_squared(reading - truth) / 3; squaredFirst += simd_length_squared(mean - truth) / 3; }
        if (launch == 7) squaredLast += simd_length_squared(mean - truth) / 3;
      }
    }
    double raw = sqrt(squaredRaw / runs), first = sqrt(squaredFirst / runs), last = sqrt(squaredLast / runs);
    printf("gyro scale, typical error: one reading taken at its word %.4f; weighed, after the first launch %.4f, after the eighth %.4f; largest change between launches %.4f\n", raw, first, last, worstStep);
    if (first > raw * 0.5 || last >= first || worstStep > 0.015) failures++;
    // The real reading of 2 November: 3.5 percent on x from one quarter turn must not be taken at its word.
    simd_float3 mean = {1, 1, 1}, variance = kPadCalibrationUnknown.gyroScale;
    pad_cal_weigh(&mean, &variance, simd_make_float3(1.0348f, 1.0056f, 1.0160f), simd_make_float3(5.4e-4f, 1.8e-4f, 5.4e-4f));
    printf("that one reading is kept as %.4f %.4f %.4f\n", mean.x, mean.y, mean.z);
    if (mean.x > 1.01f) failures++;
  }
  printf("%d failures in all\n", failures);
  return failures ? 1 : 0;
}
