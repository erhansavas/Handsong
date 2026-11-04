// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Breath: sound made visible as pressure on a plate of sand. Voice: a diaphragm itself, playing what was written for it.
#include "experience/director.h"
#include "experience/voice_out.h"
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

// ============================================================================ Breath
static const float kModes[6][2] = {{2, 3}, {1, 4}, {3, 5}, {2, 5}, {4, 7}, {3, 7}};
static int g_mode;
static float g_level, g_shake;
static float g_sand;
static bool g_listen_off, g_asked_now;

static void breath_enter(void) {
  static const float stations[HS_CREW][2] = {{-17, 9}, {17, 9}, {-19.5f, -6}, {19.5f, -6}, {-7, 19}, {7, 19}};
  set_stations(stations, HS_JOB_WATCH);
  hs_set_shot(simd_make_float3(0, -0.006f, -0.0025f), simd_make_float3(0, 0.120f, 0.057f), 28, false);
  S.camera_speed = 1.4f;
  hs_sound_score(0, HS_STEM_BREATH_BED, 1.0f, 3.0f, true);
  hs_sound_score_gain(1, 0, 2.0f);
  hs_live_set_world_gain(0.04f, 0);
  hs_hands_silence();
  g_mode = 0;
  g_sand = 0;
  g_shake = 0;
  g_listen_off = false;
  g_asked_now = false;
  S.filament = 0.55f;
  S.env = 0.8f;
  S.mover = simd_make_float3(0, 0.02f, kHallCenterZ);
  S.mover_color = simd_make_float3(1.0f, 0.5f, 0.2f);
  S.mover_power = 0.5f;
  S.mover_range = 0.12f;
  const hs_trigger_effect soft = hs_trigger_spring(1, 9, 1, 3);
  hs_fx_set_triggers(&soft, &soft);
}

static void breath_frame(void) {
  float t = (float)S.phase_time;
  g_sand += ((S.phase == HS_PHASE_LEAVING ? 0.0f : 1.0f) - g_sand) * (1 - expf(-S.dt * 1.2f));
  hs_mic_state mic = hs_mic_get_state();
  float level = 0, pitch = 0;
  S.listening_now = mic == HS_MIC_GRANTED && !g_listen_off && S.phase != HS_PHASE_LEAVING;
  hs_mic_set_listening(S.listening_now);
  if (S.listening_now) hs_mic_read(&level, &pitch);
  else if (mic == HS_MIC_DENIED) level = fmaxf(S.l2, S.r2);    // a fully designed way: the crew's bellows drive the plate instead
  g_level += (level - g_level) * (1 - expf(-S.dt * 14));
  if (mic == HS_MIC_GRANTED && pitch > 60) {
    // The voice's pitch picks the figure on the plate, as a real plate's resonances would.
    int mode = (int)fminf(5, fmaxf(0, log2f(pitch / 90) * 2));
    if (mode != g_mode) { g_mode = mode; hs_sound_strike(HS_MAT_SOUND_BRASS, 1400 + 140 * (float)mode, 0.012f, 0, HS_TIMING_DIRECT); }
  }
  g_shake += (g_level - g_shake) * (1 - expf(-S.dt * 20));
  S.scene.chladni_m = kModes[g_mode][0];
  S.scene.chladni_n = kModes[g_mode][1];
  S.scene.chladni_shake = g_shake;
  S.scene.sand_amount = g_sand;
  S.bellows[0] += (S.l2 - S.bellows[0]) * (1 - expf(-S.dt * 16));
  S.bellows[1] += (S.r2 - S.bellows[1]) * (1 - expf(-S.dt * 16));
  S.cone = sinf((float)S.seconds * 58) * g_shake * 0.7f;
  S.mover_power = 0.45f + g_shake * 1.2f;
  // A fine shimmer in the hands with the level. Silence when nothing is heard.
  hs_hands_set(HS_LAYER_SHEEN, g_shake > 0.02f ? g_shake * 0.55f : 0, 300 + 400 * g_shake, 0);
  hs_sound_set(HS_SOUND_BELLOWS, mic == HS_MIC_DENIED ? g_shake * 0.3f : 0, 0.4f);
  hs_sound_set(HS_SOUND_HISS, g_shake * 0.08f, 0.8f);  // the sand itself
  if (S.phase == HS_PHASE_AWAKEN && t > 1.2f) {
    if (mic == HS_MIC_NOT_ASKED) hs_show_text(HS_TEXT_BREATH_ASK, hs_stage_ease((t - 1.2f) / 0.6f), 0.07f, 0.88f, 30);
  }
  if (S.phase == HS_PHASE_PLAY && mic == HS_MIC_NOT_ASKED) hs_show_text(HS_TEXT_BREATH_ASK, 1, 0.07f, 0.88f, 30);
  if (S.phase == HS_PHASE_PLAY && g_listen_off) hs_show_text(HS_TEXT_BREATH_STOPPED, 1, 0.07f, 0.88f, 28);
  if (S.phase == HS_PHASE_LEAVING) {
    hs_set_shot(simd_make_float3(0, -0.008f, -0.010f), simd_make_float3(0, 0.106f, 0.058f), 28, false);
    S.camera_speed = 1.3f;
  } else {
    hs_set_shot(simd_make_float3(0, -0.006f, -0.0025f), simd_make_float3(0, 0.120f, 0.057f), 28, false);
  }
}

static void breath_exit(void) {
  S.listening_now = false;
  hs_mic_stop();
  hs_hands_set(HS_LAYER_SHEEN, 0, 300, 0);
  hs_sound_set(HS_SOUND_BELLOWS, 0, 0);
  hs_sound_set(HS_SOUND_HISS, 0, 0);
  S.scene.sand_amount = 0;
  S.scene.chladni_shake = 0;
  S.bellows[0] = S.bellows[1] = 0;
  S.cone = 0;
}

static bool breath_press(uint32_t key) {
  // The first press of the lower key asks for the microphone, at the moment it means something.
  if (key == HS_KEY_SOUTH && hs_mic_get_state() == HS_MIC_NOT_ASKED && !g_asked_now) {
    g_asked_now = true;
    hs_fx_key(1);
    hs_mic_request();
    return true;
  }
  if (S.phase != HS_PHASE_PLAY) return false;
  switch (key) {
    case HS_KEY_SOUTH: hs_go_to(HS_CH_VOICE); return true;
    case HS_KEY_EAST: hs_go_to(HS_CH_BALANCE); return true;
    case HS_KEY_WEST: hs_fx_key(0); g_sand = 0; return true;      // the plate is swept
    case HS_KEY_NORTH: hs_fx_key(3); g_sand = 1; return true;     // more sand
    case HS_KEY_UP: case HS_KEY_DOWN: case HS_KEY_LEFT: case HS_KEY_RIGHT:
      g_mode = (g_mode + (key == HS_KEY_LEFT || key == HS_KEY_UP ? 5 : 1)) % 6;
      hs_fx_cross(0, 0);
      return true;
    case HS_KEY_TOUCH:
      hs_sound_strike(HS_MAT_SOUND_BELL, 523.3f * (1 + 0.5f * (float)g_mode / 5), 0.06f, 0, HS_TIMING_DIRECT);
      hs_hands_hit(100, 0.08f, 0.5f, 0);
      g_shake = 1;
      return true;
    case HS_KEY_MUTE:
      hs_fx_mute();
      g_listen_off = !g_listen_off;
      return true;
    default: return false;
  }
}

const hs_chapter_def hs_breath_def = {4.0f, 3.0f, breath_enter, breath_frame, breath_exit, breath_press};

// ============================================================================ Voice
static int g_octave;
static float g_pitch_step;
static bool g_mac_voice;       // the link is narrow right now, so the Mac must sing it instead

static void voice_enter(void) {
  hs_world_set_well_open(true);
  static const float stations[HS_CREW][2] = {{-17, 9}, {17, 9}, {-19.5f, -6}, {19.5f, -6}, {-7, 19}, {7, 19}};
  set_stations(stations, HS_JOB_SIT);
  hs_set_shot(simd_make_float3(0, -0.009f, -0.0015f), simd_make_float3(0, 0.122f, 0.067f), 28, false);
  S.camera_speed = 1.2f;
  hs_live_set_world_gain(0, 0);
  hs_hands_silence();
  S.filament = 0.35f;
  S.env = 0.7f;
  S.mover = simd_make_float3(0, 0.01f, kHallCenterZ);
  S.mover_color = simd_make_float3(1.0f, 0.55f, 0.28f);
  S.mover_power = 0.4f;
  S.mover_range = 0.10f;
  // The bridge: under the handover the sound never leaves the room.
  hs_sound_score(0, HS_STEM_BRIDGE, 1.0f, 0.2f, false);
  hs_sound_score_gain(1, 0, 0.3f);
  g_mac_voice = S.real_time && !hs_link_wide();
  S.voice_on_mac = g_mac_voice;
  g_octave = 0;
  g_pitch_step = 0;
  if (!g_mac_voice) {
    hs_voice_start();
    hs_fx_set_speaker_mode(true);
  } else {
    hs_sound_score(2, HS_STEM_VOICE, 1.0f, 0.4f, true);
  }
  const hs_trigger_effect soft = hs_trigger_spring(1, 9, 1, 3);
  hs_fx_set_triggers(&soft, &soft);
}

static void voice_frame(void) {
  float t = (float)S.phase_time;
  // The lid swings up off the speaker, and it sits there in the well, working.
  S.lid += ((S.phase == HS_PHASE_LEAVING ? 0.0f : 1.0f) - S.lid) * (1 - expf(-S.dt * 1.6f));
  float out = hs_voice_level();
  S.cone = out * sinf((float)S.seconds * 2 * (float)M_PI * 88) * 1.0f;
  S.mover_power = 0.35f + out * 0.6f;
  if (S.phase == HS_PHASE_AWAKEN && t >= 0.6f) hs_sound_score_gain(0, 0, 0.4f);   // the Mac steps back as the controller takes over
  // Said for a few seconds, and only to someone with a controller, for whom it is news.
  if (g_mac_voice && hs_link_state() != HS_CONTROLLER_NONE && (S.phase == HS_PHASE_AWAKEN || (S.phase == HS_PHASE_PLAY && S.phase_time < 4))) hs_show_text(HS_TEXT_SOFTER, 1, 0.07f, 0.88f, 28);
  if (S.phase == HS_PHASE_LEAVING) {
    hs_set_shot(simd_make_float3(0, -0.002f, -0.006f), simd_make_float3(0, 0.150f, 0.14f), 30, true);
    S.camera_speed = 1.3f;
  } else {
    hs_set_shot(simd_make_float3(0, -0.009f, -0.0015f), simd_make_float3(0, 0.122f, 0.067f), 28, false);
  }
  hs_voice_set_volume(1);
}

static void voice_exit(void) {
  hs_world_set_well_open(false);
  hs_voice_stop();
  hs_fx_set_speaker_mode(false);
  S.lid = 0;
  S.cone = 0;
  hs_sound_score_gain(2, 0, 0.5f);
}

// The tongues: any press plucks one, wherever the music is coming from, with no delay we could remove.
static void pluck(float semitones) {
  float hertz = 880 * powf(2, (semitones + g_pitch_step + 12 * (float)g_octave) / 12);
  if (g_mac_voice) hs_sound_strike(HS_MAT_SOUND_BRASS, hertz, 0.08f, 0, HS_TIMING_DIRECT);
  else hs_speaker_pluck(hertz, 0.5f);
}

static bool voice_press(uint32_t key) {
  if (S.phase != HS_PHASE_PLAY) return false;
  switch (key) {
    case HS_KEY_SOUTH: hs_go_to(HS_CH_FINALE); hs_sound_score(0, HS_STEM_BRIDGE, 1.0f, 0.2f, false); return true;
    case HS_KEY_EAST: hs_go_to(HS_CH_BREATH); hs_sound_score(0, HS_STEM_BRIDGE, 1.0f, 0.2f, false); return true;
    case HS_KEY_WEST: pluck(0); return true;
    case HS_KEY_NORTH: pluck(7); return true;
    case HS_KEY_TOUCH: pluck(S.touching[0] ? S.touch_x[0] / 0.0255f * 7 : 3); return true;
    case HS_KEY_UP: g_pitch_step += 1; pluck(0); return true;
    case HS_KEY_DOWN: g_pitch_step -= 1; pluck(0); return true;
    case HS_KEY_LEFT: pluck(-5); return true;
    case HS_KEY_RIGHT: pluck(5); return true;
    case HS_KEY_L1: g_octave = g_octave > -1 ? g_octave - 1 : -1; pluck(0); return true;
    case HS_KEY_R1: g_octave = g_octave < 1 ? g_octave + 1 : 1; pluck(0); return true;
    default: return false;    // the plain reaction, on the speaker in this chapter
  }
}

const hs_chapter_def hs_voice_def = {1.5f, 3.0f, voice_enter, voice_frame, voice_exit, voice_press};
