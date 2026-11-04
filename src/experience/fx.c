// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// One design for each thing that happens: what it sounds like and how it feels are written here together, as one event.
// The hands answer about 110 ms after a report, and the ear would hear a late click as a mistake, so a press gets its own click at
// once and its body arrives with the hand.
#include "experience/fx_internal.h"
#include "experience/director.h"
#include "experience/stage.h"
#include "experience/voice_out.h"
#include "hands/hands.h"
#include "sound/sound.h"
#include <math.h>
#include <stdatomic.h>

static bool g_speaker_mode, g_quiet, g_paused;
static _Atomic int g_input_click_mode;      // who answers a press at once: see hs_fx_on_input
static _Atomic bool g_input_thread_clicks;

void hs_fx_set_speaker_mode(bool on) { g_speaker_mode = on; hs_fx_set_click_mode(on ? HS_CLICKS_SPEAKER : g_quiet ? HS_CLICKS_SILENT : HS_CLICKS_NORMAL); }
void hs_fx_set_muted(bool quiet) { g_quiet = quiet; hs_fx_set_click_mode(quiet ? HS_CLICKS_SILENT : g_speaker_mode ? HS_CLICKS_SPEAKER : HS_CLICKS_NORMAL); }

// The ear catches a late click far sooner than the eye a late frame. So in a live run the input thread itself plays each press's click,
// one audio block after the report instead of up to a frame later, and only the part's picture and body wait for the frame.
void hs_fx_on_input(uint32_t pressed) {
  int mode = atomic_load_explicit(&g_input_click_mode, memory_order_relaxed);
  if (mode == HS_CLICKS_SILENT) return;
  typedef struct { uint32_t key; hs_sound_material material; float hertz, gain, pan; } Click;
  static const Click clicks[] = {
    {HS_KEY_WEST, HS_MAT_SOUND_CERAMIC, 1046.5f, 0.075f, 0.45f}, {HS_KEY_SOUTH, HS_MAT_SOUND_CERAMIC, 880, 0.075f, 0.6f},
    {HS_KEY_EAST, HS_MAT_SOUND_CERAMIC, 1174.7f, 0.075f, 0.75f}, {HS_KEY_NORTH, HS_MAT_SOUND_CERAMIC, 1318.5f, 0.075f, 0.6f},
    {HS_KEY_UP, HS_MAT_SOUND_STEEL, 1450, 0.05f, -0.6f}, {HS_KEY_DOWN, HS_MAT_SOUND_STEEL, 1450, 0.05f, -0.6f},
    {HS_KEY_LEFT, HS_MAT_SOUND_STEEL, 1340, 0.05f, -0.6f}, {HS_KEY_RIGHT, HS_MAT_SOUND_STEEL, 1560, 0.05f, -0.6f},
    {HS_KEY_L1, HS_MAT_SOUND_WOOD, 420, 0.09f, -0.6f}, {HS_KEY_R1, HS_MAT_SOUND_WOOD, 420, 0.09f, 0.6f},
    {HS_KEY_L3, HS_MAT_SOUND_WOOD, 310, 0.11f, -0.4f}, {HS_KEY_R3, HS_MAT_SOUND_WOOD, 310, 0.11f, 0.4f},
    {HS_KEY_HOME, HS_MAT_SOUND_BELL, 1320, 0.06f, 0}, {HS_KEY_CREATE, HS_MAT_SOUND_CERAMIC, 1760, 0.035f, -0.25f}, {HS_KEY_OPTIONS, HS_MAT_SOUND_CERAMIC, 1760, 0.035f, 0.25f},
    {HS_KEY_TOUCH, HS_MAT_SOUND_BELL, 880, 0.05f, 0}, {HS_KEY_MUTE, HS_MAT_SOUND_BRASS, 1600, 0.03f, 0},
  };
  for (unsigned i = 0; i < sizeof clicks / sizeof clicks[0]; i++) {
    if (!(pressed & clicks[i].key)) continue;
    if (mode == HS_CLICKS_SPEAKER) hs_speaker_pluck(clicks[i].hertz * 1.6f, clicks[i].gain * 0.6f);
    else hs_sound_strike_now(clicks[i].material, clicks[i].hertz, clicks[i].gain, clicks[i].pan);
  }
}

void hs_fx_input_thread_clicks(bool on) { atomic_store(&g_input_thread_clicks, on); }
void hs_fx_set_click_mode(int mode) { atomic_store_explicit(&g_input_click_mode, mode, memory_order_relaxed); }

static void click(hs_sound_material material, float hertz, float gain, float pan) {
  if (g_quiet) return;
  if (atomic_load_explicit(&g_input_thread_clicks, memory_order_relaxed)) return;    // already played, from the input thread
  if (g_speaker_mode) {
    hs_speaker_pluck(hertz * 1.6f, gain * 0.6f);
    return;
  }
  hs_sound_strike(material, hertz, gain, pan, HS_TIMING_DIRECT);
}

static void hand(float hertz, float seconds, float strength, float pan) {
  if (g_speaker_mode || g_quiet) return;     // the waveform stream and the speaker cannot run together
  hs_hands_hit(hertz, seconds, strength, pan);
}

void hs_fx_key(int which) {
  // Each key its own note, so a rhythm on them makes a little tune.
  static const float pitches[4] = {1046.5f, 880.0f, 1174.7f, 1318.5f};
  static const float pans[4] = {0.45f, 0.6f, 0.75f, 0.6f};
  click(HS_MAT_SOUND_CERAMIC, pitches[which], 0.075f, pans[which]);
  hand(190, 0.03f, 0.34f, 0.7f);
}

void hs_fx_cross(float x, float y) {
  (void)y;
  click(HS_MAT_SOUND_STEEL, 1450 + x * 110, 0.05f, -0.6f);
  hand(170, 0.03f, 0.32f, -0.7f);
}

void hs_fx_shoulder(int side) {
  click(HS_MAT_SOUND_WOOD, 420, 0.09f, side ? 0.6f : -0.6f);
  hand(90, 0.04f, 0.28f, side ? 0.8f : -0.8f);
}

void hs_fx_stick_press(int side) {
  click(HS_MAT_SOUND_WOOD, 310, 0.11f, side ? 0.4f : -0.4f);
  hand(120, 0.05f, 0.4f, side ? 0.7f : -0.7f);
}

void hs_fx_stick_limit(int side) {
  click(HS_MAT_SOUND_PAPER, 700, 0.02f, side ? 0.4f : -0.4f);
  hand(150, 0.02f, 0.12f, side ? 0.6f : -0.6f);
}

void hs_fx_dome_click(float x) {
  click(HS_MAT_SOUND_BELL, 880 + x * 60, 0.05f, x * 0.6f);
  hand(110, 0.07f, 0.55f, x * 0.8f);
}

void hs_fx_home(void) {
  click(HS_MAT_SOUND_BELL, 1320, 0.06f, 0);
  hand(70, 0.14f, 0.6f, 0);
}

void hs_fx_pill(int side) {
  click(HS_MAT_SOUND_CERAMIC, 1760, 0.035f, side ? 0.25f : -0.25f);
  hand(210, 0.02f, 0.2f, side ? 0.3f : -0.3f);
}

void hs_fx_mute(void) {
  click(HS_MAT_SOUND_BRASS, 1600, 0.03f, 0);
  hand(240, 0.015f, 0.16f, 0);
}

void hs_fx_faint(void) {
  if (g_quiet) return;
  hs_sound_strike(HS_MAT_SOUND_GLASS, 3400, 0.006f, 0, HS_TIMING_DIRECT);
}

void hs_fx_organ_stop(bool forward) {
  hs_sound_score(3, forward ? HS_STEM_STOP_UP : HS_STEM_STOP_DOWN, 1.0f, 0.01f, false);
  // A single push travelling one way through the hands, in the direction the story moves.
  float from = forward ? -0.9f : 0.9f;
  for (int i = 0; i < 3; i++) hs_hands_hit(72 + 16 * i, 0.09f - 0.02f * i, 0.5f - 0.1f * i, from * (1 - i));
}

void hs_fx_hurry(void) {
  hand(200, 0.02f, 0.2f, 0);
}

void hs_fx_reset(void) {
  g_speaker_mode = g_quiet = g_paused = false;
  hs_sound_set_pause(false);     // or the next session would start muffled, with nothing to lift it
  hs_live_set_paused(false);
  hs_fx_set_click_mode(HS_CLICKS_NORMAL);
}

// What the chapter wants of the triggers. A pause lets go of them, and this is what comes back afterwards.
static hs_trigger_effect g_wanted_left, g_wanted_right;

void hs_fx_set_triggers(const hs_trigger_effect *left, const hs_trigger_effect *right) {
  g_wanted_left = *left;
  g_wanted_right = *right;
  if (!g_paused) hs_link_set_triggers(left, right);
}

void hs_fx_set_paused(bool paused) {
  if (paused == g_paused) return;
  g_paused = paused;
  hs_sound_set_pause(paused);
  hs_live_set_paused(paused);
  if (paused) {
    hs_hands_silence();
    // Everything that makes a sound stops making it: the chapter's loops come back with its next frame, and so does the speaker's music.
    for (int layer = 0; layer < HS_SOUND_LAYER_COUNT; layer++) hs_sound_set((hs_sound_layer)layer, 0, 0);
    hs_voice_set_volume(0);
    hs_mic_set_listening(false);
    const hs_trigger_effect off = hs_trigger_off();
    hs_link_set_triggers(&off, &off);
  } else {
    hs_link_set_triggers(&g_wanted_left, &g_wanted_right);
    hs_sound_strike(HS_MAT_SOUND_CERAMIC, 1400, 0.03f, 0, HS_TIMING_DIRECT);
  }
}
