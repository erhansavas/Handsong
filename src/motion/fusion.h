// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Sensor fusion for the controller's gyro and accelerometer, using GamepadMotionHelpers.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void padmotion_reset(void);
// Sets the starting orientation from gravity alone, so the first picture is already right.
void padmotion_settle(const float acceleration[3]);
// rotation in degrees per second, acceleration in g, seconds since the last call.
void padmotion_update(const float rotation[3], const float acceleration[3], float seconds);
// Rotation with the gyro's zero removed, in radians per second.
void padmotion_gyro(float rotation[3]);
// From the controller to the world, with y up: w, x, y, z.
void padmotion_orientation(float quaternion[4]);

#ifdef __cplusplus
}
#endif
