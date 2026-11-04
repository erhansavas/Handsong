// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#pragma once
#include <stdbool.h>

#include "render/scene.h"

void hs_lights_frame(hs_scene *scene);           // once a frame while the picture is up: the controller and the model, the same
void hs_lights_stop(void);                       // lights out, and the link is left to whatever comes next
void hs_lights_set_follow_music(bool on);        // in Full Feel: the bar moves with what the hands receive
void hs_lights_set_steady(bool steady);          // Reduce Motion: the colours stay, the pulsing goes
