// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The director: one continuous tour, frame by frame. Nothing here touches a window or the GPU, so a scripted run is the same code.
#pragma once
#include "experience/stage.h"

typedef struct {
  hs_input input;
  bool connected;
  double now_ms, target_ms;   // this moment, and when the frame will reach the panel
  float dt;
  bool focus_lost, on_cable;
  uint32_t keyboard_keys;
} hs_frame_in;

void hs_director_start(const hs_mesh_pack *pack, hs_chapter first, bool real_time);
void hs_director_frame(const hs_frame_in *in, hs_scene **out);
void hs_director_set_chapter(hs_chapter chapter);          // straight to its play, for tests and for the listening room
hs_chapter hs_director_chapter(void);
hs_phase hs_director_phase(void);
double hs_director_phase_time(void);
bool hs_director_paused(void);
float hs_director_idle_seconds(void);
bool hs_director_wants_to_close(void);
void hs_director_ask_quit(void);              // Command-Q while the picture is up: asked on screen, and a second press is the answer
bool hs_director_wants_to_quit(void);
void hs_director_set_keyboard_only(bool only);                     // the player chose to leave
void hs_director_set_calm_motion(bool calm);               // the system asked for reduced motion
void hs_director_set_modest(bool modest);                  // warm or on low power: fewer particles, never less sharp
void hs_director_finish(void);                             // lets go of everything, gently

void hs_gears_on_input(const hs_input *in, float seconds);   // on the input thread: watches for the trigger's wall

// One 21.33 ms block on the block thread: physics, the crew, what the hands and speaker get.
void hs_live_set_hold_pitch(float radians);
void hs_live_set_hand_weight(float weight);
void hs_live_set_paused(bool paused);    // nothing in the room reaches the hands or the ears
void hs_live_set_world_gain(float crew, float ball);     // how much the crew and the ball are heard and felt in this chapter
void hs_live_block(double now_ms, int8_t waveform[HS_WAVE_BYTES], float *speaker_stereo_or_null, const simd_float3 *felt_override, const simd_float3 *turn_override);
