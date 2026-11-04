// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The Mac's sound output: which is selected, how late it plays, and the "Handsong Speaker" entry in the Sound list.
#pragma once
#include <stdbool.h>

// The entry exists only while the app runs. Macs keep such a device after a process ends, so it is removed on leaving, and
// any left by a crash is removed at the next launch.
bool hs_output_ensure(void);
void hs_output_remove(void);
bool hs_output_is_selected(void);
bool hs_output_select(bool selected);    // remembers the output to return to, also for the guardian

bool hs_output_is_bluetooth(void);       // wireless headphones already play later than the hands feel
double hs_output_latency_ms(void);       // from the sample leaving this app to the sound leaving the device

typedef void (*hs_output_changed)(void *context);
void hs_output_watch(hs_output_changed changed, void *context);
