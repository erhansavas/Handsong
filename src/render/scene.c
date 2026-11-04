// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "render/scene.h"
#include <string.h>

hs_scene hs_scene_default(void) {
  hs_scene s;
  memset(&s, 0, sizeof s);
  s.text_key = s.nav_key = -1;
  s.eye = simd_make_float3(0, 0.20f, 0.20f);
  s.target = simd_make_float3(0, 0, 0);
  s.fov_degrees = 34;
  s.object = matrix_identity_float4x4;
  // Out of sight until the crew has a place: below the hall floor.
  for (int who = 0; who <= HS_CREW; who++) {
    for (int bone = 0; bone < HS_CREW_BONES; bone++) {
      s.crew_bones[who][bone] = matrix_identity_float4x4;
      s.crew_bones[who][bone].columns[3] = simd_make_float4(0, -0.012f, -0.010f, 1);
    }
  }
  s.touch[0] = s.touch[1] = simd_make_float4(0, 0, 0, -1);     // nobody has touched it yet
  for (int i = 0; i < 4; i++) s.ripple[i] = simd_make_float4(0, 0, 0, 100);
  s.text_key = -1;
  s.exposure = 1;
  s.env_strength = 1;
  s.backdrop_glow = 1;
  s.shadow_strength = 0.85f;
  s.light_count = 2;
  s.lights[0] = (hs_light){simd_make_float3(0.35f, 0.9f, 0.30f), 0.08f, simd_make_float3(2.6f, 2.45f, 2.3f), 0};
  s.lights[1] = (hs_light){simd_make_float3(-0.7f, 0.25f, -0.6f), 0.14f, simd_make_float3(0.9f, 0.55f, 0.42f), 0};
  return s;
}
