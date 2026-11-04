// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What the crew do: each has a trade and a place to work in each chapter, so they spread through the hall with purpose and
// never pile up. They look at you, answer a touch, lose their footing when the hand asks too much, and get up again.
#pragma once
#include "render/scene.h"

enum { HS_PIP, HS_TAM, HS_MOX, HS_OLA, HS_BO, HS_WREN };

typedef enum { HS_JOB_IDLE, HS_JOB_GOTO, HS_JOB_WORK, HS_JOB_WATCH, HS_JOB_SIT, HS_JOB_CHEER } hs_crew_job;

typedef struct { float x, z; hs_crew_job job; } hs_crew_station;

typedef struct {
  float foot_strength;       // how hard a foot came down since the last read, for the hands
  float foot_x;
} hs_crew_events;

void hs_crew_init(void);
void hs_crew_set_stations(const hs_crew_station stations[HS_CREW]);
void hs_crew_set_awake(float awake);                 // 0 asleep, 1 at work; their irises open with it
void hs_crew_set_attention(float x, float z, float strength);   // a touch on the dome draws them

// One physics step, on the world's thread: decides where they walk. felt is in g.
void hs_crew_step(const float felt[3], float dt, double now_ms);

// For the frame: bones and soft shadows, from the newest physics.
void hs_crew_pose(hs_scene *scene, simd_float3 eye_in_object, float dt);
hs_crew_events hs_crew_take_events(void);
float hs_crew_lamp(simd_float3 *where);              // the lamplighter's light, in the object's frame
void hs_crew_rest(hs_scene *scene, simd_float3 eye);  // a still pose with no physics running
void hs_crew_cheer(int who);                         // a hop and raised arms; also how they bow at the end
void hs_crew_cluster_report(double *worst_seconds);  // how long four of them have ever stood within 15 mm, for the gate
