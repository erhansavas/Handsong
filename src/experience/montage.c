// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The opening, 40 seconds from darkness to the hand: parts arrive from the dark, each with its own material and passage through the
// hands; the name is written in fire and lights what crosses it; everything comes together, and seven ticks lock it into your hands.
#include "experience/director.h"
#include "experience/fx_internal.h"
#include "hands/hands.h"
#include "sound/sound.h"
#include "world/crew.h"
#include "world/world.h"
#include <math.h>
#include <string.h>

typedef struct { const char *prefix; float arrive, collapse, lock; float out_x, out_y, out_z; hs_sound_material material; float pitch; int ticks; } Group;

// When each kind of part comes out of the dark, when it closes in, and at which of the seven final ticks it locks home.
static const Group kGroups[] = {
  {"gear_", 4.0f, 27.0f, -1, 1.15f, -0.020f, 0.0f, HS_MAT_SOUND_BRASS, 1500, 3},
  {"motor_", 6.2f, 27.0f, -1, 1.15f, -0.028f, 0.0f, HS_MAT_SOUND_STEEL, 500, 0},
  {"plate", 6.8f, 26.5f, -1, 1.0f, -0.010f, 0.0f, HS_MAT_SOUND_BRASS, 420, 0},
  {"coil_", 8.2f, 26.8f, -1, 1.2f, -0.045f, 0.0f, HS_MAT_SOUND_BRASS, 240, 0},
  {"pillars", 9.8f, 26.5f, -1, 1.0f, 0.012f, 0.0f, HS_MAT_SOUND_STEEL, 1900, 0},
  {"pillar_screws", 10.2f, 26.5f, -1, 1.0f, 0.020f, 0.0f, HS_MAT_SOUND_STEEL, 2600, 0},
  {"gimbal", 10.6f, 27.3f, -1, 1.0f, 0.010f, 0.0f, HS_MAT_SOUND_BRASS, 1100, 0},
  {"stick_", 11.2f, 27.6f, 6, 1.0f, 0.040f, 0.0f, HS_MAT_SOUND_BRASS, 800, 0},
  {"key_", 11.9f, 27.6f, 5, 1.0f, 0.042f, 0.0f, HS_MAT_SOUND_CERAMIC, 1300, 0},
  {"cross", 12.3f, 27.6f, 4, 1.0f, 0.042f, 0.0f, HS_MAT_SOUND_STEEL, 1500, 0},
  {"trigger_", 12.8f, 27.6f, 3, 1.0f, -0.010f, -0.030f, HS_MAT_SOUND_STEEL, 700, 0},
  {"shoulder_", 13.2f, 27.6f, 0, 1.0f, 0.012f, -0.035f, HS_MAT_SOUND_STEEL, 900, 0},
  {"hall_", 22.5f, 25.0f, -1, 1.0f, 0.018f, -0.010f, HS_MAT_SOUND_BRASS, 330, 0},
  {"floor_", 22.6f, 25.0f, -1, 1.0f, 0.018f, -0.010f, HS_MAT_SOUND_CERAMIC, 700, 0},
  {"lid", 22.7f, 25.0f, -1, 1.0f, 0.022f, -0.010f, HS_MAT_SOUND_BRASS, 500, 0},
  {"cone", 22.7f, 25.0f, -1, 1.0f, 0.012f, -0.010f, HS_MAT_SOUND_PAPER, 300, 0},
  {"cylinder", 22.8f, 25.0f, -1, 1.0f, 0.030f, -0.030f, HS_MAT_SOUND_BRASS, 900, 0},
  {"comb", 22.9f, 25.0f, -1, 1.0f, 0.030f, -0.040f, HS_MAT_SOUND_STEEL, 1200, 0},
  {"tine_", 23.0f, 25.0f, -1, 1.0f, 0.034f, -0.040f, HS_MAT_SOUND_STEEL, 2200, 0},
  {"bellows_", 23.8f, 26.0f, -1, 1.1f, 0.016f, 0.0f, HS_MAT_SOUND_PAPER, 260, 0},
  {"pipes", 24.0f, 26.0f, -1, 1.0f, 0.012f, 0.0f, HS_MAT_SOUND_BRASS, 1700, 0},
  {"gauge", 24.2f, 26.0f, -1, 1.0f, 0.020f, 0.0f, HS_MAT_SOUND_CERAMIC, 1500, 0},
  {"needle", 24.2f, 26.0f, -1, 1.0f, 0.020f, 0.0f, HS_MAT_SOUND_STEEL, 3000, 0},
  {"bezel", 24.6f, 26.2f, -1, 1.0f, 0.040f, -0.010f, HS_MAT_SOUND_BRASS, 620, 0},
  {"filament", 24.6f, 26.2f, -1, 1.0f, 0.040f, -0.010f, HS_MAT_SOUND_GLASS, 2800, 0},
  {"dome", 25.2f, 27.0f, -1, 1.0f, 0.070f, -0.010f, HS_MAT_SOUND_GLASS, 620, 0},
  {"body", 26.5f, 29.5f, -1, 1.0f, -0.075f, 0.0f, HS_MAT_SOUND_GLASS, 300, 0},
  {"pill_", 27.0f, 28.5f, 1, 1.0f, 0.030f, 0.0f, HS_MAT_SOUND_CERAMIC, 2000, 0},
  {"home", 27.0f, 28.5f, 2, 1.0f, 0.030f, 0.0f, HS_MAT_SOUND_BELL, 1500, 0},
  {"mute", 27.0f, 28.5f, 2, 1.0f, 0.030f, 0.0f, HS_MAT_SOUND_BRASS, 1800, 0},
};
enum { kGroupCount = sizeof kGroups / sizeof kGroups[0] };

static const float kTicks[7] = {30.0f, 30.85f, 31.5f, 31.95f, 32.3f, 32.55f, 32.72f};

static simd_float4x4 g_extra[HS_MAX_PART_POSES];
static uint8_t g_hidden[HS_MAX_PART_POSES];
static int8_t g_group_of[HS_MAX_PART_POSES];
static float g_last_t;
static bool g_arrived[HS_MAX_PART_POSES];
static float g_flash;

// ---------------------------------------------------------------------------- The name, a single line written by hand
enum { kPenPoints = 900 };
static simd_float4 g_pen[kPenPoints];
static int g_pen_count;

static const float kStrokes[][2] = {
  // H, down, down, and across into the a.
  {0.10f, 2.05f}, {0.06f, 1.4f}, {0.03f, 0.7f}, {0.0f, 0.0f}, {-9, -9},
  {0.85f, 2.05f}, {0.82f, 1.4f}, {0.78f, 0.7f}, {0.76f, 0.0f}, {-9, -9},
  {-0.05f, 0.95f}, {0.4f, 1.05f}, {0.78f, 1.02f}, {1.2f, 0.92f}, {-9, -9},
  // a
  {1.78f, 0.85f}, {1.5f, 1.02f}, {1.2f, 0.9f}, {1.08f, 0.5f}, {1.2f, 0.1f}, {1.5f, 0.02f}, {1.72f, 0.35f}, {1.78f, 0.9f}, {1.76f, 0.35f}, {1.85f, 0.05f}, {2.05f, 0.12f}, {2.22f, 0.5f},
  // n
  {2.27f, 0.98f}, {2.26f, 0.5f}, {2.25f, 0.0f}, {2.27f, 0.42f}, {2.42f, 0.85f}, {2.7f, 1.0f}, {2.88f, 0.7f}, {2.88f, 0.25f}, {2.98f, 0.03f}, {3.2f, 0.15f}, {3.35f, 0.6f}, {-9, -9},
  // d
  {3.95f, 0.85f}, {3.7f, 1.02f}, {3.4f, 0.9f}, {3.3f, 0.5f}, {3.4f, 0.1f}, {3.7f, 0.02f}, {3.95f, 0.4f}, {4.0f, 1.0f}, {4.02f, 1.6f}, {4.04f, 2.1f}, {4.0f, 1.4f}, {3.98f, 0.6f}, {4.02f, 0.12f}, {4.2f, 0.03f}, {4.42f, 0.35f},
  // s
  {4.66f, 1.0f}, {4.84f, 0.55f}, {4.74f, 0.12f}, {4.45f, 0.07f}, {-9, -9},
  // o
  {5.32f, 0.97f}, {5.05f, 0.92f}, {4.92f, 0.5f}, {5.05f, 0.08f}, {5.35f, 0.02f}, {5.56f, 0.35f}, {5.53f, 0.8f}, {5.32f, 0.97f}, {5.5f, 0.93f}, {5.75f, 0.72f},
  // n
  {5.87f, 0.98f}, {5.86f, 0.5f}, {5.85f, 0.0f}, {5.87f, 0.42f}, {6.02f, 0.85f}, {6.3f, 1.0f}, {6.48f, 0.7f}, {6.48f, 0.25f}, {6.58f, 0.03f}, {6.8f, 0.15f}, {6.95f, 0.6f}, {-9, -9},
  // g
  {7.55f, 0.85f}, {7.3f, 1.02f}, {7.0f, 0.9f}, {6.9f, 0.5f}, {7.0f, 0.1f}, {7.3f, 0.02f}, {7.55f, 0.4f}, {7.6f, 1.0f}, {7.6f, 0.2f}, {7.55f, -0.5f}, {7.25f, -0.85f}, {6.95f, -0.6f}, {7.2f, -0.2f}, {7.7f, 0.05f}, {8.05f, 0.3f},
};
enum { kStrokePoints = sizeof kStrokes / sizeof kStrokes[0] };

static void build_pen(void) {
  // Catmull-Rom through each run of points, as evenly as the points are spaced, in metres in front of the parts.
  const float scale = 0.0118f, x0 = -4.0f * scale, y0 = 0.062f, z0 = 0.034f;
  float total = 0;
  int count = 0;
  static float samples[kPenPoints][3];
  int i = 0;
  while (i < kStrokePoints) {
    int end = i;
    while (end < kStrokePoints && kStrokes[end][0] > -5) end++;
    for (int k = i; k + 1 < end; k++) {
      const float *p0 = kStrokes[k > i ? k - 1 : k], *p1 = kStrokes[k], *p2 = kStrokes[k + 1], *p3 = kStrokes[k + 2 < end ? k + 2 : k + 1];
      for (int s = 0; s < 10 && count < kPenPoints - 2; s++) {
        float t = (float)s / 10, t2 = t * t, t3 = t2 * t;
        float x = 0.5f * (2 * p1[0] + (p2[0] - p0[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (3 * p1[0] - p0[0] - 3 * p2[0] + p3[0]) * t3);
        float y = 0.5f * (2 * p1[1] + (p2[1] - p0[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (3 * p1[1] - p0[1] - 3 * p2[1] + p3[1]) * t3);
        samples[count][0] = x;
        samples[count][1] = y;
        samples[count][2] = 0;
        count++;
      }
    }
    if (count < kPenPoints - 2 && end > i) {
      samples[count][0] = kStrokes[end - 1][0];
      samples[count][1] = kStrokes[end - 1][1];
      samples[count][2] = 0;
      count++;
      samples[count][0] = samples[count][1] = 0;
      samples[count][2] = 1;     // the pen lifts here
      count++;
    }
    i = end + 1;
  }
  for (int k = 1; k < count; k++) {
    if (samples[k][2] > 0 || samples[k - 1][2] > 0) continue;
    total += hypotf(samples[k][0] - samples[k - 1][0], samples[k][1] - samples[k - 1][1]);
  }
  float along = 0;
  for (int k = 0; k < count; k++) {
    if (samples[k][2] > 0) {
      g_pen[k] = simd_make_float4(0, 0, 0, -1);
      continue;
    }
    if (k > 0 && samples[k - 1][2] == 0) along += hypotf(samples[k][0] - samples[k - 1][0], samples[k][1] - samples[k - 1][1]);
    g_pen[k] = simd_make_float4(x0 + samples[k][0] * scale, y0 + samples[k][1] * scale, z0, along / total);
  }
  g_pen_count = count;
}

static simd_float3 pen_tip(float progress) {
  simd_float3 best = simd_make_float3(0, 0.06f, 0.03f);
  for (int k = 0; k < g_pen_count; k++) {
    if (g_pen[k].w >= 0 && g_pen[k].w <= progress) best = g_pen[k].xyz;
  }
  return best;
}

// ---------------------------------------------------------------------------- Timeline
typedef struct { float t; float focus[3], offset[3]; float fov; } CameraKey;
static const CameraKey kCamera[] = {
  {0.0f, {-0.070f, -0.010f, 0.010f}, {-0.12f, 0.03f, 0.13f}, 30},
  {6.0f, {-0.060f, -0.012f, 0.005f}, {-0.05f, 0.05f, 0.12f}, 30},
  {10.0f, {0.0f, -0.010f, 0.005f}, {0.02f, 0.07f, 0.17f}, 32},
  {14.0f, {0.04f, 0.010f, 0.005f}, {0.04f, 0.05f, 0.20f}, 34},
  {16.0f, {0.0f, 0.045f, 0.020f}, {0.0f, 0.035f, 0.205f}, 34},
  {22.0f, {0.0f, 0.040f, 0.020f}, {0.0f, 0.040f, 0.205f}, 33},
  {26.0f, {0.0f, 0.005f, -0.005f}, {0.0f, 0.085f, 0.13f}, 32},
  {30.0f, {0.0f, 0.0f, -0.005f}, {0.04f, 0.16f, 0.16f}, 32},
  {33.0f, {0.0f, -0.002f, -0.005f}, {0.0f, 0.205f, 0.185f}, 30},
  {38.0f, {0.0f, -0.002f, -0.008f}, {0.0f, 0.170f, 0.150f}, 30},
  {40.0f, {0.0f, -0.002f, -0.008f}, {0.0f, 0.170f, 0.150f}, 30},
};
enum { kCameraKeys = sizeof kCamera / sizeof kCamera[0] };

static void enter(void) {
  if (!g_pen_count) build_pen();
  const hs_mesh_pack *pack = S.pack;
  for (uint32_t i = 0; i < pack->part_count && i < HS_MAX_PART_POSES; i++) {
    g_group_of[i] = -1;
    g_arrived[i] = false;
    const char *name = pack->parts[i].name;
    for (int g = 0; g < kGroupCount; g++) {
      if (!strncmp(name, kGroups[g].prefix, strlen(kGroups[g].prefix))) { g_group_of[i] = (int8_t)g; break; }
    }
    g_extra[i] = matrix_identity_float4x4;
    g_hidden[i] = 1;
  }
  S.scene.part_extra = g_extra;
  S.scene.part_hidden = g_hidden;
  S.hand_weight = 0;
  S.env = 0;
  S.backdrop = 0;
  S.filament = 0;
  S.awake = 0;
  S.camera_speed = 2.2f;
  S.mover_power = 0;
  g_last_t = 0;
  g_flash = 0;
  hs_crew_set_awake(0);
  hs_fx_set_click_mode(HS_CLICKS_SILENT);    // before the parts exist there is nothing to click
  hs_live_set_world_gain(0, 0);         // the crew land in the dark without a sound; the room is theirs only once it exists
  hs_sound_set(HS_SOUND_DRONE, 0, 0.1f);
  hs_set_shot(simd_make_float3(kCamera[0].focus[0], kCamera[0].focus[1], kCamera[0].focus[2]), simd_make_float3(kCamera[0].offset[0], kCamera[0].offset[1], kCamera[0].offset[2]), kCamera[0].fov, false);
  // The room is already resolved around the crew, under true gravity, before anyone can see it.
}

static bool crossed(float mark, float t) { return g_last_t < mark && t >= mark; }

static void place_part(int i, float t) {
  const Group *g = g_group_of[i] >= 0 ? &kGroups[g_group_of[i]] : NULL;
  if (!g) {
    const hs_part *part = &S.pack->parts[i];
    g_hidden[i] = !strncmp(part->name, "lattice", 7) ? 1 : part->parent >= 0 ? t < 27.0f : t < 24.0f;
    g_extra[i] = matrix_identity_float4x4;
    return;
  }
  const hs_part *part = &S.pack->parts[i];
  if (!strncmp(part->name, "lattice", 7)) { g_hidden[i] = 1; return; }
  float cx = (part->bounds_min[0] + part->bounds_max[0]) / 2, cz = (part->bounds_min[2] + part->bounds_max[2]) / 2;
  float arrive = g->arrive + fmodf(fabsf(cx) * 41 + fabsf(cz) * 83, 1.0f) * 0.9f;
  g_hidden[i] = t < arrive - 3.2f;
  if (g_hidden[i]) return;
  // Exploded place: spread out from the middle, in layers.
  float close_start = g->collapse - 1.5f, close = hs_stage_ease((t - close_start) / 2.2f);
  float out = 1 - close;
  simd_float3 exploded = simd_make_float3(cx * (g->out_x - 1), g->out_y, g->out_z) * out;
  // On the way in: from far outside, curving past the viewer's side.
  float in = 1 - hs_stage_ease((t - (arrive - 3.2f)) / 3.2f);
  float side = cx >= 0 ? 1 : -1;
  simd_float3 approach = simd_make_float3(side * 0.28f * in * in, -0.09f * in, 0.22f * in * in * in);
  // The last ones hover a finger's breadth away, until their tick drives them home.
  simd_float3 hover = simd_make_float3(0, 0, 0);
  if (g->lock >= 0) {
    float tick = kTicks[(int)g->lock];
    float home = t >= tick ? 1 : 0;
    float settle = t >= tick ? expf(-(t - tick) * 40) * cosf((t - tick) * 90) : 1;
    hover = simd_make_float3(0, 0.0035f * (1 - home + home * settle), 0);
  }
  simd_float4x4 m = matrix_identity_float4x4;
  simd_float3 at = exploded + approach + hover;
  // A slow tumble while it is still free.
  float tumble = (in * in * 1.8f) + out * 0.06f * sinf(t * 0.7f + cx * 90);
  simd_float3 center = simd_make_float3(cx, (part->bounds_min[1] + part->bounds_max[1]) / 2, cz);
  simd_quatf spin = simd_quaternion(tumble, simd_normalize(simd_make_float3(0.4f, 0.8f, 0.3f + side * 0.3f)));
  simd_float4x4 r = simd_matrix4x4(spin);
  simd_float3 rotated = simd_act(spin, center);
  m = r;
  m.columns[3] = simd_make_float4(center - rotated + at, 1);
  g_extra[i] = m;
  // Arriving in its place makes its sound and its passage through the hands, at its own side of the object.
  if (!g_arrived[i] && t >= arrive) {
    g_arrived[i] = true;
    float pan = fmaxf(-1, fminf(1, cx / 0.08f));
    float loud = S.hurry > 3 ? 0.25f : 1;
    hs_sound_strike(g->material, g->pitch * (0.92f + 0.16f * fmodf(fabsf(cz) * 310, 1.0f)), 0.055f * loud, pan * 0.8f, HS_TIMING_ALIGNED);
    hs_hands_hit(70 + g->pitch * 0.06f, 0.05f, 0.42f * loud, pan);
    for (int k = 0; k < g->ticks; k++) hs_hands_hit(160 + 30 * (float)k, 0.015f, 0.16f, pan);
  }
}

static void every_frame(void) {
  if (S.phase != HS_PHASE_AWAKEN) {
    S.scene.part_extra = NULL;
    S.scene.part_hidden = S.part_hidden;
    return;
  }
  float t = (float)S.phase_time;

  // Holding the lower key skips it all: a ring of light fills, then everything comes together fast.
  if (t < 33.0f) {
    if (S.down & HS_KEY_SOUTH) {
      S.south_held += S.dt;
      S.filament = fmaxf(S.filament, S.south_held / 0.6f * 2);
      if (S.south_held >= 0.6f) S.hurry = 14;
    } else {
      S.south_held = 0;
    }
  } else {
    S.hurry = 1;
  }

  // Each part's place.
  for (uint32_t i = 0; i < S.pack->part_count && i < HS_MAX_PART_POSES; i++) place_part((int)i, t);

  // The camera.
  int k = 0;
  while (k + 1 < kCameraKeys && kCamera[k + 1].t < t) k++;
  const CameraKey *a = &kCamera[k], *b = &kCamera[k + 1 < kCameraKeys ? k + 1 : k];
  float u = b->t > a->t ? hs_stage_ease((t - a->t) / (b->t - a->t)) : 0;
  simd_float3 focus = simd_make_float3(a->focus[0] + (b->focus[0] - a->focus[0]) * u, a->focus[1] + (b->focus[1] - a->focus[1]) * u, a->focus[2] + (b->focus[2] - a->focus[2]) * u);
  simd_float3 offset = simd_make_float3(a->offset[0] + (b->offset[0] - a->offset[0]) * u, a->offset[1] + (b->offset[1] - a->offset[1]) * u, a->offset[2] + (b->offset[2] - a->offset[2]) * u);
  hs_set_shot(focus, offset, a->fov + (b->fov - a->fov) * u, false);

  // The light: one moving strip in the dark, then the pen, then the room dawning.
  float dawn = hs_stage_ease((t - 22) / 8);
  S.env = 0.05f * hs_stage_ramp(t, 2.5f, 5.0f) + dawn * 0.95f;
  S.backdrop = hs_stage_ease((t - 24) / 6) * (1 - 0.15f * hs_stage_ramp(t, 33, 38));
  float moving = hs_stage_ramp(t, 2.5f, 4.0f) * (1 - hs_stage_ramp(t, 13.5f, 15.0f));
  float s = (t - 3) / 11;
  simd_float3 mover = simd_make_float3(-0.16f + 0.32f * s, 0.05f + 0.03f * sinf(t * 1.3f), 0.08f - 0.02f * cosf(t * 0.9f));
  simd_float3 mover_color = simd_make_float3(1.0f, 0.74f, 0.52f);
  float mover_power = moving * 42.0f;
  float mover_range = 0.42f;

  // The pen writes the name between 14 and 22 seconds, and is itself the light.
  S.scene.pen_points = g_pen;
  S.scene.pen_count = g_pen_count;
  float writing = hs_stage_ease((t - 14.2f) / 7.2f);
  S.scene.pen_progress = writing;
  S.scene.pen_brightness = hs_stage_ramp(t, 14.0f, 14.5f) * (1 - hs_stage_ramp(t, 23.0f, 25.5f));
  if (t >= 14.0f && t < 24.0f) {
    simd_float3 tip = pen_tip(writing);
    float alive = hs_stage_ramp(t, 14.0f, 14.6f) * (1 - hs_stage_ramp(t, 21.4f, 24.0f));
    mover = tip;
    mover_color = simd_make_float3(1.0f, 0.46f, 0.18f);
    mover_power = 30.0f * alive;
    mover_range = 0.24f;
    // What crosses the name is passed through the hands, left to right as the pen goes.
    float pan = fmaxf(-1, fminf(1, tip.x / 0.05f));
    hs_hands_set(HS_LAYER_SHEEN, 0.16f * alive, 240, pan);
    hs_hands_set(HS_LAYER_BODY_LEFT, 0.10f * alive * (0.5f - 0.5f * pan), 78, 0);
    hs_hands_set(HS_LAYER_BODY_RIGHT, 0.10f * alive * (0.5f + 0.5f * pan), 78, 0);
    hs_sound_set(HS_SOUND_HISS, 0.5f * alive, 0.6f);
    hs_sound_set(HS_SOUND_DRONE, 0.05f * alive + 0.02f, 0.05f + writing * 0.2f);
    if (g_last_t < 21 && (int)(writing * 90) != (int)(hs_stage_ease((g_last_t - 14.2f) / 7.2f) * 90)) {
      hs_spawn(HS_KIND_EMBER, tip, simd_make_float3(0, 0.006f, 0), 0.0008f, 0.008f, simd_make_float3(1.0f, 0.5f, 0.18f) * 2.5f, 0.00035f, 1.1f, 6);
    }
  } else if (t >= 24.0f && g_last_t < 24.0f) {
    hs_sound_set(HS_SOUND_HISS, 0, 0.6f);
  }
  S.mover = mover;
  S.mover_color = mover_color;
  S.mover_power = mover_power;
  S.mover_range = mover_range;

  // The first signs of life.
  if (crossed(1.2f, t)) {        // a heartbeat, barely there
    hs_hands_hit(58, 0.16f, 0.55f, 0);
    hs_sound_strike(HS_MAT_SOUND_PAPER, 58, 0.26f, 0, HS_TIMING_ALIGNED);
  }
  if (crossed(2.0f, t)) {        // the first ember and a brass tongue, far to the right
    hs_hands_hit(190, 0.05f, 0.14f, 0.7f);
    hs_sound_strike(HS_MAT_SOUND_BRASS, 1046, 0.025f, 0.8f, HS_TIMING_DIRECT);
    hs_spawn(HS_KIND_EMBER, simd_make_float3(0.03f, 0.01f, 0.03f), simd_make_float3(-0.008f, 0.01f, 0), 0.001f, 0.01f, simd_make_float3(1.0f, 0.5f, 0.18f) * 6, 0.0005f, 2.2f, 10);
  }
  if (t >= 3 && t < 14) {
    // The air between parts: a low restless passage through the hands, following the strip of light.
    float pan = fmaxf(-1, fminf(1, mover.x / 0.1f));
    float wave = 0.5f + 0.5f * sinf(t * 2.1f);
    hs_hands_set(HS_LAYER_SWELL, 0.13f + 0.07f * wave, 2.2f, pan);
    hs_sound_set(HS_SOUND_HISS, 0.10f, 0.25f);
    hs_sound_set(HS_SOUND_DRONE, 0.03f, 0.05f);
  } else if (t >= 14 && g_last_t < 14) {
    hs_hands_set(HS_LAYER_SWELL, 0, 2.2f, 0);
  }

  // The filament's ring wakes when it has arrived; the score comes in as the parts close.
  S.filament = fmaxf(S.filament * expf(-S.dt * 6), hs_stage_ramp(t, 25.6f, 29.0f) * 2.4f * (t >= 33.4f ? 1.0f : 1.0f - 0.5f * hs_stage_ramp(t, 33.0f, 33.1f)) + g_flash);
  g_flash *= expf(-S.dt * 9);
  if (crossed(22.0f, t)) {
    hs_sound_score(0, HS_STEM_OPEN_RISE, 1.0f, 0.5f, false);
    const hs_trigger_effect spring = hs_trigger_spring(1, 9, 1, 4);
    hs_fx_set_triggers(&spring, &spring);
  }
  if (t >= 22 && t < 30) {
    hs_hands_set(HS_LAYER_BODY_LEFT, 0.10f + 0.05f * dawn, 52, 0);
    hs_hands_set(HS_LAYER_BODY_RIGHT, 0.10f + 0.05f * dawn, 52, 0);
    hs_sound_set(HS_SOUND_DRONE, 0.03f * (1 - dawn), 0.1f);
  }
  if (t >= 26.5f && g_last_t < 26.5f) {
    hs_world_place(HS_PIP, 0.012f, -0.001f, -0.002f);    // the crew drop in, still asleep, as the room closes round them
  }
  // Seven ticks, faster and faster, and the last one locks everything into the hands.
  for (int i = 0; i < 7; i++) {
    if (!crossed(kTicks[i], t)) continue;
    float grow = (float)(i + 1) / 7;
    hs_sound_strike(HS_MAT_SOUND_STEEL, 1900 - (float)i * 110, 0.07f + 0.05f * grow, -0.2f + 0.066f * (float)i, HS_TIMING_ALIGNED);
    hs_hands_hit(170 - (float)i * 12, 0.03f, 0.35f + 0.3f * grow, -0.5f + 0.166f * (float)i);
    if (i == 6) {
      // The thunk: everything stops at once, in the hands and on the trigger motors too.
      hs_hands_hit(48, 0.26f, 1.0f, 0);
      hs_sound_strike(HS_MAT_SOUND_WOOD, 72, 0.9f, 0, HS_TIMING_ALIGNED);
      hs_sound_strike(HS_MAT_SOUND_BRASS, 180, 0.22f, 0, HS_TIMING_ALIGNED);
      hs_hands_set(HS_LAYER_BODY_LEFT, 0, 52, 0);
      hs_hands_set(HS_LAYER_BODY_RIGHT, 0, 52, 0);
      const hs_trigger_effect kick = hs_trigger_raw(255);
      hs_fx_set_triggers(&kick, &kick);
    }
  }
  if (crossed(33.05f, t)) {
    hs_sound_score_gain(0, 0, 0.02f);
    hs_sound_set(HS_SOUND_DRONE, 0, 0);
    const hs_trigger_effect spring = hs_trigger_spring(1, 9, 1, 4);
    hs_fx_set_triggers(&spring, &spring);
  }
  if (crossed(33.4f, t)) {       // after the held breath, the lights and the eyes open
    hs_sound_score(0, HS_STEM_OPEN_OPEN, 1.0f, 0.05f, false);
    hs_hands_hit(62, 0.8f, 0.34f, 0);
  }
  if (crossed(33.0f, t)) hs_fx_set_click_mode(HS_CLICKS_NORMAL);
  if (crossed(34.0f, t)) hs_live_set_world_gain(0.3f, 0);
  S.awake = hs_stage_ease((t - 33.6f) / 2.4f);
  hs_crew_set_awake(S.awake);
  if (t >= 33.4f) hs_hands_set(HS_LAYER_SHEEN, 0.05f * (1 - hs_stage_ramp(t, 35, 38)), 300, 0);

  // Into the hand: from here the pose slides from the script to where the player really holds it, with no jump.
  S.hand_weight = hs_stage_ease((t - 38.0f) / 2.0f);
  S.camera_speed = t > 33 ? 1.4f : 2.2f;
  g_last_t = t;
  if (t >= 39.9f) {
    S.scene.part_extra = NULL;
    S.scene.part_hidden = S.part_hidden;
    S.scene.pen_count = 0;
    hs_go_to_quietly(HS_CH_COILS);
  }
}

static bool press(uint32_t key) {
  if (S.phase_time >= 33.0f) return false;
  (void)key;
  // Before the parts exist, a press finds only the filament.
  hs_fx_faint();
  g_flash = 1.6f;
  return true;
}

const hs_chapter_def hs_montage_def = {40.0f, 0.0f, enter, every_frame, NULL, press};
