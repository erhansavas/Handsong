// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Decodes the whole score before it is needed, on a slow thread, so that nothing is ever read during the experience.
#pragma once
#include "sound/sound.h"
#include <stdbool.h>

bool hs_stems_load(const char *folder);    // blocks; call it from a background queue
const hs_stem *hs_stems_table(void);
int hs_stems_count(void);
const int16_t *hs_stems_voice_45k(long *frames);
