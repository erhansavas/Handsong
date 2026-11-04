// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Command line modes used by the gates. None of them is reachable from the app's own interface.
#pragma once

int hs_test_hw_smoke(double seconds);
int hs_test_tone(double hertz, double seconds, double gain);
int hs_test_render(int argc, char **argv);
int hs_test_experience(int argc, char **argv);
int hs_test_full_screen(int argc, char **argv);
int hs_test_focus(void);
int hs_test_hear(const char *out_path);
int hs_test_mic_level(double seconds);
int hs_test_mic_gate(void);
int hs_test_breath_real(double seconds);
int hs_test_hands_proof(_Bool voice);
int hs_test_motion(int argc, char **argv);
int hs_test_trigger_prediction(void);
int hs_test_press_latency(void);
int hs_test_scenarios(void);
int hs_test_realtime_audit(void);
