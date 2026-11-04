// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Finale: everything at once, one last chord, and silence. Free play: everything at once, and nothing that sends you away.
// Listening room: the instrument, full screen, showing what your music is doing in your hands.
#include "experience/director.h"
#include "hands/hands.h"
#include "listen/listen.h"
#include "sound/sound.h"
#include "world/crew.h"
#include "world/world.h"
#include <math.h>
#include <stdlib.h>

static void set_stations(const float where[HS_CREW][2], hs_crew_job job) {
  hs_crew_station s[HS_CREW];
  for (int i = 0; i < HS_CREW; i++) s[i] = (hs_crew_station){where[i][0] * 0.001f, kHallCenterZ + where[i][1] * 0.001f, job};
  hs_crew_set_stations(s);
}

// ============================================================================ Finale
static float g_finale_last;

static void finale_enter(void) {
  static const float stations[HS_CREW][2] = {{-17, 9}, {17, 9}, {-19.5f, -6}, {19.5f, -6}, {-7, 19}, {7, 19}};
  set_stations(stations, HS_JOB_WORK);
  hs_sound_score(0, HS_STEM_FINALE, 1.3f, 0.05f, false);     // the top of the arc: the one place the music stands fully in front
  hs_sound_score_gain(1, 0, 0.4f);
  hs_live_set_world_gain(0.25f, 0);
  hs_hands_silence();
  S.camera_speed = 1.1f;
  S.env = 1;
  S.filament = 1;
  S.mover_power = 0;
  g_finale_last = 0;
}

static void finale_frame(void) {
  float t = (float)S.phase_time;
  const float beat = 60.0f / 96 * 0.5f;
  // A full turn round the whole mechanism, ending facing the player.
  if (t < 14) {
    float a = hs_stage_ease(t / 14) * 2 * (float)M_PI;
    float r = 0.215f, up = 0.11f + 0.05f * sinf(a * 1.5f);
    hs_set_shot(simd_make_float3(0, -0.004f, -0.006f), simd_make_float3(sinf(a) * r, up, cosf(a) * r), 30, true);
  } else {
    hs_set_shot(simd_make_float3(0, -0.002f, -0.006f), simd_make_float3(0, 0.15f, 0.135f), 30, true);
  }
  // Everything that can work, works.
  float grow = fminf(1, t / 14);
  S.filament = t < 18 ? 1 + grow * 1.6f : 1.0f;
  S.gear_turn[0] = (t < 18) ? 0.5f + 0.5f * sinf(t * 1.1f) : S.gear_turn[0] * expf(-S.dt * 3);
  S.gear_turn[1] = (t < 18) ? 0.5f + 0.5f * sinf(t * 1.1f + 1) : S.gear_turn[1] * expf(-S.dt * 3);
  S.bellows[0] = S.gear_turn[0];
  S.bellows[1] = S.gear_turn[1];
  S.needle = S.gear_turn[0] * 0.5f + S.gear_turn[1] * 0.5f;
  S.scene.lattice_wake = t < 18 ? grow : 0;
  if (t > 8 && t < 14 && fmodf(t, 0.2f) < S.dt) {
    simd_float3 valve = hs_socket(fmodf(t, 0.4f) < 0.2f ? "socket_valve_l" : "socket_valve_r");
    hs_spawn(HS_KIND_STEAM, valve, simd_make_float3(0, 0.035f, 0), 0.001f, 0.01f, simd_make_float3(1.0f, 0.86f, 0.74f) * 0.28f, 0.0030f, 1.0f, 5);
  }
  // In the hands: the rhythm grows, the last chord is one long breath, and then nothing at all.
  if (t < 14) {
    int now = (int)(t / beat), before = (int)(g_finale_last / beat);
    if (now != before && t > 0.1f) {
      bool strong = now % 2 == 0;
      hs_hands_hit(strong ? 56 : 88, strong ? 0.10f : 0.05f, (strong ? 0.5f : 0.3f) + grow * 0.45f, (now % 4 == 1) ? -0.6f : (now % 4 == 3) ? 0.6f : 0);
    }
    hs_hands_set(HS_LAYER_BODY_LEFT, 0.12f + grow * 0.2f, 50, 0);
    hs_hands_set(HS_LAYER_BODY_RIGHT, 0.12f + grow * 0.2f, 50, 0);
  } else if (t < 18) {
    if (g_finale_last < 14) {
      hs_hands_hit(44, 0.6f, 1.0f, 0);
      const hs_trigger_effect kick = hs_trigger_raw(255);
      hs_fx_set_triggers(&kick, &kick);
    }
    float rest = 1 - hs_stage_ramp(t, 14, 18);
    hs_hands_set(HS_LAYER_BODY_LEFT, 0.42f * rest * rest, 42, 0);
    hs_hands_set(HS_LAYER_BODY_RIGHT, 0.42f * rest * rest, 42, 0);
  } else if (g_finale_last < 18) {
    hs_hands_silence();
    const hs_trigger_effect soft = hs_trigger_spring(1, 9, 1, 3);
    hs_fx_set_triggers(&soft, &soft);
    for (int who = 0; who < HS_CREW; who++) hs_crew_cheer(who);       // they bow
  }
  if (t >= 18) hs_show_text(HS_TEXT_YOURS, hs_stage_ease((t - 18.2f) / 0.8f), 0.07f, 0.88f, 44);
  g_finale_last = t;
  if (t >= 19.95f && S.phase == HS_PHASE_AWAKEN) hs_go_to_quietly(HS_CH_WORKSHOP);
}

static void finale_exit(void) {
  S.gear_turn[0] = S.gear_turn[1] = 0;
  S.bellows[0] = S.bellows[1] = 0;
  S.needle = 0;
  S.scene.lattice_wake = 0;
  hs_hands_silence();
}

static bool finale_press(uint32_t key) {
  if (key == HS_KEY_SOUTH) { hs_fx_key(1); hs_go_to_quietly(HS_CH_WORKSHOP); return true; }
  if (key == HS_KEY_EAST) { hs_fx_key(2); hs_go_to(HS_CH_VOICE); return true; }
  return false;
}

const hs_chapter_def hs_finale_def = {20.0f, 0.0f, finale_enter, finale_frame, finale_exit, finale_press};

// ============================================================================ Free play
// Nothing to learn and nowhere to be sent: every control does something, none of them leaves. Out is the one held key the screen always shows, or the pause menu.
enum { kTines = 8 };
static const float kBeatSeconds = 60.0f / 84 * 0.5f;
// A pentatonic comb in the piece's own mode: nothing the player does can sound wrong.
static const float kComb[kTines] = {293.66f, 349.23f, 392.0f, 440.0f, 523.25f, 587.33f, 698.46f, 783.99f};
static float g_drum, g_tine_lift[kTines];
static const float kHoldToLeave = 1.2f;

static void strike_tine(int tine, float strength) {
  float pressure = fmaxf(S.l2, S.r2);
  float pan = -0.5f + (float)tine / 7;
  hs_sound_strike(HS_MAT_SOUND_BRASS, kComb[tine], (0.05f + pressure * 0.05f) * strength, pan, HS_TIMING_DIRECT);
  hs_sound_strike(HS_MAT_SOUND_GLASS, kComb[tine] * 2, 0.012f * strength, pan, HS_TIMING_DIRECT);
  hs_hands_hit(90 + kComb[tine] * 0.08f, 0.05f, (0.42f + pressure * 0.4f) * strength, pan);
  g_tine_lift[tine] = 1;
}

static void bass(float pan) {
  hs_hands_hit(58, 0.18f, 0.9f, pan);
  hs_sound_strike(HS_MAT_SOUND_PAPER, 80, 0.22f, pan * 0.6f, HS_TIMING_ALIGNED);
}

static void workshop_enter(void) {
  static const float stations[HS_CREW][2] = {{-17, 9}, {17, 9}, {-19.5f, -6}, {19.5f, -6}, {-7, 19}, {7, 19}};
  set_stations(stations, HS_JOB_WATCH);
  hs_sound_score(0, HS_STEM_WORKSHOP_BED, 1.25f, 1.0f, true);
  hs_live_set_world_gain(0.3f, 0.5f);
  S.env = 0.9f;
  S.backdrop = 0.85f;
  S.filament = 1.0f;
  S.east_held = 0;
  S.awake = 1;              // it can be the first thing opened, with no opening before it to wake them
  hs_crew_set_awake(1);
  S.hand_weight = 1;
  g_drum = 0;
  hs_pedals_play_begin();
  const hs_trigger_effect full = hs_trigger_raw(255);
  hs_fx_set_triggers(&full, &full);
  hs_set_shot(simd_make_float3(0, -0.004f, -0.008f), simd_make_float3(0, 0.135f, 0.105f), 30, true);
  S.camera_speed = 1.5f;
}

static void workshop_frame(void) {
  g_drum += S.dt / kBeatSeconds;
  if (g_drum >= 16) g_drum -= 16;
  S.scene.cylinder_step = g_drum;
  for (int i = 0; i < kTines; i++) {
    g_tine_lift[i] *= expf(-S.dt * 16);
    S.scene.tines[i] = g_tine_lift[i] * cosf(g_tine_lift[i] * 40);
  }
  hs_pedals_play_frame();
  hs_ball_play_frame();
  hs_finger_play_frame();
  // Where the light is: at the valve while it is hot, otherwise over the middle of the hall.
  if (S.mover_power < 0.5f) {
    S.mover = simd_make_float3(0, 0.02f, kHallCenterZ);
    S.mover_power = 0.5f;
    S.mover_color = simd_make_float3(1.0f, 0.6f, 0.3f);
    S.mover_range = 0.12f;
  }
  // The touch surface is a set of strings: put a finger down and a tine sounds, hold it and slide, and every tine it crosses sounds in turn, either
  // way, harder the faster it goes. Two fingers can strum at once.
  static int last_tine[2] = {-1, -1};
  for (int finger = 0; finger < 2; finger++) {
    if (!S.touching[finger]) { last_tine[finger] = -1; continue; }
    int tine = (int)fminf(7, fmaxf(0, (S.touch_x[finger] / 0.0255f + 1) * 4));
    if (last_tine[finger] < 0) { strike_tine(tine, 0.9f); last_tine[finger] = tine; continue; }
    int crossed = abs(tine - last_tine[finger]);
    for (int k = 1; k <= crossed; k++) strike_tine(last_tine[finger] + (tine > last_tine[finger] ? k : -k), fminf(1, 0.55f + 0.12f * (float)crossed));
    last_tine[finger] = tine;
  }
  // Shaking makes the crew cheer.
  if (simd_length(S.hand_turn) > 6) for (int who = 0; who < HS_CREW; who++) hs_crew_cheer(who);
  // The one way out, held, and the filament fills as it is held, so it can never come as a surprise.
  if (S.phase != HS_PHASE_LEAVING && !S.paused && (S.down & HS_KEY_EAST)) {
    S.east_held += S.dt;
    S.filament = fmaxf(S.filament, 1 + S.east_held / kHoldToLeave * 2);
    if (S.east_held >= kHoldToLeave) S.leaving_to_home = true;
  } else {
    S.east_held = 0;
  }
}

static void workshop_exit(void) {
  hs_pedals_play_end();
  hs_finger_play_end();
  for (int i = 0; i < HS_LAYER_COUNT; i++) hs_hands_set((hs_hand_layer)i, 0, 60, 0);
  hs_sound_set(HS_SOUND_ROLL, 0, 0.5f);
  hs_sound_set(HS_SOUND_RUB, 0, 0.5f);
  hs_world_set_drag(HS_WORLD_BALL, 0);
  for (int i = 0; i < kTines; i++) { g_tine_lift[i] = 0; S.scene.tines[i] = 0; }
}

static bool workshop_press(uint32_t key) {
  switch (key) {
    case HS_KEY_WEST: hs_fx_key(0); strike_tine(4, 1); return true;
    case HS_KEY_SOUTH: hs_fx_key(1); strike_tine(5, 1); return true;
    case HS_KEY_EAST: hs_fx_key(2); strike_tine(6, 1); return true;
    case HS_KEY_NORTH: hs_fx_key(3); strike_tine(7, 1); return true;
    case HS_KEY_LEFT: hs_fx_cross(-1, 0); strike_tine(0, 1); return true;
    case HS_KEY_DOWN: hs_fx_cross(0, 1); strike_tine(1, 1); return true;
    case HS_KEY_UP: hs_fx_cross(0, -1); strike_tine(2, 1); return true;
    case HS_KEY_RIGHT: hs_fx_cross(1, 0); strike_tine(3, 1); return true;
    case HS_KEY_L1: hs_fx_shoulder(0); bass(-1); return true;      // the magnet on that side jumps with it
    case HS_KEY_R1: hs_fx_shoulder(1); bass(1); return true;
    case HS_KEY_TOUCH:
      hs_fx_dome_click(S.touching[0] ? S.touch_x[0] / 0.0255f : 0);
      hs_sound_strike(HS_MAT_SOUND_BELL, 587.33f, 0.06f, 0, HS_TIMING_DIRECT);
      return true;
    default: return false;
  }
}

const hs_chapter_def hs_workshop_def = {1.0f, 2.0f, workshop_enter, workshop_frame, workshop_exit, workshop_press};

// ============================================================================ The listening room
static void listening_enter(void) {
  hs_world_set_well_open(true);
  static const float stations[HS_CREW][2] = {{-17, 9}, {17, 9}, {-19.5f, -6}, {19.5f, -6}, {-7, 19}, {7, 19}};
  set_stations(stations, HS_JOB_SIT);
  hs_live_set_world_gain(0, 0);
  S.env = 0.72f;
  S.filament = 0.4f;
  S.backdrop = 0.7f;
  S.awake = 1;
  hs_crew_set_awake(1);
  S.hand_weight = 1;
  S.camera_speed = 1.2f;
  S.lid = 1;
  hs_fx_set_muted(true);
  S.mover = simd_make_float3(0, 0.01f, kHallCenterZ);
  S.mover_color = simd_make_float3(1.0f, 0.55f, 0.28f);
  S.mover_power = 0.4f;
  S.mover_range = 0.10f;
}

static void listening_frame(void) {
  hs_listen_status status = hs_listen_get_status();
  float level = fmaxf(status.left, status.right);
  S.lid += (1 - S.lid) * (1 - expf(-S.dt * 1.6f));
  S.cone = sinf((float)S.seconds * 2 * (float)M_PI * 58) * fminf(1, level * 1.2f);
  S.coil[0] = status.left * 2;
  S.coil[1] = status.right * 2;
  S.mover_power = 0.3f + status.hit * 1.8f;
  S.filament = 0.35f + status.hit * 1.2f;
  hs_set_shot(simd_make_float3(0, -0.009f, -0.010f), simd_make_float3(0, 0.055f, 0.030f), 30, false);
  // No sound engine runs here, but the crew and the ball still live: the room is stepped from the frame, and what it would have said to the hands is dropped.
  static float owed;
  static int8_t unused_waveform[HS_WAVE_BYTES];
  const float period = (float)(HS_STREAM_PERIOD_MS / 1000.0);
  owed = fminf(owed + S.dt, period * 3);
  while (owed >= period) {
    hs_live_block(S.seconds * 1000, unused_waveform, NULL, NULL, NULL);
    owed -= period;
  }
}

static void listening_exit(void) {
  hs_world_set_well_open(false);
  hs_fx_set_muted(false);
  S.lid = 0;
  S.cone = 0;
}

static bool listening_press(uint32_t key) {
  if (key == HS_KEY_EAST) S.leaving_to_home = true;
  return true;      // keys move on screen and make no sound of their own: your music stays yours
}

const hs_chapter_def hs_listening_def = {0.5f, 0.5f, listening_enter, listening_frame, listening_exit, listening_press};
