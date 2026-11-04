// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Lattice: a sensing grid waking under the finger, in the piece's own warm light.
// Balance: the crew under true inertial forces, and the optional walk inside.
#include "experience/director.h"
#include "hands/hands.h"
#include "sound/sound.h"
#include "world/crew.h"
#include "world/world.h"
#include <math.h>

static void set_stations(const float where[HS_CREW][2], hs_crew_job job) {
  hs_crew_station s[HS_CREW];
  for (int i = 0; i < HS_CREW; i++) s[i] = (hs_crew_station){where[i][0] * 0.001f, kHallCenterZ + where[i][1] * 0.001f, job};
  hs_crew_set_stations(s);
}

// ============================================================================ Lattice
static float g_bell_hz = 880;
static float g_wake;
static int g_trace;

static void lattice_enter(void) {
  static const float stations[HS_CREW][2] = {{-15, 12}, {15, 12}, {-19.5f, -6}, {19.5f, -6}, {0, 19.5f}, {0, 3}};
  set_stations(stations, HS_JOB_WATCH);
  hs_set_shot(simd_make_float3(0, 0.002f, -0.010f), simd_make_float3(0, 0.145f, 0.018f), 30, false);
  S.camera_speed = 1.5f;
  hs_sound_score(0, HS_STEM_LATTICE_BED, 1.0f, 3.0f, true);
  hs_sound_score(1, HS_STEM_LATTICE_TOP, 1.0f, 3.0f, true);
  hs_live_set_world_gain(0.05f, 0);
  hs_hands_silence();
  S.mover_power = 0;
  S.filament = 0.5f;
  g_wake = 0;
  const hs_trigger_effect soft = hs_trigger_spring(1, 9, 1, 3);
  hs_fx_set_triggers(&soft, &soft);
}

static void lattice_frame(void) {
  float t = (float)S.phase_time;
  if (S.phase == HS_PHASE_AWAKEN) {
    S.env = 1 - 0.45f * hs_stage_ramp(t, 0, 2);
    static float last = 0;
    if (t < last) last = 0;
    if (last < 1.0f && t >= 1.0f) {     // one sweep runs across the sleeping grid
      S.scene.touch[0] = simd_make_float4(0, kHallCenterZ, 0.0f, 0);
      hs_hands_hit(140, 0.25f, 0.28f, 0);
      hs_sound_strike(HS_MAT_SOUND_GLASS, 1760, 0.02f, 0, HS_TIMING_ALIGNED);
    }
    last = t;
    if (S.scene.touch[0].w >= 0) S.scene.touch[0].w += S.dt;
  }
  if (S.phase == HS_PHASE_PLAY) hs_set_shot(simd_make_float3(0, 0.002f, -0.010f), simd_make_float3(0, 0.150f, 0.030f), 30, true);
  if (S.phase == HS_PHASE_LEAVING) {
    hs_set_shot(simd_make_float3(0, -0.002f, -0.006f), simd_make_float3(0.0f, 0.170f, 0.150f), 30, true);
    S.env = 0.55f + 0.45f * hs_stage_ramp((float)S.phase_time, 0, 2);
  }
  hs_finger_play_frame();
}

// Under the finger: the lattice wakes, a warm light moves with it, the glass sings by speed.
void hs_finger_play_frame(void) {
  bool taught = S.chapter == HS_CH_LATTICE;     // in free play the fingers strum, so the rest of the instrument keeps its own sounds
  float fastest = 0, pan = 0;
  for (int i = 0; i < 2; i++) {
    if (S.phase == HS_PHASE_PLAY && S.touching[i]) {
      S.scene.touch[i] = simd_make_float4(S.touch_x[i], S.touch_z[i], 1, S.touch_age[i]);
      if (S.touch_began[i]) hs_hands_hit(170, 0.018f, 0.16f, S.touch_x[i] / 0.0255f);
      if (S.touch_speed[i] > fastest) { fastest = S.touch_speed[i]; pan = S.touch_x[i] / 0.0255f; }
    } else if (S.phase != HS_PHASE_AWAKEN) {
      float strength = S.scene.touch[i].z * expf(-S.dt * 4);
      S.scene.touch[i].z = strength;
      S.scene.touch[i].w = fmaxf(S.scene.touch[i].w, 0) + S.dt;
    }
  }
  bool anyone = S.phase == HS_PHASE_PLAY && (S.touching[0] || S.touching[1]);
  float rate = fminf(1, fastest / 0.15f);
  // Deep green at rest, brightening toward mint the faster the finger goes. The same colour goes to the controller's bar and to the light under the dome.
  simd_float3 green = simd_make_float3(0.06f + 0.30f * rate, 0.72f + 0.28f * rate, 0.26f + 0.24f * rate);
  S.scene.touch_color += (green - S.scene.touch_color) * (1 - expf(-S.dt * 10));
  if (anyone) {
    int finger = S.touching[0] ? 0 : 1;
    S.mover = simd_make_float3(S.touch_x[finger], 0.008f, S.touch_z[finger]);
    S.mover_color = S.scene.touch_color;
    S.mover_power = 0.6f;
    S.mover_range = 0.06f;
  } else if (taught) {
    S.mover_power = 0;
  }
  if (taught) hs_hands_set(HS_LAYER_SHEEN, anyone ? 0.075f + rate * 0.015f : 0, 300 + rate * 500, pan);
  // A finger crossing a wire feels it, as a ridge in the glass.
  static int last_cell;
  if (taught && anyone) {
    int i = S.touching[0] ? 0 : 1;
    float dz = S.touch_z[i] - kHallCenterZ, r = hypotf(S.touch_x[i], dz), a = atan2f(dz, S.touch_x[i]);
    int cell = (int)(r / 0.005f) * 64 + (int)((a + (float)M_PI) / (2 * (float)M_PI / 12));
    if (cell != last_cell) {
      last_cell = cell;
      hs_hands_hit(175, 0.10f, 0.4f + rate * 0.6f, pan);
      hs_sound_strike(HS_MAT_SOUND_GLASS, 2100 + 300 * rate, 0.004f + rate * 0.006f, pan * 0.6f, HS_TIMING_DIRECT);
    }
  }
  if (taught) hs_sound_set(HS_SOUND_RUB, anyone ? 0.06f + rate * 0.5f : 0, rate);
  g_wake += ((anyone ? 1.0f : 0.15f) - g_wake) * (1 - expf(-S.dt * 3));
  S.scene.lattice_wake = g_wake * (S.phase == HS_PHASE_LEAVING ? 1 - hs_stage_ramp((float)S.phase_time, 0, 2) : 1);
  // Two fingers strike an arc between them: real sparks, in the air under the dome.
  if (S.phase == HS_PHASE_PLAY && S.touching[0] && S.touching[1]) {
    float y = 0.012f;
    float u = fmodf((float)S.seconds * 7.3f, 1.0f);
    simd_float3 at = simd_make_float3(S.touch_x[0] + (S.touch_x[1] - S.touch_x[0]) * u, y, S.touch_z[0] + (S.touch_z[1] - S.touch_z[0]) * u);
    hs_spawn(HS_KIND_EMBER, at, simd_make_float3(0, 0, 0), 0.0006f, 0.012f, simd_make_float3(1.0f, 0.55f, 0.22f) * 4, 0.00028f, 0.3f, 14);
    hs_sound_set(HS_SOUND_HISS, 0.25f, 0.8f);
  } else {
    hs_sound_set(HS_SOUND_HISS, 0, 0.8f);
  }
  // The inspector follows the finger, and the others glance over.
  if (S.touching[0]) hs_crew_set_attention(S.touch_x[0], S.touch_z[0], S.phase == HS_PHASE_PLAY ? 0.5f : 0);
  else hs_crew_set_attention(0, 0, 0);
}

static void lattice_exit(void) { hs_finger_play_end(); }

void hs_finger_play_end(void) {
  hs_hands_set(HS_LAYER_SHEEN, 0, 300, 0);
  hs_sound_set(HS_SOUND_RUB, 0, 0);
  hs_sound_set(HS_SOUND_HISS, 0, 0);
  S.scene.lattice_wake = 0;
  S.scene.touch[0] = S.scene.touch[1] = simd_make_float4(0, 0, 0, -1);
  S.env = 1;
  hs_crew_set_attention(0, 0, 0);
}

static bool lattice_press(uint32_t key) {
  if (S.phase != HS_PHASE_PLAY) return false;
  switch (key) {
    case HS_KEY_SOUTH: hs_go_to(HS_CH_BALANCE); return true;
    case HS_KEY_EAST: hs_go_to(HS_CH_GEARS); return true;
    case HS_KEY_WEST: {          // another bell
      static const float bells[5] = {880, 1046.5f, 1174.7f, 698.5f, 587.3f};
      static int at;
      at = (at + 1) % 5;
      g_bell_hz = bells[at];
      hs_sound_strike(HS_MAT_SOUND_BELL, g_bell_hz, 0.05f, 0.4f, HS_TIMING_DIRECT);
      hs_hands_hit(190, 0.03f, 0.2f, 0.6f);
      return true;
    }
    case HS_KEY_NORTH:           // the whole grid answers
      hs_fx_key(3);
      S.scene.touch[1] = simd_make_float4(0, kHallCenterZ, 0.5f, 0);
      return true;
    case HS_KEY_UP: case HS_KEY_DOWN: case HS_KEY_LEFT: case HS_KEY_RIGHT:
      g_trace = (g_trace + 1) % 3;
      hs_fx_cross(0, 0);
      S.scene.touch[0].z = 1;
      S.scene.touch[0].w = 0;
      return true;
    case HS_KEY_TOUCH: {
      float x = S.touching[0] ? S.touch_x[0] / 0.0255f : 0;
      hs_sound_strike(HS_MAT_SOUND_BELL, g_bell_hz * (1 + x * 0.06f), 0.09f, x * 0.5f, HS_TIMING_DIRECT);
      hs_hands_hit(105, 0.08f, 0.62f, x * 0.8f);
      if (S.touching[0]) S.scene.touch[0].w = 0;      // a ring runs out from the click
      return true;
    }
    default: return false;
  }
}

const hs_chapter_def hs_lattice_def = {4.0f, 2.5f, lattice_enter, lattice_frame, lattice_exit, lattice_press};

// ============================================================================ Balance
static bool g_inside;
static float g_weight_at, g_weight_speed;
static float g_inside_mix;
static simd_float3 g_walker;
static float g_walker_yaw, g_walker_pitch;
static float g_gather_until;
static float g_shake;
static const float kBalanceStations[HS_CREW][2] = {{-15, 12}, {19.5f, -6}, {-19.5f, -6}, {0, 19.5f}, {0, 3}, {15, 12}};

static void balance_enter(void) {
  S.env = 1;      // coming back from the plate as much as coming on from the lattice
  set_stations(kBalanceStations, HS_JOB_IDLE);
  hs_set_shot(simd_make_float3(0, -0.002f, -0.006f), simd_make_float3(0.0f, 0.165f, 0.135f), 30, true);
  S.camera_speed = 1.5f;
  hs_sound_score(0, HS_STEM_BALANCE_BED, 1.0f, 2.5f, true);
  hs_sound_score(1, HS_STEM_BALANCE_MOTION, 1.0f, 2.5f, true);
  hs_live_set_world_gain(0.4f, 0);
  g_weight_at = g_weight_speed = 0;
  hs_hands_silence();
  S.filament = 1.0f;
  g_inside = false;
  g_inside_mix = 0;
  g_gather_until = 0;
  const hs_trigger_effect soft = hs_trigger_spring(1, 9, 1, 3);
  hs_fx_set_triggers(&soft, &soft);
}

static void balance_frame(void) {
  float t = (float)S.phase_time;
  if (S.phase == HS_PHASE_AWAKEN) {
    static float last = 0;
    if (t < last) last = 0;
    if (last < 1.0f && t >= 1.0f) hs_hands_hit(70, 0.5f, 0.3f, 0);
    last = t;
  }
  // Shaking raises dust, and hands the crew to the forces.
  float turning = simd_length(S.hand_turn);
  g_shake += (fminf(1, turning / 8) - g_shake) * (1 - expf(-S.dt * 6));
  if (g_shake > 0.25f) {
    simd_float3 at = simd_make_float3(0, -0.004f, kHallCenterZ);
    hs_spawn(HS_KIND_DUST, at, simd_make_float3(0, 0.012f, 0), 0.016f, 0.02f, simd_make_float3(0.85f, 0.62f, 0.42f) * 0.5f * g_shake, 0.00030f, 1.6f, (int)(40 * g_shake));
  }
  // A heavy weight loose inside the body. Tip the controller and you feel it set off, run across from one hand to the other, knock against the end
  // and lie still; hold it level and there is nothing at all. Direction is what this chapter is about, so nothing hums underneath it.
  simd_float3 down = simd_act(simd_inverse(S.hand), simd_make_float3(0, -1, 0));
  float slope = fabsf(down.x) < 0.05f ? 0 : down.x;
  g_weight_speed += (slope * 7.0f - S.hand_turn.z * 0.9f - g_weight_speed * 2.2f) * S.dt;
  g_weight_at += g_weight_speed * S.dt;
  if (fabsf(g_weight_at) >= 1) {
    float side = g_weight_at > 0 ? 1.0f : -1.0f;
    g_weight_at = side;
    if (g_weight_speed * side > 0.2f) hs_hands_hit(52, 0.3f, fminf(1, 0.45f + fabsf(g_weight_speed) * 0.4f), side);
    if (g_weight_speed * side > 0) g_weight_speed = -g_weight_speed * 0.15f;
  }
  float rolling = fminf(0.55f, g_weight_speed * g_weight_speed * 0.7f);
  if (rolling < 0.02f) rolling = 0;
  // Which hand it is in, and a lower note as it runs toward you, a higher one away.
  float tone = 62 + 16 * fmaxf(-1, fminf(1, down.z));
  hs_hands_set(HS_LAYER_BODY_LEFT, rolling * (1 - g_weight_at) * 0.5f, tone, 0);
  hs_hands_set(HS_LAYER_BODY_RIGHT, rolling * (1 + g_weight_at) * 0.5f, tone, 0);
  // Light moves with the lamplighter; the chapter's mover is the far rim.
  S.mover_power = 0;
  // A call from the touch surface brings them over, and it ends with the touch.
  if (S.touching[0] && S.phase == HS_PHASE_PLAY && !g_inside) hs_crew_set_attention(S.touch_x[0], S.touch_z[0], 0.6f);
  else if ((float)S.seconds < g_gather_until) hs_crew_set_attention(0, kHallCenterZ, 0.6f);
  else hs_crew_set_attention(0, 0, 0);

  // Inside, you stand among them. The picture never cuts: the camera flies in through the dome.
  g_inside_mix += ((g_inside ? 1.0f : 0.0f) - g_inside_mix) * (1 - expf(-S.dt * 1.8f));
  // Once through the wall there is no wall: seen from this close and this low it would only cut lines across the picture.
  S.hide_clear = g_inside_mix > 0.55f;
  S.inside = g_inside;
  if (g_inside) {
    float speed = S.r2 > 0.2f ? 0.040f : S.l2 > 0.2f ? 0.010f : 0.022f;
    g_walker_yaw -= S.rx * 1.1f * S.dt;
    g_walker_pitch = fmaxf(-0.8f, fminf(0.5f, g_walker_pitch - S.ry * 0.8f * S.dt));
    float c = cosf(g_walker_yaw), s = sinf(g_walker_yaw);
    g_walker.x += (c * S.lx + s * S.ly) * speed * S.dt;
    g_walker.z += (-s * S.lx + c * S.ly) * speed * S.dt;
    float dz = g_walker.z - kHallCenterZ, r = hypotf(g_walker.x, dz);
    if (r > 0.019f) { g_walker.x *= 0.019f / r; g_walker.z = kHallCenterZ + dz * 0.019f / r; }
    // Your footsteps, by distance walked.
    static float stride;
    stride += hypotf(S.lx, S.ly) * speed * S.dt;
    if (stride > 0.006f) {
      stride = 0;
      hs_hands_hit(68, 0.05f, 0.34f, g_walker.x / 0.026f);
      hs_sound_strike(HS_MAT_SOUND_WOOD, 240, 0.05f, 0, HS_TIMING_ALIGNED);
    }
    // Keeps its distance from the crew: at this scale, walking through someone fills the whole picture.
    for (int who = 0; who < HS_CREW; who++) {
      float p[3], q[4], v[3];
      hs_world_pose(who, p, q, v);
      float dx = g_walker.x - p[0], dz2 = g_walker.z - p[2], d = hypotf(dx, dz2);
      if (d < 0.013f && d > 1e-5f) { g_walker.x = p[0] + dx * 0.013f / d; g_walker.z = p[2] + dz2 * 0.013f / d; }
    }
    // Eye well above the crew's heads, looking a little down at them: a place to stand and watch, not a lens on the floor.
    simd_float3 eye_at = simd_make_float3(g_walker.x, 0.0085f, g_walker.z);
    simd_float3 look = simd_make_float3(-s * cosf(g_walker_pitch), sinf(g_walker_pitch), -c * cosf(g_walker_pitch));
    hs_set_shot(eye_at + look * 0.01f, -look * 0.01f, 52, false);
    S.camera_speed = 5;
  } else if (S.phase == HS_PHASE_LEAVING) {
    hs_set_shot(simd_make_float3(0, 0, -0.010f), simd_make_float3(0, 0.078f, 0.036f), 26, false);
    S.camera_speed = 1.4f;
  } else {
    hs_set_shot(simd_make_float3(0, -0.002f, -0.006f), simd_make_float3(0.0f, 0.165f, 0.135f), 30, true);
    S.camera_speed = 2.5f;
  }
}

static void balance_exit(void) {
  hs_hands_set(HS_LAYER_BODY_LEFT, 0, 60, 0);
  hs_hands_set(HS_LAYER_BODY_RIGHT, 0, 60, 0);
  S.hide_clear = false;
  S.inside = false;
  hs_crew_set_attention(0, 0, 0);
  hs_hands_set(HS_LAYER_SWELL, 0, 3, 0);
}

static bool balance_press(uint32_t key) {
  if (S.phase != HS_PHASE_PLAY) return false;
  if (g_inside) {
    switch (key) {
      case HS_KEY_SOUTH: hs_hands_hit(60, 0.08f, 0.5f, 0); hs_sound_strike(HS_MAT_SOUND_WOOD, 200, 0.1f, 0, HS_TIMING_DIRECT); return true;   // a hop
      case HS_KEY_EAST: case HS_KEY_TOUCH: g_inside = false; hs_hands_hit(50, 0.2f, 0.4f, 0); return true;
      case HS_KEY_WEST: hs_crew_cheer(HS_PIP); hs_fx_key(0); return true;
      default: return false;
    }
  }
  switch (key) {
    case HS_KEY_SOUTH: hs_go_to(HS_CH_BREATH); return true;
    case HS_KEY_EAST: hs_go_to(HS_CH_LATTICE); return true;
    case HS_KEY_WEST:            // Bo jumps
      hs_fx_key(0);
      hs_world_nudge(HS_BO, 0, 0.11f, 0);
      hs_crew_cheer(HS_BO);
      return true;
    case HS_KEY_NORTH:           // everyone gathers, and spreads out again by itself
      hs_fx_key(3);
      g_gather_until = (float)S.seconds + 3.0f;
      return true;
    case HS_KEY_UP: case HS_KEY_DOWN: case HS_KEY_LEFT: case HS_KEY_RIGHT: {     // send the crew that way
      float dx = key == HS_KEY_RIGHT ? 1.0f : key == HS_KEY_LEFT ? -1.0f : 0, dz = key == HS_KEY_DOWN ? 1.0f : key == HS_KEY_UP ? -1.0f : 0;
      hs_fx_cross(dx, dz);
      float moved[HS_CREW][2];
      for (int i = 0; i < HS_CREW; i++) { moved[i][0] = kBalanceStations[i][0] + dx * 6; moved[i][1] = kBalanceStations[i][1] + dz * 6; }
      set_stations(moved, HS_JOB_IDLE);
      return true;
    }
    case HS_KEY_L3: hs_fx_stick_press(0); set_stations(kBalanceStations, HS_JOB_IDLE); return true;
    case HS_KEY_R3: hs_fx_stick_press(1); S.closer_want = S.closer_want > 0.5f ? 0 : 1; return true;
    case HS_KEY_TOUCH:           // going inside is always your own choice
      hs_fx_dome_click(0);
      g_inside = true;
      g_walker = simd_make_float3(0, 0, kHallCenterZ + 0.019f);
      g_walker_yaw = 0;
      g_walker_pitch = -0.28f;
      return true;
    default: return false;
  }
}


const hs_chapter_def hs_balance_def = {3.0f, 2.5f, balance_enter, balance_frame, balance_exit, balance_press};
