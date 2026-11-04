// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The shown pose must keep its heading whatever the tilt, and must never come out turned around.
// clang -O1 -Wall -Isrc test/heading_test.c -lm -o /tmp/heading_test && /tmp/heading_test
#include "motion/heading.h"
#include <stdio.h>
#include <stdlib.h>

static simd_quatf pose(float yawDeg, float pitchDeg, float rollDeg) {
  const float k = (float)M_PI / 180;
  return simd_normalize(simd_mul(simd_quaternion(yawDeg * k, simd_make_float3(0, 1, 0)),
                                 simd_mul(simd_quaternion(pitchDeg * k, simd_make_float3(1, 0, 0)), simd_quaternion(rollDeg * k, simd_make_float3(0, 0, 1)))));
}

int main(void) {
  int failures = 0, checks = 0;
  const simd_float3 up = simd_make_float3(0, 1, 0), right = simd_make_float3(1, 0, 0);
  for (float yaw = -170; yaw <= 170; yaw += 17) {
    float last = 0;
    for (float pitch = 0; pitch <= 88; pitch += 2) {
      simd_quatf q = pose(yaw, pitch, 0);
      float h = pad_heading(q);
      simd_quatf shown = simd_normalize(simd_mul(simd_quaternion(-h, up), q));
      // With its own heading taken away the controller's right-hand side must point right, at every tilt.
      float along = simd_dot(simd_act(shown, right), right);
      checks++;
      if (along < 0.999f) { printf("turned: yaw %.0f pitch %.0f right-axis dot %.3f\n", yaw, pitch, along); failures++; }
      float step = pitch > 0 ? fabsf(pad_wrapped(h - last)) * 180 / (float)M_PI : 0;
      if (step > 0.01f) { printf("jump: yaw %.0f pitch %.0f by %.2f deg\n", yaw, pitch, step); failures++; }
      last = h;
    }
  }
  // A roll on top of the tilt, as a real hand does: the heading stays near, never turned around.
  for (float yaw = -160; yaw <= 160; yaw += 40) for (float pitch = 0; pitch <= 85; pitch += 5) for (float roll = -40; roll <= 40; roll += 10) {
    float off = fabsf(pad_wrapped(pad_heading(pose(yaw, pitch, roll)) - yaw * (float)M_PI / 180)) * 180 / (float)M_PI;
    checks++;
    if (off > 45) { printf("far off: yaw %.0f pitch %.0f roll %.0f by %.1f deg\n", yaw, pitch, roll, off); failures++; }
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
