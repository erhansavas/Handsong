// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "app/app_delegate.h"
#import "app/test_modes.h"
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc >= 3 && !strcmp(argv[1], "--test")) {
      setenv("HANDSONG_PASS_TIMES", "1", 0);
      if (!strcmp(argv[2], "hw-smoke")) return hs_test_hw_smoke(argc >= 4 ? atof(argv[3]) : 3);
      if (!strcmp(argv[2], "tone")) return hs_test_tone(argc >= 4 ? atof(argv[3]) : 1000, argc >= 5 ? atof(argv[4]) : 3, argc >= 6 ? atof(argv[5]) : 0.2);
      if (!strcmp(argv[2], "render")) return hs_test_render(argc - 3, argv + 3);
      if (!strcmp(argv[2], "experience")) return hs_test_experience(argc - 3, argv + 3);
      if (!strcmp(argv[2], "full-screen")) return hs_test_full_screen(argc - 3, argv + 3);
      if (!strcmp(argv[2], "focus")) return hs_test_focus();
      if (!strcmp(argv[2], "hear")) return hs_test_hear(argc >= 4 ? argv[3] : "/tmp/handsong_hear.txt");
      if (!strcmp(argv[2], "mic-level")) return hs_test_mic_level(argc >= 4 ? atof(argv[3]) : 12);
      if (!strcmp(argv[2], "mic-gate")) return hs_test_mic_gate();
      if (!strcmp(argv[2], "breath-real")) return hs_test_breath_real(argc >= 4 ? atof(argv[3]) : 25);
      if (!strcmp(argv[2], "hands-proof")) return hs_test_hands_proof(false);
      if (!strcmp(argv[2], "voice-proof")) return hs_test_hands_proof(true);
      if (!strcmp(argv[2], "motion")) return hs_test_motion(argc - 3, argv + 3);
      if (!strcmp(argv[2], "trigger-prediction")) return hs_test_trigger_prediction();
      if (!strcmp(argv[2], "press-latency")) return hs_test_press_latency();
      if (!strcmp(argv[2], "scenarios")) return hs_test_scenarios();
      if (!strcmp(argv[2], "realtime-audit")) return hs_test_realtime_audit();
      fprintf(stderr, "unknown test mode %s\n", argv[2]);
      return 64;
    }
    NSApplication *app = NSApplication.sharedApplication;
    AppDelegate *delegate = [AppDelegate new];
    app.delegate = delegate;
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [app run];
  }
  return 0;
}
