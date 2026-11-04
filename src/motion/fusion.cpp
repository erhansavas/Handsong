// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "motion/fusion.h"
#include "GamepadMotionHelpers/GamepadMotion.hpp"

static GamepadMotion g_motion;

extern "C" {

void padmotion_reset(void) {
  g_motion.Reset();
  // The zero of the gyro is kept by motion.c, which also remembers it between runs, so nothing here may learn one of its
  // own: continuous calibration would take the mean of all your motion since connecting off a rate already zeroed.
  g_motion.SetCalibrationMode(GamepadMotionHelpers::Manual);
}

void padmotion_settle(const float a[3]) {
  // Plain gravity correction, run forward over a few simulated seconds.
  g_motion.ResetMotion();
  for (int i = 0; i < 120; i++) g_motion.ProcessMotion(0, 0, 0, a[0], a[1], a[2], 1.0f / 30);
}

void padmotion_update(const float r[3], const float a[3], float seconds) {
  g_motion.ProcessMotion(r[0], r[1], r[2], a[0], a[1], a[2], seconds);
}

void padmotion_gyro(float rotation[3]) {
  g_motion.GetCalibratedGyro(rotation[0], rotation[1], rotation[2]);
  const float radians = 3.14159265f / 180;
  for (int i = 0; i < 3; i++) rotation[i] *= radians;
}

void padmotion_orientation(float q[4]) { g_motion.GetOrientation(q[0], q[1], q[2], q[3]); }

}
