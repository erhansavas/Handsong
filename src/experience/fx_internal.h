// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { HS_CLICKS_NORMAL, HS_CLICKS_SILENT, HS_CLICKS_SPEAKER };
void hs_fx_set_click_mode(int mode);
void hs_fx_set_paused(bool paused);   // the world holds its breath: filter, level, hands
