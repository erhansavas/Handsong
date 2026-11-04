// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Which way a pose faces around the vertical: the twist of the rotation about the world's y axis. It stays continuous
// however steeply the controller is held, where following one of its axes would jump by 180 degrees.
#pragma once
#include <math.h>
#include <simd/simd.h>

static inline float pad_heading(simd_quatf q) { return 2 * atan2f(q.vector.y, q.vector.w); }
static inline float pad_wrapped(float angle) { return atan2f(sinf(angle), cosf(angle)); }
