// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Coils: two voice coils seen and felt, and four surfaces you can tell apart with your eyes closed.
// Gears: the real trigger motors at their ceiling, with a gear train that turns one to one and a wall that hits.
#include "experience/director.h"
#include "experience/wall_predict.h"
#include "hands/hands.h"
#include "sound/sound.h"
#include "world/crew.h"
#include "world/world.h"
#include <math.h>
#include <stdatomic.h>

static void score_two(int bed, float bed_gain, int top, float top_gain, float seconds) {
  hs_sound_score(0, bed, bed_gain, seconds, true);
  if (top >= 0) hs_sound_score(1, top, top_gain, seconds, true);
  else hs_sound_score_gain(1, 0, seconds);
}

static void set_stations(const float where[HS_CREW][2], hs_crew_job job) {
  hs_crew_station s[HS_CREW];
  for (int i = 0; i < HS_CREW; i++) s[i] = (hs_crew_station){where[i][0] * 0.001f, kHallCenterZ + where[i][1] * 0.001f, job};
  hs_crew_set_stations(s);
}

// ============================================================================ Coils
typedef enum { kGlass, kGravel, kWater, kWood, kLid } Surface;

static Surface g_surface;
static float g_seam_distance;
static float g_last_ball[3];

static Surface surface_at(float x, float z) {
  float dz = z - kHallCenterZ, r = hypotf(x, dz);
  if (r < 0.012f) return kLid;
  float a = atan2f(dz, x);
  if (a < 0) a += 2 * (float)M_PI;
  return (Surface)((int)(a / ((float)M_PI / 2)) % 4);
}

static void coils_enter(void) {
  static const float stations[HS_CREW][2] = {{-15, 12}, {15, 12}, {-19.5f, -6}, {19.5f, -6}, {0, 3}, {0, 19.5f}};
  set_stations(stations, HS_JOB_WORK);
  hs_set_shot(simd_make_float3(-0.062f, -0.012f, 0.012f), simd_make_float3(-0.070f, 0.110f, 0.130f), 27, false);
  S.camera_speed = 1.7f;
  score_two(HS_STEM_COILS_BED, 1.0f, HS_STEM_COILS_TOP, 0.9f, 3.0f);
  hs_world_set_ball_active(true);
  hs_world_set_drag(HS_WORLD_BALL, 0);
  hs_live_set_world_gain(0.35f, 1);
  hs_hands_silence();
  S.env = 1;
  S.backdrop = 0.85f;
  S.filament = 1.0f;
  S.awake = 1;
  hs_crew_set_awake(1);
  S.mover_power = 0;
  S.hand_weight = 1;
  g_seam_distance = 0;
}

static void coils_frame(void) {
  float t = (float)S.phase_time;
  if (S.phase == HS_PHASE_AWAKEN) {
    // A hard low beat in one hand, while the coil on that side is seen to jump: then the other. Only then the bench.
    static float last = 0;
    if (t < last) last = 0;
    const float beats[6] = {0.5f, 1.1f, 1.7f, 3.0f, 3.6f, 4.2f};
    for (int i = 0; i < 6; i++) {
      if (last < beats[i] && t >= beats[i]) {
        float pan = i < 3 ? -1.0f : 1.0f;
        hs_hands_hit(58, 0.18f, 0.9f, pan);
        hs_sound_strike(HS_MAT_SOUND_PAPER, 80, 0.22f, pan * 0.6f, HS_TIMING_ALIGNED);
      }
    }
    if (last < 5.2f && t >= 5.2f) {
      hs_world_place(HS_WORLD_BALL, 0.014f, 0.003f, -0.002f);
      hs_sound_strike(HS_MAT_SOUND_STEEL, 2100, 0.06f, 0.2f, HS_TIMING_ALIGNED);
      hs_hands_hit(120, 0.05f, 0.6f, 0.2f);
    }
    last = t;
    // The coil hangs under the plate, so it is seen from below, through the clear body. Each blow lights it from inside, and the bellows above it pump with it.
    float beat_left, beat_right;
    hs_hands_view(&beat_left, &beat_right);
    if (t < 5) {
      S.mover = simd_make_float3(t < 2.5f ? -0.062f : 0.062f, -0.0156f, 0.012f);
      S.mover_color = simd_make_float3(1.0f, 0.5f, 0.2f);
      S.mover_range = 0.035f;
      float blow = fminf(1, (t < 2.5f ? beat_left : beat_right) * 2.2f);
      S.mover_power = blow > 0.02f ? blow * 1.6f : 0;       // between blows it costs nothing at all
      for (int side = 0; side < 2; side++) S.bellows[side] += (fmaxf(0, S.scene.coil[side]) * 0.8f - S.bellows[side]) * (1 - expf(-S.dt * 30));
    } else {
      S.mover_power = 0;
      S.bellows[0] = S.bellows[1] = 0;
    }
    if (t < 2.5f) hs_set_shot(simd_make_float3(-0.062f, -0.0156f, 0.012f), simd_make_float3(-0.060f, -0.060f, 0.105f), 24, false);
    else if (t < 5) hs_set_shot(simd_make_float3(0.062f, -0.0156f, 0.012f), simd_make_float3(0.060f, -0.060f, 0.105f), 24, false);
    else hs_set_shot(simd_make_float3(0, -0.004f, -0.008f), simd_make_float3(0, 0.135f, 0.105f), 30, true);
    S.camera_speed = t < 5 ? 1.7f : 3.0f;     // up from under the plate in good time: play starts with the whole instrument in the picture
  } else if (S.phase == HS_PHASE_PLAY) {
    hs_set_shot(simd_make_float3(0, -0.004f, -0.008f), simd_make_float3(0, 0.135f, 0.105f), 30, true);
    S.camera_speed = 3;
    const hs_trigger_effect brake = hs_trigger_spring(1, 9, 2, 5);
    if (S.phase_time < S.dt * 1.5) hs_fx_set_triggers(&brake, &brake);
  } else {
    hs_set_shot(simd_make_float3(0.066f, -0.010f, -0.020f), simd_make_float3(0.14f, 0.09f, -0.14f), 30, true);
    S.camera_speed = 1.5f;
  }
  hs_ball_play_frame();
}

// The ball tells what it rolls on.
void hs_ball_play_frame(void) {
  float p[3], q[4], v[3];
  hs_world_pose(HS_WORLD_BALL, p, q, v);
  float speed = sqrtf(v[0] * v[0] + v[2] * v[2]);
  float pan = fmaxf(-1, fminf(1, p[0] / 0.026f));
  g_surface = surface_at(p[0], p[2]);
  S.ball_surface = (int)g_surface;
  float moving = hs_stage_ramp(speed, 0.0008f, 0.004f) * (S.phase == HS_PHASE_PLAY ? 1.0f : hs_stage_ramp((float)S.phase_time, 5.3f, 6.0f) * (S.phase == HS_PHASE_AWAKEN ? 1.0f : 0.0f));
  float strength = fminf(1, speed / 0.04f);
  float on_glass = g_surface == kGlass || g_surface == kLid ? 1.0f : 0.0f, on_gravel = g_surface == kGravel ? 1.0f : 0.0f, on_water = g_surface == kWater ? 1.0f : 0.0f;
  // In free play the ball is one voice among many: under the notes and the blows, never a wash over them.
  float share = S.chapter == HS_CH_COILS ? 1.0f : 0.45f;
  hs_hands_set(HS_LAYER_SHEEN, S.chapter == HS_CH_COILS ? on_glass * moving * (0.09f + strength * 0.06f) : 0, 620, pan);     // glass: almost nothing, and that is the point
  hs_hands_set(HS_LAYER_GRAVEL, share * on_gravel * moving * (0.28f + strength * 0.6f), 60 + strength * 900, pan);
  hs_hands_set(HS_LAYER_SWELL, share * on_water * moving * (0.26f + strength * 0.5f), 2.4f + strength * 4, pan);
  hs_world_set_drag(HS_WORLD_BALL, on_water > 0 ? 2.2f : 0);
  hs_sound_set(HS_SOUND_ROLL, moving * (0.06f + 0.16f * strength) * (on_glass > 0 ? 0.35f : 1.0f), on_gravel > 0 ? 0.9f : on_water > 0 ? 0.1f : 0.5f);
  hs_sound_set(HS_SOUND_RUB, on_glass * moving * strength * 0.25f, 0.8f);
  // Wood is felt at its joints, a tick at each one.
  g_seam_distance += speed * S.dt;
  if (g_surface == kWood && g_seam_distance > 0.0028f && moving > 0.3f) {
    g_seam_distance = 0;
    hs_hands_hit(160, 0.02f, 0.3f + strength * 0.5f, pan);
    hs_sound_strike(HS_MAT_SOUND_WOOD, 620, 0.03f + strength * 0.05f, pan * 0.7f, HS_TIMING_ALIGNED);
  }
  if (g_surface == kWater && moving > 0.2f && fmodf((float)S.seconds, 0.25f) < S.dt) {
    S.scene.ripple[(int)(S.seconds * 4) % 4] = simd_make_float4(p[0], p[2], strength * 0.8f + 0.2f, 0);
    if (((int)(S.seconds * 13) % 5) == 0) {     // a bubble
      hs_hands_hit(55 + 40 * strength, 0.06f, 0.22f, pan);
      hs_sound_strike(HS_MAT_SOUND_GLASS, 520 + 300 * strength, 0.012f, pan * 0.5f, HS_TIMING_ALIGNED);
    }
  }
  for (int i = 0; i < 4; i++) S.scene.ripple[i].w += S.dt;
  // Each side's pedal brakes the ball while it is on that side, and you feel it through the pedal.
  float brake = p[0] < 0 ? S.l2 : S.r2;
  if (brake > 0.05f) hs_world_set_drag(HS_WORLD_BALL, on_water > 0 ? 2.2f + brake * 10 : brake * 12);
  // The touch surface pulls the ball.
  if (S.chapter == HS_CH_COILS && S.touching[0] && S.phase == HS_PHASE_PLAY) {
    float dx = S.touch_x[0] - p[0], dz = S.touch_z[0] - p[2];
    hs_world_nudge(HS_WORLD_BALL, dx * 0.14f, 0, dz * 0.14f);
  }
  g_last_ball[0] = p[0];
  g_last_ball[1] = p[1];
  g_last_ball[2] = p[2];
}

static void coils_exit(void) {
  for (int i = 0; i < HS_LAYER_COUNT; i++) hs_hands_set((hs_hand_layer)i, 0, 60, 0);
  hs_sound_set(HS_SOUND_ROLL, 0, 0.5f);
  hs_sound_set(HS_SOUND_RUB, 0, 0.5f);
  hs_world_set_drag(HS_WORLD_BALL, 0);
  S.bellows[0] = S.bellows[1] = 0;
}

static bool coils_press(uint32_t key) {
  if (S.phase != HS_PHASE_PLAY) return false;
  float p[3], q[4], v[3];
  hs_world_pose(HS_WORLD_BALL, p, q, v);
  switch (key) {
    case HS_KEY_SOUTH: hs_go_to(HS_CH_GEARS); return true;
    case HS_KEY_NORTH: {         // on to the next surface
      hs_fx_key(3);
      static const float centres[4][2] = {{0.013f, 0.013f}, {-0.013f, 0.013f}, {-0.016f, -0.013f}, {0.016f, -0.013f}};
      int next = (g_surface == kLid ? 0 : ((int)g_surface + 1) % 4);
      hs_world_place(HS_WORLD_BALL, centres[next][0], -0.002f, kHallCenterZ + centres[next][1]);
      return true;
    }
    case HS_KEY_UP: hs_fx_cross(0, -1); hs_world_nudge(HS_WORLD_BALL, 0, 0, -0.05f); return true;
    case HS_KEY_DOWN: hs_fx_cross(0, 1); hs_world_nudge(HS_WORLD_BALL, 0, 0, 0.05f); return true;
    case HS_KEY_LEFT: case HS_KEY_L1: hs_fx_shoulder(0); hs_world_nudge(HS_WORLD_BALL, -0.05f, 0, 0); return true;
    case HS_KEY_RIGHT: case HS_KEY_R1: hs_fx_shoulder(1); hs_world_nudge(HS_WORLD_BALL, 0.05f, 0, 0); return true;
    case HS_KEY_TOUCH: hs_fx_dome_click(p[0] / 0.026f); hs_world_nudge(HS_WORLD_BALL, 0, 0.09f, 0); return true;
    default: return false;
  }
}

const hs_chapter_def hs_coils_def = {8.0f, 2.5f, coils_enter, coils_frame, coils_exit, coils_press};

// ============================================================================ Gears
typedef struct {
  float position, speed, turn, spin, load, heat;
  bool at_wall, predicted, pressed_in;
  hs_wall_predictor predictor;
  float last_tooth;
  double wall_since;
} Pedal;

static Pedal g_pedal[2];
// The blow is decided on the input thread, at every report, because a frame can be 17 ms too late to catch it.
static _Atomic bool g_watch_walls, g_input_fired[2];
static hs_wall_predictor g_input_predictor[2];

void hs_gears_on_input(const hs_input *in, float seconds) {
  if (!atomic_load_explicit(&g_watch_walls, memory_order_relaxed)) return;
  for (int side = 0; side < 2; side++) {
    float value = (float)(side ? in->right_trigger : in->left_trigger) / 255.0f;
    bool coming = hs_wall_will_hit(&g_input_predictor[side], value, seconds, 0.985f, 0.105f);
    if (value < 0.7f) atomic_store_explicit(&g_input_fired[side], false, memory_order_relaxed);
    if (coming && !atomic_load_explicit(&g_input_fired[side], memory_order_relaxed)) {
      atomic_store_explicit(&g_input_fired[side], true, memory_order_relaxed);
      float pan = side ? 0.9f : -0.9f, speed = g_input_predictor[side].speed;
      hs_hands_hit_now(46, 0.14f, fminf(1, 0.6f + speed * 0.12f), pan);
      hs_sound_strike_with_hand(HS_MAT_SOUND_WOOD, 110, 0.5f, pan * 0.5f);
      hs_sound_strike_with_hand(HS_MAT_SOUND_STEEL, 640, 0.12f, pan * 0.5f);
    }
  }
}
static float g_gauge;
static bool g_both_sounded;

static void gears_enter(void) {
  static const float stations[HS_CREW][2] = {{0, 19.5f}, {19.5f, -6}, {-19.5f, -6}, {-15, 12}, {0, 3}, {15, 12}};
  set_stations(stations, HS_JOB_WORK);
  hs_set_shot(simd_make_float3(0.070f, -0.012f, -0.022f), simd_make_float3(0.11f, 0.075f, -0.175f), 28, false);
  S.camera_speed = 1.5f;
  score_two(HS_STEM_GEARS_BED, 1.0f, HS_STEM_GEARS_MOTION, 1.0f, 2.5f);
  hs_live_set_world_gain(0.15f, 0);
  hs_hands_silence();
  hs_pedals_play_begin();
}

void hs_pedals_play_begin(void) {
  for (int i = 0; i < 2; i++) g_pedal[i] = (Pedal){0};
  g_gauge = 0;
  g_both_sounded = false;
  atomic_store(&g_input_fired[0], false);
  atomic_store(&g_input_fired[1], false);
  atomic_store(&g_watch_walls, true);
  S.mover_power = 0;
  S.mover_color = simd_make_float3(1.0f, 0.42f, 0.14f);
  S.mover_range = 0.22f;
}

static void pedal_frame(int side, float value, float speed) {
  Pedal *p = &g_pedal[side];
  float pan = side ? 0.9f : -0.9f;
  const float wall = 0.985f;
  hs_hand_layer body = side ? HS_LAYER_BODY_RIGHT : HS_LAYER_BODY_LEFT;
  // First touch: the backlash closes with the finest tick.
  if (!p->pressed_in && value > 0.012f) {
    p->pressed_in = true;
    hs_sound_strike(HS_MAT_SOUND_STEEL, 3300, 0.018f, pan * 0.7f, HS_TIMING_DIRECT);
  }
  if (p->pressed_in && value < 0.006f) p->pressed_in = false;
  // Under load: pitch and pressure follow the travel, and the hand gets a low body that grows with the pull.
  float load = value > 0.02f ? value : 0;
  p->load += (load - p->load) * (1 - expf(-S.dt * 30));
  hs_hands_set(body, p->at_wall ? 0.05f : load * load * load * 0.42f + (load > 0 ? 0.012f : 0), 42 + 32 * load, 0);
  // The wall is known 110 ms ahead from how fast the finger is coming, so the blow lands as the pedal stops.
  if (S.real_time) {
    if (atomic_load_explicit(&g_input_fired[side], memory_order_relaxed)) p->predicted = true;
  } else if (hs_wall_will_hit(&p->predictor, value, S.dt, wall, 0.105f) && !p->predicted && !p->at_wall) {
    p->predicted = true;
    hs_hands_hit(46, 0.14f, fminf(1, 0.6f + speed * 0.12f), pan);
    hs_sound_strike(HS_MAT_SOUND_WOOD, 110, 0.5f, pan * 0.5f, HS_TIMING_ALIGNED);
    hs_sound_strike(HS_MAT_SOUND_STEEL, 640, 0.12f, pan * 0.5f, HS_TIMING_ALIGNED);
  }
  if (!p->at_wall && value >= wall) {
    p->at_wall = true;
    p->wall_since = S.seconds;
    if (!p->predicted) {        // a slow arrival: a smaller, firm stop
      hs_hands_hit(62, 0.1f, 0.6f, pan);
      hs_sound_strike(HS_MAT_SOUND_WOOD, 150, 0.25f, pan * 0.5f, HS_TIMING_DIRECT);
    }
    p->predicted = true;
  }
  if (p->at_wall && value < wall - 0.02f) p->at_wall = false;
  if (value < 0.7f) p->predicted = false;
  // Sudden release: the spring throws the train free, and it runs down on its own.
  if (speed < -3.5f && p->spin >= -0.5f) {
    p->spin = fminf(speed * 1.1f, -3.0f);
    hs_hands_hit(82, 0.16f, 0.8f, pan);
    hs_sound_strike(HS_MAT_SOUND_BRASS, 240, 0.22f, pan * 0.6f, HS_TIMING_DIRECT);
    for (int who = 0; who < HS_CREW; who++) hs_world_nudge(who, side ? 0.015f : -0.015f, 0.012f, 0);    // the crew stagger
  }
  p->spin *= expf(-S.dt * 1.8f);
  // The gear follows the pedal one to one; the free spin is added on top and dies away.
  float turn = value;
  p->turn = turn + p->spin * 0.06f;
  // A tooth passing: heard and felt on the way back, each one its own tick.
  float tooth = floorf(p->turn * 24);
  if (tooth != p->last_tooth) {
    float abs_speed = fabsf(speed);
    bool coming_back = speed < -0.02f;
    if (coming_back || abs_speed > 2.5f) {
      hs_sound_strike(HS_MAT_SOUND_STEEL, 2500 + 200 * (float)((int)tooth % 3), 0.02f + fminf(0.03f, abs_speed * 0.012f), pan * 0.6f, HS_TIMING_DIRECT);
      hs_hands_hit(170, 0.012f, 0.12f + fminf(0.3f, abs_speed * 0.1f), pan);
    }
    p->last_tooth = tooth;
  }
  // Heat: only what really works hard glows.
  float want_heat = p->at_wall ? 1.0f : load * 0.3f;
  p->heat += (want_heat - p->heat) * (1 - expf(-S.dt * (want_heat > p->heat ? 1.6f : 0.8f)));
  if (p->at_wall && S.seconds - p->wall_since < 2.5) {
    simd_float3 valve = hs_socket(side ? "socket_valve_r" : "socket_valve_l");
    valve.y += S.bellows[side] * 0.003f;
    float wall_held = (float)(S.seconds - p->wall_since);
    hs_spawn(HS_KIND_STEAM, valve, simd_make_float3(0, 0.040f, 0), 0.0012f, 0.012f, simd_make_float3(1.0f, 0.86f, 0.74f) * (0.35f - wall_held * 0.1f), 0.0030f, 1.1f, 7);
    if (wall_held < 0.15f) hs_spawn(HS_KIND_EMBER, valve, simd_make_float3(0, 0.03f, 0), 0.0015f, 0.03f, simd_make_float3(1.0f, 0.5f, 0.15f) * 3, 0.00028f, 0.8f, 24);
  }
}

static void gears_frame(void) {
  if (S.phase == HS_PHASE_AWAKEN) {
    float t = (float)S.phase_time;
    static float last = 0;
    if (t < last) last = 0;
    if (last < 0.8f && t >= 0.8f) { hs_hands_hit(170, 0.03f, 0.5f, -0.9f); hs_sound_strike(HS_MAT_SOUND_STEEL, 1400, 0.05f, -0.7f, HS_TIMING_ALIGNED); }
    if (last < 1.6f && t >= 1.6f) { hs_hands_hit(170, 0.03f, 0.5f, 0.9f); hs_sound_strike(HS_MAT_SOUND_STEEL, 1600, 0.05f, 0.7f, HS_TIMING_ALIGNED); }
    if (last < 0.15f && t >= 0.15f) {
      // From the first millimetre, as hard as the firmware goes. A higher ceiling would take modified firmware, which is not done.
      const hs_trigger_effect full = hs_trigger_raw(255);
      hs_fx_set_triggers(&full, &full);
    }
    last = t;
    S.camera_speed = 1.5f;
    hs_set_shot(simd_make_float3(0.070f, -0.012f, -0.022f), simd_make_float3(0.11f, 0.075f, -0.175f), 28, false);
  } else if (S.phase == HS_PHASE_PLAY) {
    hs_set_shot(simd_make_float3(0.0f, -0.006f, -0.015f), simd_make_float3(0.11f, 0.11f, -0.20f), 31, true);
    S.camera_speed = 2.5f;
  } else {
    hs_set_shot(simd_make_float3(0, 0, -0.010f), simd_make_float3(0, 0.145f, 0.02f), 30, false);
    S.camera_speed = 1.4f;
  }
  hs_pedals_play_frame();
}

// Both pedals, whole: the gear train, the wall, the steam and the heat. The chapter that teaches them and free play share it.
void hs_pedals_play_frame(void) {
  bool live = S.phase != HS_PHASE_LEAVING;
  pedal_frame(0, live ? S.l2 : 0, S.l2_speed);
  pedal_frame(1, live ? S.r2 : 0, S.r2_speed);
  S.scene.gear_turn[0] = 0;
  S.gear_turn[0] = g_pedal[0].turn;
  S.gear_turn[1] = g_pedal[1].turn;
  S.bellows[0] += (S.l2 - S.bellows[0]) * (1 - expf(-S.dt * 18));
  S.bellows[1] += (S.r2 - S.bellows[1]) * (1 - expf(-S.dt * 18));
  // Both at the wall: the dial fills, and a chord answers.
  bool both = g_pedal[0].at_wall && g_pedal[1].at_wall;
  float pull = (S.l2 + S.r2) * 0.5f;
  float want = both ? 1.0f : pull * 0.72f;
  g_gauge += (want - g_gauge) * (1 - expf(-S.dt * (want > g_gauge ? 1.6f : 2.5f)));
  S.needle = g_gauge;
  if (both && !g_both_sounded) {
    g_both_sounded = true;
    hs_sound_score(3, HS_STEM_STOP_UP, 1.0f, 0.01f, false);
    hs_hands_hit(50, 0.5f, 0.9f, 0);
  }
  if (!both) g_both_sounded = false;
  float load_sound = fmaxf(g_pedal[0].load, g_pedal[1].load);
  hs_sound_set(HS_SOUND_LOAD, load_sound * 0.09f, load_sound);
  hs_sound_set(HS_SOUND_BELLOWS, (fabsf(S.l2_speed) + fabsf(S.r2_speed)) * 0.05f, 0.4f);
  float steam = fmaxf(g_pedal[0].at_wall ? 1.0f : 0.0f, g_pedal[1].at_wall ? 1.0f : 0.0f);
  hs_sound_set(HS_SOUND_STEAM, steam * 0.09f, 0.6f);
  // The ember under load is the chapter's light, falling on the shell from inside.
  float heat = fmaxf(g_pedal[0].heat, g_pedal[1].heat);
  int hot = g_pedal[1].heat > g_pedal[0].heat;
  simd_float3 valve = hs_socket(hot ? "socket_valve_r" : "socket_valve_l");
  S.mover = valve;
  S.mover_power = heat * 1.2f;
  S.mover_color = simd_make_float3(1.0f, 0.42f, 0.14f);
  S.mover_range = 0.22f;
  S.filament = 1.0f + heat * 1.0f;
}

static void gears_exit(void) { hs_pedals_play_end(); }

void hs_pedals_play_end(void) {
  atomic_store(&g_watch_walls, false);
  hs_hands_set(HS_LAYER_BODY_LEFT, 0, 60, 0);
  hs_hands_set(HS_LAYER_BODY_RIGHT, 0, 60, 0);
  hs_sound_set(HS_SOUND_LOAD, 0, 0);
  hs_sound_set(HS_SOUND_BELLOWS, 0, 0);
  hs_sound_set(HS_SOUND_STEAM, 0, 0);
  S.gear_turn[0] = S.gear_turn[1] = 0;
  S.bellows[0] = S.bellows[1] = 0;
  S.needle = 0;
  S.mover_power = 0;
}

static bool gears_press(uint32_t key) {
  if (S.phase != HS_PHASE_PLAY) return false;
  switch (key) {
    case HS_KEY_SOUTH: hs_go_to(HS_CH_LATTICE); return true;
    case HS_KEY_EAST: hs_go_to(HS_CH_COILS); return true;
    case HS_KEY_WEST: case HS_KEY_NORTH:       // the crew wave back
      hs_fx_key(key == HS_KEY_WEST ? 0 : 3);
      hs_crew_cheer(HS_PIP);
      hs_crew_cheer(HS_TAM);
      return true;
    case HS_KEY_UP: case HS_KEY_DOWN: case HS_KEY_LEFT: case HS_KEY_RIGHT:
      hs_fx_cross(key == HS_KEY_RIGHT ? 1.0f : key == HS_KEY_LEFT ? -1.0f : 0, 0);
      hs_crew_cheer(HS_MOX);
      return true;
    default: return false;
  }
}

const hs_chapter_def hs_gears_def = {4.0f, 3.0f, gears_enter, gears_frame, gears_exit, gears_press};
