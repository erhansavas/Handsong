// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The hall inside the instrument, simulated with Jolt: the crew and a steel ball, under the forces the real controller feels.
// Gravity and acceleration, centrifugal, Coriolis, angular acceleration. Lengths in metres, in the object's own frame.
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { HS_WORLD_BODIES = 7, HS_WORLD_BALL = 6 };

static const float kHallCenterZ = -0.010f, kHallFloorY = -0.0054f, kHallRadius = 0.0262f;
static const float kWorldSlowMotion = 0.45f;    // small things fall too fast to read: this is the demo's slow motion

typedef enum { HS_HIT_WALL, HS_HIT_FLOOR, HS_HIT_EACH_OTHER } hs_world_hit_kind;
typedef struct { hs_world_hit_kind kind; int body; float speed, x, z; } hs_world_hit;

void hs_world_create(void);
void hs_world_destroy(void);

// One physics step. felt: what the object's accelerometer reads, in g. turn: radians per second.
void hs_world_step(const float felt[3], const float turn[3], float dt);

// World thread only. A body on its feet keeps itself upright and goes where its feet carry it. Off, it is only a body.
void hs_world_steer(int body, bool on, float vx, float vz, float face_x, float face_z);

// From the frame thread: done at the start of the next physics step.
void hs_world_nudge(int body, float vx, float vy, float vz);
void hs_world_place(int body, float x, float y, float z);
void hs_world_set_ball_active(bool active);
void hs_world_set_well_open(bool open);      // the lid up: nothing crosses the hole around the speaker
void hs_world_set_drag(int body, float per_second);    // water holds a ball back

// From any thread: where each body was after the newest finished step.
void hs_world_pose(int body, float position[3], float rotation[4], float velocity[3]);
float hs_world_foot_drop(int body);          // from the body's position to its feet, along its up
int hs_world_take_hits(hs_world_hit *out, int capacity);
long hs_world_rescues(void);

#ifdef __cplusplus
}
#endif
