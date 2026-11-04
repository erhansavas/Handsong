// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// handsong --test hw-smoke [seconds]: what the live controller says, with nothing audible and no vibration sent.
#import "app/test_modes.h"
#import "link/link.h"
#import <Foundation/Foundation.h>
#include <math.h>

int hs_test_hw_smoke(double seconds) {
  hs_link_start();
  hs_link_set_lights_quiet(false, false);
  double start = hs_now_ms();
  while (hs_link_state() == HS_CONTROLLER_NONE && hs_now_ms() - start < 3000) usleep(20000);
  if (hs_link_state() == HS_CONTROLLER_NONE) {
    printf("no controller connected\n");
    hs_link_stop();
    return 2;
  }
  printf("state %d\n", hs_link_state());
  usleep((useconds_t)(seconds * 1e6));
  hs_input in;
  hs_link_snapshot(&in);
  hs_imu_calibration cal = hs_link_calibration();
  double g[3];
  for (int a = 0; a < 3; a++) g[a] = (in.accel[a] - cal.accel_zero[a]) * cal.accel_scale[a];
  printf("input rate %.0f/s, wide %d\n", hs_link_input_rate(), hs_link_wide());
  printf("sticks %d %d %d %d, triggers %d %d, keys %x\n", in.left_x, in.left_y, in.right_x, in.right_y, in.left_trigger, in.right_trigger, in.keys);
  printf("gyro scale %.4f %.4f %.4f (1/16.4 = 0.0610)\n", cal.gyro_scale[0], cal.gyro_scale[1], cal.gyro_scale[2]);
  printf("felt %.3f %.3f %.3f g, size %.3f\n", g[0], g[1], g[2], sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]));
  printf("battery %d%%, charging %d\n", in.battery_percent, in.charging);
  hs_link_stats stats = hs_link_get_stats();
  printf("control reports %ld, failures %ld\n", stats.control_sent, stats.send_failures);
  hs_link_stop();
  bool scale_ok = fabs(cal.gyro_scale[0] - 0.061) < 0.006, gravity_ok = fabs(sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) - 1) < 0.2;
  printf("%s\n", scale_ok && gravity_ok ? "hw-smoke: pass" : "hw-smoke: FAIL");
  return scale_ok && gravity_ok ? 0 : 1;
}
