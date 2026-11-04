// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// One block of what happens between frames: 16 physics steps under the forces the hand really makes, then the hands' block.
#include "experience/director.h"
#include "hands/hands.h"
#include "sound/sound.h"
#include "world/crew.h"
#include "world/world.h"
#include <math.h>
#include <stdatomic.h>

enum { kSubsteps = 16 };

static _Atomic float g_crew_touch_gain = 1, g_ball_touch_gain = 1, g_hold_pitch;

static _Atomic float g_hand_weight = 1;

void hs_live_set_hold_pitch(float radians) { atomic_store_explicit(&g_hold_pitch, radians, memory_order_relaxed); }
void hs_live_set_hand_weight(float weight) { atomic_store_explicit(&g_hand_weight, weight, memory_order_relaxed); }

static _Atomic bool g_quiet_for_pause;
void hs_live_set_paused(bool paused) { atomic_store(&g_quiet_for_pause, paused); }

void hs_live_set_world_gain(float crew, float ball) {
  atomic_store(&g_crew_touch_gain, crew);
  atomic_store(&g_ball_touch_gain, ball);
}

void hs_live_block(double now_ms, int8_t waveform[HS_WAVE_BYTES], float *speaker_stereo_or_null, const simd_float3 *felt_override, const simd_float3 *turn_override) {
  (void)speaker_stereo_or_null;
  simd_float3 felt, turn;
  if (felt_override) { felt = *felt_override; turn = *turn_override; }
  else hs_motion_take_forces(&felt, &turn);
  // The grip's slow part leaves the physics as it leaves the picture: holding it comfortably tilted is not a slope in the hall.
  felt = simd_act(simd_quaternion(atomic_load_explicit(&g_hold_pitch, memory_order_relaxed), simd_make_float3(1, 0, 0)), felt);
  // While the opening still holds the object itself, its hall is level whatever the hand is doing.
  float weight = atomic_load_explicit(&g_hand_weight, memory_order_relaxed);
  felt = simd_make_float3(0, 1, 0) + (felt - simd_make_float3(0, 1, 0)) * weight;
  turn *= weight;
  const float dt = (float)(HS_STREAM_PERIOD_MS / 1000.0 / kSubsteps);
  const float felt3[3] = {felt.x, felt.y, felt.z}, turn3[3] = {turn.x, turn.y, turn.z};
  float crew_gain = atomic_load_explicit(&g_crew_touch_gain, memory_order_relaxed), ball_gain = atomic_load_explicit(&g_ball_touch_gain, memory_order_relaxed);
  if (atomic_load_explicit(&g_quiet_for_pause, memory_order_relaxed)) crew_gain = ball_gain = 0;
  for (int i = 0; i < kSubsteps; i++) {
    hs_crew_step(felt3, dt, now_ms + i * (double)dt * 1000);
    hs_world_step(felt3, turn3, dt);
    hs_world_hit hits[16];
    int count = hs_world_take_hits(hits, 16);
    for (int k = 0; k < count; k++) {
      const hs_world_hit *h = &hits[k];
      float pan = fmaxf(-1, fminf(1, h->x / 0.026f));
      float amount = fminf(1, h->speed * 9);
      if (amount < 0.05f) continue;
      if (h->body == HS_WORLD_BALL) {
        hs_hands_hit_rt(95 + 40 * amount, 0.05f, amount * 0.75f * ball_gain, pan);
        if (amount > 0.3f) hs_sound_strike_rt(HS_MAT_SOUND_STEEL, 2100, amount * 0.04f * ball_gain, pan * 0.7f);
      } else {
        hs_hands_hit_rt(80 + 30 * (float)h->body, 0.045f, amount * 0.55f * crew_gain, pan);
        // The crew's small knocks belong to the hands. Only the hard ones reach the room, or six of them become a rattle.
        if (amount > 0.6f) hs_sound_strike_rt(HS_MAT_SOUND_BRASS, 900 + 240 * (float)h->body, amount * 0.015f * crew_gain, pan * 0.7f);
      }
    }
  }
  // Every footfall of the crew, where it landed.
  hs_crew_events events = hs_crew_take_events();
  if (events.foot_strength > 0 && crew_gain > 0) {
    float pan = fmaxf(-1, fminf(1, events.foot_x / 0.026f));
    if (events.foot_strength > 0.16f) hs_hands_hit_rt(62 + 60 * (1 - events.foot_strength), 0.05f, events.foot_strength * crew_gain, pan);
    hs_sound_strike_rt(HS_MAT_SOUND_BRASS, 760 + 900 * (0.34f - events.foot_strength), events.foot_strength * 0.015f * crew_gain, pan * 0.7f);
  }
  hs_hands_block(waveform);
}
