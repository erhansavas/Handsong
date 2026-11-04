// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "motion/motion.h"
#include "link/link.h"
#include "motion/calibration.h"
#include "motion/fusion.h"
#include "motion/heading.h"
#include "motion/quiet_fit.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const float kHeadingSeconds = 4.0f;     // nothing absolute knows your heading, so it returns to face you this slowly
static const double kRadioDelayMs = 6;         // from the sensor to the report arriving

// Written by the report thread, read by the frame thread: a reader retries if a write crossed it.
static _Atomic unsigned g_shown_version;
static simd_quatf g_shown;
static simd_float3 g_shown_turn;
static double g_shown_at;

// Report thread only.
static struct {
  bool known, heading_known;
  float heading_zero, quiet_seconds;
  simd_float3 gyro_zero, felt_slow, recent_turn[3], clean_turn;
  int recent_at;
  PadCalibration calibration;
  hs_quiet_fit fit;
  double error_degrees;
  long error_samples, settled_reports;
  FILE *log;
} M;

static _Atomic bool g_recenter;
static _Atomic long g_reports;
static _Atomic float g_gyro_zero[3];

// Forces, summed on the report thread and taken by the world thread.
static _Atomic unsigned g_force_lock;
static simd_float3 g_felt_sum, g_turn_sum;
static int g_force_samples;
static simd_float3 g_last_felt = {0, 1, 0}, g_last_turn;

// The frame thread's filter.
static simd_quatf g_steady;
static bool g_steady_known;
static int g_steady_frames;

static inline float median3(float a, float b, float c) { return fmaxf(fminf(a, b), fminf(fmaxf(a, b), c)); }
static inline float ramp(double t, double from, double to) { return (float)fmin(1, fmax(0, (t - from) / (to - from))); }
static inline float ease(float x) { return x * x * (3 - 2 * x); }

static simd_float3 g_restored_zero;

void hs_motion_restore_zero(simd_float3 zero) { g_restored_zero = zero; }

void hs_motion_reset(void) {
  memset(&M, 0, sizeof M);
  M.gyro_zero = g_restored_zero;
  M.calibration = kPadCalibrationNone;
  M.felt_slow = simd_make_float3(0, 1, 0);
  hs_quiet_fit_init(&M.fit);
  padmotion_reset();
  g_steady_known = false;
  g_steady_frames = 0;
}

void hs_motion_feed(const float rotation[3], const float acceleration[3], double seconds, double host_ms) {
  const simd_float3 up = simd_make_float3(0, 1, 0);
  float dt = (float)fmin(seconds, 0.25);
  simd_float3 raw_felt = simd_make_float3(acceleration[0], acceleration[1], acceleration[2]);
  simd_float3 felt = pad_cal_felt(&M.calibration, raw_felt);
  const float true_felt[3] = {felt.x, felt.y, felt.z};
  float size = simd_length(felt);

  // A held hand's tremor averages to nothing, so any quiet second teaches the gyro's true zero, also in the hands.
  simd_float3 rate = simd_make_float3(rotation[0], rotation[1], rotation[2]) - M.gyro_zero;
  bool quiet = simd_length(rate) < 5 && fabsf(size - 1) < 0.035f && simd_length(felt - M.felt_slow) < 0.025f;
  M.felt_slow += (felt - M.felt_slow) * fminf(dt / 0.25f, 1);
  M.quiet_seconds = quiet ? M.quiet_seconds + dt : 0;
  if (M.quiet_seconds > 1.0f) {
    M.gyro_zero += rate * fminf(dt / 2.5f, 1);
    for (int a = 0; a < 3; a++) atomic_store(&g_gyro_zero[a], M.gyro_zero[a]);
    // The same quiet moments, in many directions, tell each accelerometer axis its zero and gain with no ritual at all.
    if (hs_quiet_fit_add(&M.fit, raw_felt)) {
      PadCalibration better = M.calibration;
      if (hs_quiet_fit_solve(&M.fit, &better.offset, &better.gain)) M.calibration = better;
    }
  }
  simd_float3 scaled = rate * M.calibration.gyroScale;
  const float clean[3] = {scaled.x, scaled.y, scaled.z};
  if (!M.known) {
    padmotion_settle(true_felt);
    M.known = true;
    M.felt_slow = felt;
  }
  padmotion_update(clean, true_felt, dt);
  float w[3], q[4];
  padmotion_gyro(w);
  padmotion_orientation(q);
  simd_float3 turn = simd_make_float3(w[0], w[1], w[2]);
  simd_quatf attitude = simd_normalize(simd_quaternion(q[1], q[2], q[3], q[0]));
  float heading = pad_heading(attitude);
  if (!M.heading_known || atomic_exchange(&g_recenter, false)) {
    M.heading_zero = heading;
    M.heading_known = true;
  } else if (simd_length(turn) < 0.6f) {
    M.heading_zero += pad_wrapped(heading - M.heading_zero) * fminf(dt / kHeadingSeconds, 1);
  }
  // What predicts the next moment must not see the actuators shaking their own sensor: one report's spike goes, and what
  // remains is averaged over a few milliseconds.
  M.recent_turn[M.recent_at = (M.recent_at + 1) % 3] = turn;
  simd_float3 steady;
  for (int a = 0; a < 3; a++) steady[a] = median3(M.recent_turn[0][a], M.recent_turn[1][a], M.recent_turn[2][a]);
  M.clean_turn += (steady - M.clean_turn) * (1 - expf(-dt / 0.005f));
  M.settled_reports++;
  if (size > 0.9f && size < 1.1f && M.settled_reports > 200) {      // the first seconds are the filter finding its feet
    M.error_degrees += acos(fmin(1, simd_dot(simd_act(attitude, felt / size), up))) * 180 / M_PI;
    M.error_samples++;
  }
  simd_quatf shown = simd_normalize(simd_mul(simd_quaternion(-M.heading_zero, up), attitude));
  atomic_fetch_add_explicit(&g_shown_version, 1, memory_order_acq_rel);
  g_shown = shown;
  g_shown_turn = M.clean_turn;
  g_shown_at = host_ms;
  atomic_fetch_add_explicit(&g_shown_version, 1, memory_order_acq_rel);

  unsigned expected = 0;
  while (!atomic_compare_exchange_weak(&g_force_lock, &expected, 1)) expected = 0;
  g_felt_sum += felt;
  g_turn_sum += turn;
  g_force_samples++;
  atomic_store(&g_force_lock, 0);
  atomic_fetch_add(&g_reports, 1);
  if (M.log) fprintf(M.log, "%.4f %.3f %.3f %.3f %.4f %.4f %.4f\n", seconds, rotation[0], rotation[1], rotation[2], acceleration[0], acceleration[1], acceleration[2]);
}

void hs_motion_take_forces(simd_float3 *felt, simd_float3 *turn) {
  unsigned expected = 0;
  while (!atomic_compare_exchange_weak(&g_force_lock, &expected, 1)) expected = 0;
  if (g_force_samples) {
    g_last_felt = g_felt_sum / (float)g_force_samples;     // the mean also averages out the actuators shaking the sensor
    g_last_turn = g_turn_sum / (float)g_force_samples;
    g_felt_sum = g_turn_sum = simd_make_float3(0, 0, 0);
    g_force_samples = 0;
  }
  *felt = g_last_felt;
  *turn = g_last_turn;
  atomic_store(&g_force_lock, 0);
}

static uint32_t g_clock;
static bool g_clock_known;
static hs_imu_calibration g_imu;
static bool g_imu_known;

static hs_motion_keys_handler g_keys_handler;
static uint32_t g_last_keys;

void hs_motion_set_keys_handler(hs_motion_keys_handler handler) { g_keys_handler = handler; }

static hs_motion_report_handler g_report_handler;
void hs_motion_set_report_handler(hs_motion_report_handler handler) { g_report_handler = handler; }

static void on_input(const hs_input *in, double host_ms, void *context) {
  (void)context;
  uint32_t pressed = in->keys & ~g_last_keys;
  g_last_keys = in->keys;
  if (pressed && g_keys_handler) g_keys_handler(pressed);
  if (!g_imu_known) { g_imu = hs_link_calibration(); g_imu_known = true; }
  float rotation[3], acceleration[3];
  for (int a = 0; a < 3; a++) {
    rotation[a] = ((float)in->gyro[a] - g_imu.gyro_zero[a]) * g_imu.gyro_scale[a];
    acceleration[a] = ((float)in->accel[a] - g_imu.accel_zero[a]) * g_imu.accel_scale[a];
  }
  double seconds = g_clock_known ? (uint32_t)(in->sensor_clock - g_clock) / 3e6 : 0;
  if (g_report_handler && seconds > 0) g_report_handler(in, (float)seconds);
  g_clock = in->sensor_clock;
  g_clock_known = true;
  hs_motion_feed(rotation, acceleration, seconds, host_ms);
}

void hs_motion_start(void) {
  hs_motion_reset();
  hs_link_set_input_handler(on_input, NULL);
}

void hs_motion_stop(void) {
  hs_link_set_input_handler(NULL, NULL);
  if (M.log) { fclose(M.log); M.log = NULL; }
}

void hs_motion_recenter(void) { atomic_store(&g_recenter, true); }

hs_motion_view hs_motion_at(double target_ms, double now_ms, float dt) {
  (void)now_ms;
  simd_quatf attitude;
  simd_float3 turn;
  double at;
  unsigned before, after;
  do {
    before = atomic_load_explicit(&g_shown_version, memory_order_acquire);
    attitude = g_shown;
    turn = g_shown_turn;
    at = g_shown_at;
    after = atomic_load_explicit(&g_shown_version, memory_order_acquire);
  } while (before != after || (before & 1));
  if (at == 0) attitude = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  double horizon = target_ms - at + kRadioDelayMs;
  float speed = simd_length(turn);
  float gate = fminf(1, fmaxf(0, (speed - 0.15f) / 0.65f));
  if (horizon > 0 && speed > 1e-3f) {
    // Carried forward only while really turning: a steady hand's tremor would be enlarged by predicting it. When the radio
    // stalls the prediction fades away gradually, so the picture can never jump back and forth.
    float ahead = (float)(fmin(horizon, 45) / 1000), trust = 1 - ease(ramp(horizon, 45, 110));
    attitude = simd_normalize(simd_mul(attitude, simd_quaternion(speed * ahead * trust * gate * gate * (3 - 2 * gate), turn / speed)));
  }
  // The last step before the screen. Still, it takes out the sensor's noise; moving, it is out of the way within a frame.
  // Any sudden change, a recentre or the controller waking, is crossed in motion instead of a jump.
  if (!g_steady_known) { g_steady = attitude; g_steady_known = true; }
  float cutoff = 12 + 30 * speed, tau = 1 / (2 * (float)M_PI * cutoff);
  float blend = dt / (dt + tau);
  // After a stall the sensors may say the hand is somewhere else by now. The picture walks there, a little faster than the hand moves,
  // instead of arriving in one frame.
  simd_quatf gap = simd_mul(simd_inverse(g_steady), attitude);
  float apart = 2 * atan2f(simd_length(simd_imag(gap)), fabsf(simd_real(gap)));
  float allowed = (speed + 0.25f) * dt;
  // Except while the very first attitude is still being found, behind the opening's dark or a fade.
  if (g_steady_frames++ > 90 && apart * blend > allowed && apart > 1e-4f) blend = allowed / apart;
  g_steady = simd_normalize(simd_slerp(g_steady, attitude, blend));
  hs_motion_view view = {g_steady, turn, target_ms - at, speed};
  return view;
}

float hs_motion_hold_pitch(simd_quatf attitude) {
  simd_float3 face_up = simd_act(attitude, simd_make_float3(0, 1, 0));
  return fmaxf(-0.35f, fminf(1.5f, atan2f(face_up.z, face_up.y)));
}

double hs_motion_gravity_error(void) { return M.error_samples ? M.error_degrees / (double)M.error_samples : 0; }
long hs_motion_reports(void) { return atomic_load(&g_reports); }
void hs_motion_log_to(const char *path) { M.log = fopen(path, "w"); }
simd_float3 hs_motion_gyro_zero(void) { return simd_make_float3(atomic_load(&g_gyro_zero[0]), atomic_load(&g_gyro_zero[1]), atomic_load(&g_gyro_zero[2])); }
