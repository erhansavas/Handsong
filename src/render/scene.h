// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Everything one frame shows, handed from the experience to the renderer.
#pragma once
#include "../../shaders/shared.h"
#include "link/protocol.h"
#include <stdbool.h>

enum { HS_MAX_PART_POSES = 256, HS_CREW = 6, HS_CREW_BONES = 7 };

typedef struct {
  simd_float3 eye, target;
  float fov_degrees;
  simd_float4x4 object;        // where the instrument is, the hand's pose
  double seconds;
  float exposure, dim, env_yaw, env_strength;
  float backdrop_glow, shadow_strength, filament;
  int light_count;
  hs_light lights[HS_MAX_LIGHTS];
  // The rig: how far each control has moved.
  float sticks[2][2];          // right and toward the player, -1 to 1
  float stick_press[2];        // 0 to 1
  float triggers[2];           // 0 to 1
  float shoulders[2];
  float keys[4];               // west, south, east, north
  float cross[2];              // right, toward the player: the one cross rocks
  float dome_press, pills[2], home, mute;
  simd_float4 touch[2];
  float gear_turn[2];          // radians
  float bellows[2];            // 0 empty to 1 full
  float needle;                // the gauge, 0 to 1
  float coil[2];               // where each magnet is, -1 to 1
  float lid;                   // 0 shut to 1 open
  float cone;                  // the speaker cone, -1 to 1
  float cylinder_step;         // the drum's turn, in steps of 16 per turn
  float tines[8];              // how far each tine is lifted, 0 to 1
  float lattice_wake;
  simd_float4 ripple[4];
  const simd_float4x4 *part_extra;   // per part: an extra placement, for parts arriving in the opening. NULL: none
  const uint8_t *part_hidden;        // per part: not drawn at all. NULL: all drawn
  // Light in the air: a pen writing with fire, and fine particles that run on the GPU.
  const simd_float4 *pen_points;     // xyz in the object's frame, w 0 to 1 along the stroke; breaks are marked by w < 0
  int pen_count;
  float pen_progress;                // how far the pen has written, in the same 0 to 1
  float pen_brightness;
  hs_particle_spawn spawns[HS_MAX_SPAWNS];
  int spawn_count;
  float chladni_m, chladni_n, chladni_shake;   // the plate's mode, and how hard it is being shaken
  float sand_amount;                 // 0 no sand, 1 the plate covered
  // A line of words, drawn last and crisp at the panel's own pixels. The renderer is told what to draw by key.
  int text_key;                      // -1 for none
  float text_alpha, text_x, text_y, text_size;
  int nav_key;                       // a second, small line at the foot of the picture; -1 for none
  float nav_alpha;
  float caption_shade;
  simd_float3 touch_color;           // what a finger on the touch surface lights, and the bar with it
  simd_float3 filament_tint;         // the colour the controller's light bar has now; zero for the instrument's own
  uint8_t player_lights;             // five bits, as on the controller
  bool mic_light;
  int capsule_count;
  hs_capsule capsules[HS_MAX_CAPSULES];
  simd_float4x4 crew_bones[HS_CREW + 1][HS_CREW_BONES];   // in the object's frame, metres; the last row is the steel ball
} hs_scene;

hs_scene hs_scene_default(void);
