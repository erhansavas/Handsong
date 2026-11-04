// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The hand: what the controller's sensors say, carried to the picture. Everything here was learned the hard way: the zero is
// learned in any quiet second, heading is the twist about the vertical, and a stall fades instead of jumping.
#pragma once
#include "link/protocol.h"
#include <simd/simd.h>
#include <stdbool.h>

// Called on the input thread with the keys that just went down. Must not block.
typedef void (*hs_motion_keys_handler)(uint32_t pressed);
void hs_motion_set_keys_handler(hs_motion_keys_handler handler);
// Called on the input thread with every report, and the time since the last one. Must not block.
typedef void (*hs_motion_report_handler)(const hs_input *input, float seconds);
void hs_motion_set_report_handler(hs_motion_report_handler handler);
void hs_motion_start(void);                       // listens to the link
void hs_motion_stop(void);
void hs_motion_recenter(void);

// One sensor report, already scaled: degrees per second, g, and seconds since the last. Also how recorded sessions replay.
void hs_motion_feed(const float rotation[3], const float acceleration[3], double seconds, double host_ms);

typedef struct {
  simd_quatf pose;       // the hand, heading taken out, carried forward to the target time
  simd_float3 turn;      // radians per second in the object's own frame
  double age_ms;         // how old the newest report is, at the target time
  float speed;
} hs_motion_view;

// The pose for a frame that appears at target_ms. Called from the frame thread only.
hs_motion_view hs_motion_at(double target_ms, double now_ms, float dt);

// Forces for the crew: mean acceleration in g and turning in radians per second since the last call. Owned by the world thread.
void hs_motion_take_forces(simd_float3 *felt, simd_float3 *turn);

float hs_motion_hold_pitch(simd_quatf attitude);  // how far up from flat it is held; the slow part leaves the picture
double hs_motion_gravity_error(void);             // mean degrees between gravity and the estimate, while calm
long hs_motion_reports(void);
void hs_motion_log_to(const char *path);          // keeps every report, to replay as a regression test
simd_float3 hs_motion_gyro_zero(void);
void hs_motion_restore_zero(simd_float3 zero);    // what an earlier run learned, trusted until a quiet second teaches better
void hs_motion_reset(void);
