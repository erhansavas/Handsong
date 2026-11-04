// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// How each part moves. One owner for the rig: every model pack names the same parts, and each moves here and nowhere else.
#pragma once
#include "render/mesh_pack.h"
#include "render/scene.h"

// Fills one draw per part with its place in the world for this frame.
void hs_rig_compute(const hs_mesh_pack *pack, const hs_scene *scene, hs_draw *draws);
