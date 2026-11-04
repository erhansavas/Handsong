// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// handsong --test motion FILE: the sensor path, replayed. Without FILE, scripted cases that must keep their direction and never jump.
// Each line of a recording: seconds, three rates in degrees per second, three accelerations in g.
#import "app/test_modes.h"
#import "motion/heading.h"
#import "motion/motion.h"
#import <Foundation/Foundation.h>

typedef struct { double worst_step, worst_gravity; long frames; } Result;

static float angle_between(simd_quatf a, simd_quatf b) {
  simd_quatf d = simd_mul(simd_inverse(a), b);
  return 2 * atan2f(simd_length(simd_imag(d)), fabsf(simd_real(d))) * 180 / (float)M_PI;
}

// Plays one hand path through the real filter at the controller's rate, and reads it back at 60 Hz as a frame would.
typedef simd_quatf (*hand_path)(double t);

static Result run_path(hand_path path, double seconds, double stall_from, double stall_to) {
  hs_motion_reset();
  Result r = {0};
  simd_quatf last = path(0), previous_view = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  const double report = 1.0 / 133;
  double next_frame = 0;
  bool first = true;
  double t = 0, last_fed = 0, previous_speed = 0;
  while (t < seconds) {
    simd_quatf q = path(t);
    bool stalled = t >= stall_from && t < stall_to;
    if (!stalled) {
      double dt = t - last_fed;
      if (dt <= 0) dt = report;
      simd_quatf delta = simd_mul(simd_inverse(last), q);
      float angle = 2 * atan2f(simd_length(simd_imag(delta)), simd_real(delta));
      simd_float3 axis = simd_length(simd_imag(delta)) > 1e-7f ? simd_normalize(simd_imag(delta)) : simd_make_float3(0, 1, 0);
      simd_float3 rate = axis * (angle / (float)dt) * (float)(180 / M_PI);
      simd_float3 felt = simd_act(simd_inverse(q), simd_make_float3(0, 1, 0));
      const float rotation[3] = {rate.x, rate.y, rate.z}, acceleration[3] = {felt.x, felt.y, felt.z};
      hs_motion_feed(rotation, acceleration, dt, t * 1000);
      last = q;
      last_fed = t;
    }
    if (t >= next_frame) {
      hs_motion_view view = hs_motion_at(t * 1000 + 16, t * 1000, 1.0f / 60);
      if (!first && t > 1.6) {
        double step = angle_between(previous_view, view.pose);
        // What the hand itself moved in this frame is no jump, and neither is the change in how far ahead the prediction looks.
        double hand = angle_between(path(t - 1.0 / 60), q);
        double looking = fabs(view.speed - previous_speed) * 0.022 * 180 / M_PI;
        r.worst_step = fmax(r.worst_step, step - hand - looking);
      }
      previous_speed = view.speed;
      previous_view = view.pose;
      first = false;
      next_frame += 1.0 / 60;
      r.frames++;
    }
    t += report;
  }
  return r;
}

static simd_quatf attitude(float pitch, float yaw, float roll) {
  return simd_mul(simd_quaternion(yaw, simd_make_float3(0, 1, 0)), simd_mul(simd_quaternion(pitch, simd_make_float3(1, 0, 0)), simd_quaternion(roll, simd_make_float3(0, 0, 1))));
}
static simd_quatf path_still(double t) { (void)t; return attitude(0.35f, 0, 0); }
static simd_quatf path_slow_grips(double t) { return attitude(0.2f + 0.5f * (float)sin(t * 0.5), 0.3f * (float)sin(t * 0.3), 0.25f * (float)sin(t * 0.4)); }
static simd_quatf path_quick(double t) { return attitude(0.3f * (float)sin(t * 3), 0.6f * (float)sin(t * 2.2), 0.4f * (float)sin(t * 2.7)); }
static simd_quatf path_full_flip(double t) { return attitude((float)fmod(t, 8) * 0.785f, 0, 0); }

int hs_test_motion(int argc, char **argv) {
  int failures = 0;
  if (argc >= 1) {
    FILE *f = fopen(argv[0], "r");
    if (!f) { fprintf(stderr, "no recording\n"); return 1; }
    hs_motion_reset();
    double seconds, t = 0, next_frame = 0, worst_step = 0;
    float g[3], a[3];
    simd_quatf previous = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
    long frames = 0;
    while (fscanf(f, "%lf %f %f %f %f %f %f", &seconds, &g[0], &g[1], &g[2], &a[0], &a[1], &a[2]) == 7) {
      t += seconds;
      hs_motion_feed(g, a, seconds, t * 1000);
      if (t >= next_frame) {
        hs_motion_view view = hs_motion_at(t * 1000 + 16, t * 1000, 1.0f / 60);
        if (frames > 100) worst_step = fmax(worst_step, angle_between(previous, view.pose));
        previous = view.pose;
        next_frame += 1.0 / 60;
        frames++;
      }
    }
    fclose(f);
    double gravity = hs_motion_gravity_error();
    printf("replay %s: %ld frames, largest step %.3f deg (at most 0.04), gravity error %.3f deg (at most 0.04)\n", argv[0], frames, worst_step, gravity);
    if (worst_step > 0.04 || gravity > 0.04) failures++;
  }
  struct { const char *name; hand_path path; double stall_from, stall_to; double limit; } cases[] = {
    {"still", path_still, -1, -1, 0.3}, {"slow grips", path_slow_grips, -1, -1, 0.3}, {"quick turns", path_quick, -1, -1, 0.3}, {"full flip", path_full_flip, -1, -1, 0.3},
    {"radio stall", path_slow_grips, 4.0, 5.5, 0.3}, {"stall while turning", path_quick, 4.0, 4.5, 0.3},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    Result r = run_path(cases[i].path, 16, cases[i].stall_from, cases[i].stall_to);
    bool ok = r.worst_step <= cases[i].limit;
    printf("%s %-22s jump beyond the hand's own motion %.3f deg (at most %.2f)\n", ok ? "pass" : "FAIL", cases[i].name, r.worst_step, cases[i].limit);
    if (!ok) failures++;
  }
  // Direction: tilting right must roll the object right, in every grip. A simple sign test on real filter output.
  struct { const char *name; float pitch; } grips[] = {{"flat", 0}, {"held up", 0.8f}, {"steep", 1.3f}};
  for (unsigned i = 0; i < 3; i++) {
    hs_motion_reset();
    float pitch = grips[i].pitch;
    simd_quatf q = attitude(pitch, 0, 0);
    simd_float3 felt = simd_act(simd_inverse(q), simd_make_float3(0, 1, 0));
    const float still[3] = {0, 0, 0}, f[3] = {felt.x, felt.y, felt.z};
    simd_quatf before = simd_quaternion(0.0f, simd_make_float3(0, 1, 0)), after = before;
    for (int k = 0; k < 800; k++) {
      hs_motion_feed(still, f, 1.0 / 133, k / 0.133);
      if (k % 2 == 0) before = hs_motion_at(k / 0.133 + 16, k / 0.133, 1.0f / 66).pose;
    }
    // A turn about the object's own vertical, clockwise seen from above, for half a second.
    const float turn[3] = {0, -60, 0};
    for (int k = 0; k < 66; k++) {
      hs_motion_feed(turn, f, 1.0 / 133, 6100 + k / 0.133);
      if (k % 2 == 0) after = hs_motion_at(6100 + k / 0.133 + 16, 6100 + k / 0.133, 1.0f / 66).pose;
    }
    simd_float3 axis_x_before = simd_act(before, simd_make_float3(1, 0, 0)), axis_x_after = simd_act(after, simd_make_float3(1, 0, 0));
    float moved = simd_dot(simd_cross(axis_x_before, axis_x_after), simd_act(before, simd_make_float3(0, 1, 0)));
    bool ok = moved < -0.05f;
    printf("%s direction in the %s grip: %.3f\n", ok ? "pass" : "FAIL", grips[i].name, moved);
    if (!ok) failures++;
  }
  printf("%d failures\n", failures);
  return failures ? 1 : 0;
}
