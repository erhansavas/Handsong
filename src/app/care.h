// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Everything that touches the system, in one place, and reversible from one place: the login item, what the old tools left behind,
// remembered calibration and leaving no trace on removal.
#pragma once
#include <stdbool.h>

void hs_care_at_launch(void);        // restores what the motion path has learned
void hs_care_at_quit(void);
bool hs_care_login_item(void);
void hs_care_set_login_item(bool on);
bool hs_care_in_applications(void);
void hs_care_remove_everything(void);
