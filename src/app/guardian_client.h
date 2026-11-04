// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Starts the small helper that puts the controller and the sound output right if this process is killed.
#pragma once

void hs_guardian_start(void);
void hs_guardian_goodbye(void);          // the app is leaving cleanly, so the helper has nothing to do
const char *hs_guardian_output_file(void);   // where the sound output to return to is kept while ours is selected
