// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The experience's connection to the Mac's sound output. Changing device fades, reopens at the new latency and starts from silence.
#pragma once
#include <stdbool.h>

bool hs_sound_output_start(void);
void hs_sound_output_stop(void);
void hs_sound_output_device_changed(void);
double hs_sound_output_latency_ms(void);    // what the device adds at this buffer size, from its own report
