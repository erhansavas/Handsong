// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The controller's own lights, playing along: real light in the hands, moving with the light on screen. Never a flash: everything here rises and
// falls over a tenth of a second or more. Quiet by default, bright only at the top of the story.
#include "experience/director.h"
#include "experience/lights.h"
#include "experience/voice_out.h"
#include "hands/hands.h"
#include "listen/listen.h"
#include <math.h>

static float g_red, g_green, g_blue, g_struck;
static simd_float3 g_tint = {1.0f, 0.42f, 0.14f};
static bool g_follow_music, g_steady;

void hs_lights_set_follow_music(bool on) { g_follow_music = on; }
void hs_lights_set_steady(bool steady) { g_steady = steady; }

static uint8_t fill(int count) { return (uint8_t)((1 << (count < 0 ? 0 : count > 5 ? 5 : count)) - 1); }
static uint8_t one(float where) { int i = (int)lrintf((where * 0.5f + 0.5f) * 4); return (uint8_t)(1 << (i < 0 ? 0 : i > 4 ? 4 : i)); }

// The five small lights do something in every moment: they count while the name is written and while the parts lock in, run across as one chapter
// gives way to the next, and then say which chapter this is. In play they become that chapter's own gauge whenever you are doing something.
static uint8_t player_lights(int chapter_count, float shake) {
  float t = (float)S.phase_time;
  if (S.chapter == HS_CH_ASSEMBLY) {
    float at = (float)S.phase_time;
    if (at < 14) return 0;
    if (at < 22) return fill((int)((at - 14) / 8 * 6));          // with the pen
    if (at < 30) return 0x1F;
    static const float kTicks[5] = {30.0f, 30.85f, 31.5f, 31.95f, 32.3f};
    int ticks = 0;
    for (int i = 0; i < 5; i++) if (at >= kTicks[i]) ticks++;
    if (at < 33) return fill(ticks);                             // one more with each part that locks
    if (at < 33.4f) return 0;                                    // the held breath
    return at < 36 ? 0x1F : 0;
  }
  if (S.phase == HS_PHASE_LEAVING) return one(sinf(t * 7) * 1.1f);     // running from side to side as the camera moves on
  if (S.phase == HS_PHASE_AWAKEN && t < 1.5f) return fmodf(t, 0.5f) < 0.3f ? fill(chapter_count) : 0;
  switch (S.chapter) {
    case HS_CH_GEARS: {
      float pull = fmaxf(S.l2, S.r2);
      if (pull > 0.02f) return fill((int)ceilf(pull * 5));       // a load gauge
      break;
    }
    case HS_CH_LATTICE:
      if (S.touching[0]) return one(S.touch_x[0] / 0.0255f);     // under the finger
      break;
    case HS_CH_COILS:
    case HS_CH_BALANCE: {
      simd_float3 down = simd_act(simd_inverse(S.hand), simd_make_float3(0, -1, 0));
      if (fabsf(down.x) > 0.06f) return one(down.x * 1.6f);      // a spirit level
      break;
    }
    case HS_CH_BREATH:
      if (shake > 0.03f) return fill((int)ceilf(shake * 5));     // a level meter
      break;
    case HS_CH_VOICE: return 0x04;                               // only the middle: listen
    case HS_CH_FINALE: return S.phase_time > 18 ? 0 : g_struck > 0.25f ? 0x1F : one(sinf(t * 3) * 1.1f);
    case HS_CH_WORKSHOP: {
      float pull = fmaxf(S.l2, S.r2);
      if (pull > 0.02f) return fill((int)ceilf(pull * 5));
      if (S.touching[0]) return one(S.touch_x[0] / 0.0255f);
      simd_float3 down = simd_act(simd_inverse(S.hand), simd_make_float3(0, -1, 0));
      if (fabsf(down.x) > 0.06f) return one(down.x * 1.6f);
      return (uint8_t)(1 << ((int)S.scene.cylinder_step % 5));
    }
    case HS_CH_LISTENING: return fill((int)ceilf(g_struck * 5));
    default: break;
  }
  return fill(chapter_count);
}

void hs_lights_frame(hs_scene *scene) {
  float shake = scene->chladni_shake;
  float left, right;
  hs_hands_view(&left, &right);
  if (S.chapter == HS_CH_LISTENING) {     // no hands engine runs in there: the music's own level
    hs_listen_status status = hs_listen_get_status();
    left = status.left;
    right = status.right;
  }
  float struck = fminf(1, (left + right) * 1.6f);
  g_struck += (struck - g_struck) * (1 - expf(-S.dt / (struck > g_struck ? 0.10f : 0.40f)));
  if (g_steady) g_struck = 0;
  // The bar belongs to the moment: its colour says what is alive right now, and how much of it says how strongly. A finger on the touch surface is green everywhere.
  float r = 1.0f, g = 0.42f, b = 0.14f, level = 0.05f + g_struck * 0.25f;
  float pull = fmaxf(S.l2, S.r2);
  // Metal under load: dull red, through orange, to yellow-white at the stop.
  #define HEAT() do { r = 1; g = 0.12f + 0.60f * pull; b = 0.02f + 0.36f * pull * pull * pull; level = 0.04f + pull * pull * 0.55f + g_struck * 0.3f; } while (0)
  #define GREEN(amount) do { r = scene->touch_color.x; g = scene->touch_color.y; b = scene->touch_color.z; level = amount; } while (0)
  int players = 0;
  uint8_t mic = 0;
  switch (S.chapter) {
    case HS_CH_ASSEMBLY: level = S.phase_time < 30 ? 0 : fminf(0.25f, (float)(S.phase_time - 30) * 0.05f); break;
    case HS_CH_COILS: {
      players = 1;
      // Copper for the coils' blows; in play, what the ball is rolling on.
      static const float kOn[5][3] = {{0.95f, 0.78f, 0.58f}, {1.0f, 0.40f, 0.16f}, {0.22f, 0.70f, 0.72f}, {1.0f, 0.56f, 0.20f}, {0.95f, 0.78f, 0.58f}};
      r = 1; g = 0.48f; b = 0.20f;
      if (S.phase == HS_PHASE_PLAY) { r = kOn[S.ball_surface][0]; g = kOn[S.ball_surface][1]; b = kOn[S.ball_surface][2]; }
      level = 0.06f + g_struck * 0.4f;
      break;
    }
    case HS_CH_GEARS: players = 2; HEAT(); break;
    case HS_CH_LATTICE:
      players = 3;
      GREEN(S.touching[0] || S.touching[1] ? 0.45f : 0.10f);
      break;
    case HS_CH_BALANCE: {
      players = 4;
      // The crew's lantern, leaning toward red the further it is tipped.
      float tip = fminf(1, simd_length(S.hand_turn) * 0.25f);
      r = 1; g = 0.62f - 0.30f * tip; b = 0.26f - 0.16f * tip;
      level = 0.05f + tip * 0.25f + g_struck * 0.2f;
      break;
    }
    case HS_CH_BREATH: {
      players = 5;
      // Sand in lamplight, whitening as the voice gets stronger.
      float loud = fminf(1, shake);
      r = 1; g = 0.70f + 0.25f * loud; b = 0.36f + 0.50f * loud;
      level = 0.04f + loud * 0.5f;
      // Lit exactly while the instrument is listening, and out when you mute it. While it waits to be allowed, it slowly asks.
      mic = S.listening_now || (hs_mic_get_state() == HS_MIC_NOT_ASKED && fmodf((float)S.seconds, 1.6f) < 0.8f) ? 1 : 0;
      break;
    }
    case HS_CH_VOICE: players = 5; r = 1; g = 0.78f; b = 0.58f; level = 0.03f + hs_voice_level() * 0.12f; break;
    case HS_CH_FINALE: {
      players = 5;
      // Everything together: ember climbing to gold with the piece.
      float rise = fminf(1, (float)S.phase_time / 14.0f);
      r = 1; g = 0.30f + 0.40f * rise + g_struck * 0.15f; b = 0.10f + 0.16f * rise;
      level = S.phase_time > 18 ? 0.04f : 0.15f + rise * 0.1f + g_struck * 0.55f;
      break;
    }
    case HS_CH_WORKSHOP:
      players = 5;
      if (S.touching[0] || S.touching[1]) GREEN(0.45f);
      else if (pull > 0.02f) HEAT();
      else { r = 1; g = 0.60f; b = 0.25f; level = 0.05f + g_struck * 0.35f; }     // brass, answering each note
      break;
    case HS_CH_LISTENING: {
      // The music's own weight: warm when it is calm, brighter and yellower on a hit.
      hs_listen_status status = hs_listen_get_status();
      r = 1; g = 0.38f + 0.40f * status.hit; b = 0.12f + 0.20f * status.hit;
      level = g_follow_music ? 0.03f + g_struck * 0.5f : 0;
      break;
    }
    default: break;
  }
  #undef HEAT
  #undef GREEN
  if (S.paused || S.disconnected) { level *= 0.2f; mic = 0; }
  float slew = 1 - expf(-S.dt / 0.12f);
  g_red += (r * level - g_red) * slew;
  g_green += (g * level - g_green) * slew;
  g_blue += (b * level - g_blue) * slew;
  uint8_t lit = player_lights(players, shake);
  if (S.chapter == HS_CH_LISTENING && !g_follow_music) lit = 0;
  hs_link_set_lights_show(true, (uint8_t)lrintf(g_red * 255), (uint8_t)lrintf(g_green * 255), (uint8_t)lrintf(g_blue * 255), lit, mic);
  // The same on the model. Its filament keeps its own brightness and takes the bar's colour, as gently as the bar does.
  float most = fmaxf(r, fmaxf(g, b));
  g_tint += (simd_make_float3(r / most, g / most, b / most) - g_tint) * slew;
  scene->filament_tint = g_tint;
  scene->player_lights = lit;
  scene->mic_light = mic != 0;
}

void hs_lights_stop(void) {
  g_red = g_green = g_blue = g_struck = 0;
  hs_link_set_lights_show(false, 0, 0, 0, 0, 0);
}
