// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "experience/director.h"
#include "experience/voice_out.h"
#include "experience/fx_internal.h"
#include "hands/hands.h"
#include "listen/listen.h"
#include "sound/sound.h"
#include "world/crew.h"
#include "world/world.h"
#include <math.h>
#include <string.h>

hs_stage S;

static const hs_chapter_def *chapter_def(hs_chapter c) {
  static const hs_chapter_def *const table[HS_CH_COUNT] = {&hs_montage_def, &hs_coils_def, &hs_gears_def, &hs_lattice_def, &hs_balance_def, &hs_breath_def,
                                                           &hs_voice_def, &hs_finale_def, &hs_workshop_def, &hs_listening_def};
  return table[c];
}

float hs_stage_ease(float t) {
  t = fminf(1, fmaxf(0, t));
  return t * t * (3 - 2 * t);
}

float hs_stage_ramp(float x, float from, float to) { return fminf(1, fmaxf(0, (x - from) / (to - from))); }

const char *hs_text_line(int key) {
  static const char *const lines[HS_TEXT_COUNT] = {
    [HS_TEXT_PAUSED] = "Paused     {south} go on     {east} leave",
    [HS_TEXT_TURN_ON] = "Turn on your controller, then press its home button.",
    [HS_TEXT_BREATH_ASK] = "Let the instrument hear your breath. Nothing is recorded.",
    [HS_TEXT_BREATH_STOPPED] = "Not listening. The mute button under the touch surface starts it again.",
    [HS_TEXT_UNPLUG] = "Unplug the cable: Handsong works over Bluetooth.",
    [HS_TEXT_MOVE_ON_FIRST] = "{south} next     {sticks} look around     {press-stick} closer     {options} pause",
    [HS_TEXT_PAUSED_KEYS] = "Paused     Return goes on     Left arrow leaves",
    [HS_TEXT_KEYBOARD_FREE] = "No controller: free play needs one in your hands. Esc, then the left arrow, leaves.",
    [HS_TEXT_LISTENING_SPEAKER] = "Play anything on your Mac: it comes from the controller.     {sticks} look around     {east} leave",
    [HS_TEXT_VOICE_MAC] = "Listen: on this link the music plays from your Mac. {west} and {north} pluck a note.",
    [HS_TEXT_YOURS] = "It is yours now.",
    [HS_TEXT_SOFTER] = "The hands are softer on this Mac right now.",
    [HS_TEXT_BATTERY] = "The controller's battery is getting low.",
    [HS_TEXT_MOVE_ON] = "{south} next     {east} back     {sticks} look around     {press-stick} closer     {options} pause",
    [HS_TEXT_ALLOW_MIC] = "{south} allow the microphone     {east} back     {options} pause",
    [HS_TEXT_SKIP] = "Hold {south} to skip the opening",
    [HS_TEXT_HOLD_IT] = "This is the controller in your hands. Move it.",
    [HS_TEXT_COILS] = "Tilt the controller to roll the ball over glass, gravel, water and wood. Each feels different in your hands. {north} moves it on.",
    [HS_TEXT_GEARS] = "Pull {triggers} slowly, all the way to the stop. Then let go fast.",
    [HS_TEXT_LATTICE] = "Run a finger over {touch}. Click it to ring the bell.",
    [HS_TEXT_BALANCE] = "Tip and shake the controller: the crew inside feel it. Click {touch} to go in among them.",
    [HS_TEXT_INSIDE] = "{left-stick} walk     {right-stick} look around     Click {touch} to come back out",
    [HS_TEXT_BREATH] = "Hum or blow toward your Mac. The sand follows your voice.",
    [HS_TEXT_BREATH_NO_MIC] = "Pull {triggers}: the bellows shake the sand.",
    [HS_TEXT_VOICE] = "Listen: this music comes from the controller itself. {west} and {north} pluck a note.",
    [HS_TEXT_WORKSHOP] = "Free play. Everything is alive: press anything.",
    [HS_TEXT_WORKSHOP_KEYS] = "{touch} strum     {south} {west} {north} {east} notes     {triggers} steam     {sticks} look around     {press-stick} closer     {options} pause     Hold {east} to leave",
    [HS_TEXT_PAUSED_FREE] = "Paused     {south} go on     {north} the finale     {east} leave",
    [HS_TEXT_LISTENING] = "Play anything on your Mac and feel it.     {sticks} look around     {east} leave",
    [HS_TEXT_QUIT_ASK] = "Quit Handsong?     Command-Q again or {south} quits     Esc or {east} stays",
    [HS_TEXT_KEYBOARD] = "No controller: the arrow keys move through the story, and dragging turns it. A controller puts it in your hands.",
  };
  return lines[key];
}

void hs_show_text(int key, float alpha, float x, float y, float size) {
  S.scene.text_key = key;
  S.scene.text_alpha = alpha;
  S.scene.text_x = x;
  S.scene.text_y = y;
  S.scene.text_size = size;
}

void hs_set_shot(simd_float3 focus, simd_float3 offset, float fov, bool framed) { S.shot = (hs_shot){focus, offset, fov, framed}; }

simd_float3 hs_socket(const char *name) {
  int at = hs_mesh_pack_find(S.pack, name);
  if (at < 0) return simd_make_float3(0, 0, 0);
  const hs_part *p = &S.pack->parts[at];
  return simd_make_float3(p->pivot[0], p->pivot[1], p->pivot[2]);
}

static bool g_modest;
void hs_director_set_modest(bool modest) { g_modest = modest; }

void hs_spawn(int kind, simd_float3 position, simd_float3 velocity, float spread, float speed_spread, simd_float3 color, float size, float life, int count) {
  if (S.scene.spawn_count >= HS_MAX_SPAWNS) return;
  if (g_modest) count = count / 3 + 1;
  S.scene.spawns[S.scene.spawn_count++] = (hs_particle_spawn){position, spread, velocity, speed_spread, color, size, life, (float)kind, count, 0};
}

// ---------------------------------------------------------------------------- Input
static float stick_axis(uint8_t raw) {
  float v = ((float)raw - 127.5f) / 127.5f;
  float dead = 0.07f;
  float magnitude = fabsf(v);
  if (magnitude < dead) return 0;
  return copysignf((magnitude - dead) / (1 - dead), v);
}

static void read_input(const hs_frame_in *in) {
  uint32_t before = S.input.keys;
  float l2_before = S.l2, r2_before = S.r2;
  S.input = in->input;
  S.connected = in->connected;
  S.down = in->connected ? in->input.keys : in->keyboard_keys;       // the keyboard still answers when a controller has gone
  S.pressed = S.down & ~before;
  S.released = before & ~S.down;
  S.lx = stick_axis(in->input.left_x);
  S.ly = stick_axis(in->input.left_y);
  S.rx = stick_axis(in->input.right_x);
  S.ry = stick_axis(in->input.right_y);
  S.l2 = in->connected ? (float)in->input.left_trigger / 255.0f : 0;
  S.r2 = in->connected ? (float)in->input.right_trigger / 255.0f : 0;
  float rate = 1 / fmaxf(S.dt, 0.001f);
  S.l2_speed += ((S.l2 - l2_before) * rate - S.l2_speed) * 0.5f;
  S.r2_speed += ((S.r2 - r2_before) * rate - S.r2_speed) * 0.5f;
  for (int i = 0; i < 2; i++) {
    bool touching = in->connected && in->input.touch[i].down;
    S.touch_began[i] = touching && !S.touching[i];
    S.touch_ended[i] = !touching && S.touching[i];
    if (touching) {
      // The pad maps onto the dome, keeping its own proportions inside the hall.
      float x = ((float)in->input.touch[i].x / 1919.0f - 0.5f) * 2, y = ((float)in->input.touch[i].y / 1079.0f - 0.5f) * 2;
      float wx = x * 0.0255f, wz = kHallCenterZ + y * 0.0200f;
      if (S.touch_began[i]) { S.touch_x[i] = wx; S.touch_z[i] = wz; S.touch_age[i] = 0; }
      float moved = hypotf(wx - S.touch_x[i], wz - S.touch_z[i]);
      S.touch_speed[i] += (moved * rate - S.touch_speed[i]) * 0.3f;
      S.touch_x[i] = wx;
      S.touch_z[i] = wz;
    } else {
      S.touch_speed[i] = 0;
    }
    S.touching[i] = touching;
    S.touch_age[i] += S.dt;
  }
}

// ---------------------------------------------------------------------------- Every control's own travel
static inline void key_spring(float *value, float *speed, float target, float dt) {
  // Stiff and a little under-damped: a real key lands, a sprung one rebounds.
  *speed += (target - *value) * 2600 * dt - *speed * 42 * dt;
  *value += *speed * dt;
}

static void plain_reaction(uint32_t key) {
  switch (key) {
    case HS_KEY_WEST: hs_fx_key(0); break;
    case HS_KEY_SOUTH: hs_fx_key(1); break;
    case HS_KEY_EAST: hs_fx_key(2); break;
    case HS_KEY_NORTH: hs_fx_key(3); break;
    case HS_KEY_L1: hs_fx_shoulder(0); break;
    case HS_KEY_R1: hs_fx_shoulder(1); break;
    case HS_KEY_L3: case HS_KEY_R3:       // either stick: the line at the bottom draws one and says closer
      hs_fx_stick_press(key == HS_KEY_R3);
      if (S.phase != HS_PHASE_LEAVING && S.chapter != HS_CH_LISTENING && !S.inside) S.closer_want = S.closer_want > 0.5f ? 0 : 1;
      break;
    case HS_KEY_TOUCH: hs_fx_dome_click(S.touching[0] ? S.touch_x[0] / 0.0255f : 0); break;
    case HS_KEY_HOME: hs_fx_home(); break;
    case HS_KEY_CREATE: hs_fx_pill(0); break;
    case HS_KEY_OPTIONS: hs_fx_pill(1); break;
    case HS_KEY_MUTE: hs_fx_mute(); break;
    case HS_KEY_UP: hs_fx_cross(0, -1); break;
    case HS_KEY_DOWN: hs_fx_cross(0, 1); break;
    case HS_KEY_LEFT: hs_fx_cross(-1, 0); break;
    case HS_KEY_RIGHT: hs_fx_cross(1, 0); break;
    default: break;
  }
}

// Each magnet is a mass on two springs: what reaches the hands kicks it, and it rings back to rest, as a bass driver does.
static void move_coils(float dt) {
  static float position[2], speed[2], before[2];
  const float kOmega = 2 * (float)M_PI * 9, kDamping = 0.16f, kKick = 62, kTremble = 0.22f;
  float hands[2];
  hs_hands_view(&hands[0], &hands[1]);
  if (S.chapter == HS_CH_LISTENING) hands[0] = hands[1] = 0;      // the music's own stream is what moves them there
  for (int side = 0; side < 2; side++) {
    float level = fminf(1, fmaxf(hands[side] * 2.2f, S.coil[side]));   // a chapter may drive them itself
    S.coil[side] = 0;
    if (level > before[side]) speed[side] += (level - before[side]) * kKick;
    before[side] = level;
    float rest = sinf((float)S.seconds * 2 * (float)M_PI * 9 + side) * level * kTremble;     // a held sound trembles rather than sitting off centre
    for (int step = 0; step < 4; step++) {
      float h = dt / 4;
      speed[side] += ((rest - position[side]) * kOmega * kOmega - speed[side] * 2 * kDamping * kOmega) * h;
      position[side] += speed[side] * h;
      if (fabsf(position[side]) > 1) { position[side] = copysignf(1, position[side]); speed[side] *= -0.3f; }
    }
    S.scene.coil[side] = position[side];
  }
}

static void update_rig(void) {
  float dt = S.dt;
  const uint32_t face[4] = {HS_KEY_WEST, HS_KEY_SOUTH, HS_KEY_EAST, HS_KEY_NORTH};
  for (int i = 0; i < 4; i++) key_spring(&S.keys[i], &S.keys_v[i], (S.down & face[i]) ? 1 : 0, dt);
  float cross_x = ((S.down & HS_KEY_RIGHT) ? 1.0f : 0.0f) - ((S.down & HS_KEY_LEFT) ? 1.0f : 0.0f);
  float cross_y = ((S.down & HS_KEY_DOWN) ? 1.0f : 0.0f) - ((S.down & HS_KEY_UP) ? 1.0f : 0.0f);
  key_spring(&S.cross[0], &S.cross_v[0], cross_x, dt);
  key_spring(&S.cross[1], &S.cross_v[1], cross_y, dt);
  key_spring(&S.dome_press, &S.dome_press_v, (S.down & HS_KEY_TOUCH) ? 1 : 0, dt);
  float k = 1 - expf(-dt * 42);
  S.shoulders[0] += (((S.down & HS_KEY_L1) ? 1.0f : 0.0f) - S.shoulders[0]) * k;
  S.shoulders[1] += (((S.down & HS_KEY_R1) ? 1.0f : 0.0f) - S.shoulders[1]) * k;
  S.pills[0] += (((S.down & HS_KEY_CREATE) ? 1.0f : 0.0f) - S.pills[0]) * k;
  S.pills[1] += (((S.down & HS_KEY_OPTIONS) ? 1.0f : 0.0f) - S.pills[1]) * k;
  S.home += (((S.down & HS_KEY_HOME) ? 1.0f : 0.0f) - S.home) * k;
  S.mute += (((S.down & HS_KEY_MUTE) ? 1.0f : 0.0f) - S.mute) * k;
  S.stick_press[0] += (((S.down & HS_KEY_L3) ? 1.0f : 0.0f) - S.stick_press[0]) * k;
  S.stick_press[1] += (((S.down & HS_KEY_R3) ? 1.0f : 0.0f) - S.stick_press[1]) * k;
  for (int side = 0; side < 2; side++) {
    float x = side ? S.rx : S.lx, y = side ? S.ry : S.ly;
    bool at_limit = hypotf(x, y) > 0.97f;
    if (at_limit && !S.stick_at_limit[side]) hs_fx_stick_limit(side);
    S.stick_at_limit[side] = at_limit;
  }
  hs_scene *s = &S.scene;
  s->sticks[0][0] = S.lx; s->sticks[0][1] = S.ly;
  s->sticks[1][0] = S.rx; s->sticks[1][1] = S.ry;
  s->stick_press[0] = S.stick_press[0]; s->stick_press[1] = S.stick_press[1];
  s->triggers[0] = S.l2; s->triggers[1] = S.r2;
  s->shoulders[0] = S.shoulders[0]; s->shoulders[1] = S.shoulders[1];
  memcpy(s->keys, S.keys, sizeof s->keys);
  s->cross[0] = S.cross[0]; s->cross[1] = S.cross[1];
  s->pills[0] = S.pills[0]; s->pills[1] = S.pills[1];
  s->home = S.home; s->mute = S.mute;
  s->dome_press = S.dome_press;
  // The going train runs while the instrument is alive; the trigger train is the chapter's own.
  for (int side = 0; side < 2; side++) s->gear_turn[side] = S.gear_turn[side] + (float)S.seconds * 0.35f * S.awake * 0.0f;
  s->bellows[0] = S.bellows[0]; s->bellows[1] = S.bellows[1];
  s->needle = S.needle;
  s->cone = S.cone;
  s->lid = S.lid;
  move_coils(dt);
  s->filament = S.filament;
}

// ---------------------------------------------------------------------------- The hand
static void update_hand(const hs_frame_in *in) {
  float dt = S.dt;
  if (S.connected) {
    hs_motion_view view = hs_motion_at(in->target_ms, in->now_ms, dt);
    S.hand_turn = view.turn;
    float pitch = hs_motion_hold_pitch(view.pose);
    if (!S.hold_known) { S.hold_pitch = pitch; S.hold_known = 1; }
    S.hold_pitch += (pitch - S.hold_pitch) * (1 - expf(-dt / 1.6f));
    hs_live_set_hold_pitch(S.hold_pitch);
    S.hand = simd_normalize(simd_mul(simd_quaternion(-S.hold_pitch, simd_make_float3(1, 0, 0)), view.pose));
  } else {
    // Without a hand it settles to a gentle resting turn, with no jump.
    simd_quatf rest = simd_quaternion(sinf((float)S.seconds * 0.2f) * 0.07f, simd_make_float3(0, 1, 0));
    S.hand = simd_normalize(simd_slerp(S.hand, rest, 1 - expf(-dt * 0.9f)));
    S.hand_turn = simd_make_float3(0, 0, 0);
  }
  hs_live_set_hand_weight(S.hand_weight);
  simd_quatf none = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  // The hand still moves everything in the hall through the physics, whatever share of it the picture shows.
  float wanted_share = S.inside ? 0.0f : S.chapter == HS_CH_LATTICE || S.chapter == HS_CH_BREATH || S.chapter == HS_CH_VOICE || S.chapter == HS_CH_WORKSHOP ? 0.4f : 1.0f;
  S.hand_share_now += (wanted_share - S.hand_share_now) * (1 - expf(-dt * 2.5f));
  S.shown = simd_normalize(simd_slerp(none, S.hand, S.hand_weight * S.hand_share_now));
  S.object = simd_matrix4x4(S.shown);
}

// ---------------------------------------------------------------------------- The camera
static inline void critical(float *x, float *v, float want, float w, float dt) {
  *v += ((want - *x) * w * w - *v * 2 * w) * dt;
  *x += *v * dt;
}

static inline void critical3(simd_float3 *x, simd_float3 *v, simd_float3 want, float w, float dt) {
  *v += ((want - *x) * (w * w) - *v * (2 * w)) * dt;
  *x += *v * dt;
}

static void update_camera(void) {
  float dt = S.dt, w = S.camera_speed * S.hurry;
  // Sticks take the camera a little way round the object, on a sprung leash. The object stays one to one with the hand.
  float reach = S.phase == HS_PHASE_PLAY ? 1.0f : 0.25f;
  if (S.inside) reach = 0;      // walking and looking have the sticks in there
  float want_yaw = (S.lx + S.rx) * 0.5f * 0.75f * reach, want_pitch = -(S.ly + S.ry) * 0.5f * 0.45f * reach;
  S.orbit_yaw += (want_yaw - S.orbit_yaw) * (1 - expf(-dt * 5));
  S.orbit_pitch += (want_pitch - S.orbit_pitch) * (1 - expf(-dt * 5));
  critical(&S.closer, &S.closer_v, S.closer_want, 5, dt);

  simd_float3 focus = simd_act(S.shown, S.shot.focus);
  simd_float3 offset = S.shot.offset;
  float c = cosf(S.orbit_yaw), s = sinf(S.orbit_yaw);
  offset = simd_make_float3(c * offset.x + s * offset.z, offset.y, -s * offset.x + c * offset.z);
  float along = simd_length(simd_make_float3(offset.x, 0, offset.z)), elevation = atan2f(offset.y, along) + S.orbit_pitch, reach_len = simd_length(offset);
  float heading = atan2f(offset.x, offset.z);
  elevation = fmaxf(-0.2f, fminf(1.5f, elevation));
  offset = simd_make_float3(sinf(heading) * cosf(elevation), sinf(elevation), cosf(heading) * cosf(elevation)) * reach_len;
  offset *= 1 - S.closer * 0.55f;

  // Keep the whole object in the frame whatever the grip: the object's corners tell how far back the camera must stand.
  float need = 1;
  if (S.shot.framed && S.closer < 0.1f) {
    const hs_mesh_pack *pack = S.pack;
    (void)pack;
    static const float corners[8][3] = {{-.092f, -.03f, -.05f}, {.092f, -.03f, -.05f}, {-.092f, .02f, -.05f}, {.092f, .02f, -.05f},
                                        {-.092f, -.03f, .045f}, {.092f, -.03f, .045f}, {-.092f, .02f, .045f}, {.092f, .02f, .045f}};
    simd_float3 eye = focus + offset * S.frame_scale;
    simd_float3 forward = simd_normalize(focus - eye), right = simd_normalize(simd_cross(forward, simd_make_float3(0, 1, 0))), up = simd_cross(right, forward);
    float tan_y = tanf(S.shot.fov * (float)M_PI / 360), tan_x = tan_y * 2560.0f / 1664.0f, most = 0;
    for (int i = 0; i < 8; i++) {
      simd_float3 p = simd_act(S.shown, simd_make_float3(corners[i][0], corners[i][1], corners[i][2])) - eye;
      float z = fmaxf(simd_dot(p, forward), 0.01f);
      most = fmaxf(most, fmaxf(fabsf(simd_dot(p, right)) / (z * tan_x), fabsf(simd_dot(p, up)) / (z * tan_y)));
    }
    need = S.frame_scale * most / 0.90f;
  }
  need = fmaxf(1, need);
  critical(&S.frame_scale, &S.frame_scale_v, need, need > S.frame_scale ? 4.0f : 1.2f, dt);
  simd_float3 eye_want = focus + offset * S.frame_scale;
  critical3(&S.eye, &S.eye_v, eye_want, w, dt);
  critical3(&S.target, &S.target_v, focus, w, dt);
  critical(&S.fov, &S.fov_v, S.shot.fov, w, dt);
  S.scene.eye = S.eye;
  S.scene.target = S.target;
  S.scene.fov_degrees = S.fov;
}

// ---------------------------------------------------------------------------- The story
static void enter_chapter(hs_chapter chapter) {
  S.chapter = chapter;
  S.phase = HS_PHASE_AWAKEN;
  S.phase_time = 0;
  S.hurry = 1;
  S.closer_want = 0;
  // Once it has been let loose, the ball stays a real thing in the hall, whichever chapter this is: it goes on rolling with the hand.
  if (chapter >= HS_CH_COILS && chapter != HS_CH_LISTENING) hs_world_set_ball_active(true);
  const hs_chapter_def *d = chapter_def(chapter);
  if (d->enter) d->enter();
}

void hs_go_to(hs_chapter chapter) {
  if (S.phase == HS_PHASE_LEAVING && S.next_chapter == chapter) return;
  S.next_chapter = chapter;
  S.phase = HS_PHASE_LEAVING;
  S.phase_time = 0;
  S.hurry = 1;
  hs_fx_organ_stop(chapter > S.chapter);
  hs_fx_set_triggers((const hs_trigger_effect[]){hs_trigger_off()}, (const hs_trigger_effect[]){hs_trigger_off()});
}

void hs_go_to_quietly(hs_chapter chapter) {
  S.next_chapter = chapter;
  S.phase = HS_PHASE_LEAVING;
  S.phase_time = chapter_def(S.chapter)->leave_seconds;
}

void hs_director_set_chapter(hs_chapter chapter) {
  const hs_chapter_def *old = chapter_def(S.chapter);
  if (old->exit) old->exit();
  enter_chapter(chapter);
}

static void advance(void) {
  const hs_chapter_def *d = chapter_def(S.chapter);
  S.phase_time += S.dt * S.hurry;
  if (S.phase == HS_PHASE_AWAKEN && S.phase_time >= d->awaken_seconds) {
    S.phase = HS_PHASE_PLAY;
    S.phase_time = 0;
    S.hurry = 1;
  } else if (S.phase == HS_PHASE_LEAVING && S.phase_time >= d->leave_seconds) {
    if (d->exit) d->exit();
    enter_chapter(S.next_chapter);
  }
}

static bool g_asking_quit, g_wants_quit, g_paused_before_asking, g_keyboard_only;

// What to do right now, in one plain line, for the first moments of each chapter: nobody should have to guess.
static void show_guidance(void) {
  const float kSays = 16;
  int line = -1;
  switch (S.chapter) {
    case HS_CH_COILS: line = HS_TEXT_COILS; break;
    case HS_CH_GEARS: line = HS_TEXT_GEARS; break;
    case HS_CH_LATTICE: line = HS_TEXT_LATTICE; break;
    case HS_CH_BALANCE: line = HS_TEXT_BALANCE; break;
    case HS_CH_BREATH: line = hs_mic_get_state() == HS_MIC_DENIED ? HS_TEXT_BREATH_NO_MIC : HS_TEXT_BREATH; break;
    case HS_CH_VOICE: line = S.voice_on_mac ? HS_TEXT_VOICE_MAC : HS_TEXT_VOICE; break;
    case HS_CH_WORKSHOP: line = HS_TEXT_WORKSHOP; break;
    case HS_CH_LISTENING: line = hs_listen_get_status().speaker_chosen ? HS_TEXT_LISTENING_SPEAKER : HS_TEXT_LISTENING; break;
    default: break;
  }
  if (S.chapter == HS_CH_ASSEMBLY && S.phase_time > 34) { hs_show_text(HS_TEXT_HOLD_IT, hs_stage_ease((float)(S.phase_time - 34) / 0.8f), 0.07f, 0.895f, 28); return; }
  if (S.inside) { hs_show_text(HS_TEXT_INSIDE, 1, 0.07f, 0.92f, 28); return; }
  if (line < 0 || S.phase != HS_PHASE_PLAY) return;
  // It stays for as long as it is true, quieter once it has been read: what a chapter's keys do is never gone from the screen.
  float alpha = fminf(1, (float)S.phase_time / 0.6f) * (1 - 0.2f * hs_stage_ramp((float)S.phase_time, kSays, kSays + 1));
  hs_show_text(line, alpha, 0.07f, 0.895f, 28);
}

void hs_show_nav(int key) {
  S.scene.nav_key = key;
  S.scene.nav_alpha = S.pause_mix > 0.5f ? 0 : 1.0f;
}

void hs_director_ask_quit(void) {
  if (g_asking_quit) { g_wants_quit = true; return; }      // asked twice is an answer
  g_asking_quit = true;
  g_paused_before_asking = S.paused;
  S.paused = true;
  S.pause_since = S.seconds;
  hs_fx_pill(1);
}
bool hs_director_wants_to_quit(void) { return g_wants_quit; }
void hs_director_set_keyboard_only(bool only) { g_keyboard_only = only; }

static void handle_keys(void) {
  const hs_chapter_def *d = chapter_def(S.chapter);
  for (uint32_t bit = 1; bit && bit <= HS_KEY_LEFT; bit <<= 1) {
    if (!(S.pressed & bit)) continue;
    if (g_asking_quit) {
      if (bit == HS_KEY_SOUTH) g_wants_quit = true;
      else if (bit == HS_KEY_EAST || bit == HS_KEY_OPTIONS) { g_asking_quit = false; S.paused = g_paused_before_asking; hs_fx_pill(1); }
      continue;
    }
    if (bit == HS_KEY_OPTIONS) {
      S.paused = !S.paused;
      S.pause_since = S.seconds;
      hs_fx_pill(1);
      continue;
    }
    if (S.paused) {
      if (bit == HS_KEY_SOUTH || bit == HS_KEY_TOUCH) { S.paused = false; hs_fx_key(1); }
      else if (bit == HS_KEY_NORTH && S.chapter == HS_CH_WORKSHOP) { S.paused = false; hs_fx_key(3); hs_go_to(HS_CH_FINALE); }
      else if (bit == HS_KEY_EAST) { S.leaving_to_home = true; hs_fx_key(2); }
      else plain_reaction(bit);
      continue;
    }
    if (bit == HS_KEY_HOME) {
      hs_motion_recenter();
      hs_fx_home();
      continue;
    }
    bool used = false;
    // Moving on is under way: next hurries it, and back turns it round.
    if (S.phase == HS_PHASE_LEAVING && S.chapter != HS_CH_WORKSHOP && S.chapter != HS_CH_LISTENING && bit == HS_KEY_EAST && S.next_chapter > HS_CH_COILS && S.next_chapter < HS_CH_WORKSHOP) {
      hs_fx_key(2);
      S.next_chapter = (hs_chapter)(S.next_chapter - 1);
      S.hurry = 3;
      used = true;
    }
    // The line at the bottom says what the lower and right keys do, and it stays true through a chapter's opening as well as its play:
    // back goes back at once, next plays the opening faster, never a cut. Free play has neither.
    if (!used && S.chapter != HS_CH_ASSEMBLY && S.chapter != HS_CH_LISTENING && S.chapter != HS_CH_WORKSHOP && S.phase != HS_PHASE_PLAY && (bit == HS_KEY_SOUTH || bit == HS_KEY_EAST)) {
      bool asks_first = S.chapter == HS_CH_BREATH && bit == HS_KEY_SOUTH && hs_mic_get_state() == HS_MIC_NOT_ASKED;
      bool finale_goes = S.chapter == HS_CH_FINALE;
      bool first_chapter = S.chapter == HS_CH_COILS && bit == HS_KEY_EAST;     // nothing lies behind it: the opening is not played twice
      if (S.phase == HS_PHASE_AWAKEN && bit == HS_KEY_EAST && !finale_goes && !first_chapter) {
        hs_fx_key(2);
        hs_go_to((hs_chapter)(S.chapter - 1));
        used = true;
      } else if (!asks_first && !finale_goes && !first_chapter) {
        S.hurry = 3;
        hs_fx_hurry();
        used = true;
      }
    }
    if (!used && d->press) used = d->press(bit);
    if (!used) plain_reaction(bit);
  }
}

static void set_default_light(void) {
  hs_scene *s = &S.scene;
  s->light_count = 4;
  s->lights[0] = (hs_light){simd_make_float3(0.35f, 0.9f, 0.30f), 0.08f, simd_make_float3(2.6f, 2.45f, 2.3f), 0};
  s->lights[1] = (hs_light){simd_make_float3(-0.7f, 0.25f, -0.6f), 0.14f, simd_make_float3(0.9f, 0.55f, 0.42f), 0};
  float scale = S.env;
  for (int i = 0; i < 2; i++) s->lights[i].color *= scale;
  simd_float3 mover = simd_act(S.shown, S.mover);
  s->lights[2] = (hs_light){mover, 0.006f, S.mover_color * S.mover_power, S.mover_range};
  simd_float3 lamp;
  float lit = hs_crew_lamp(&lamp);
  s->lights[3] = (hs_light){simd_act(S.shown, lamp), 0.0015f, simd_make_float3(1.0f, 0.55f, 0.24f) * 0.5f * lit, 0.06f};
}

// Words never snap. One on screen finishes fading before the next begins, however fast the keys are going, and an unchanged line is simply left alone.
typedef struct { int key; float alpha, x, y, size; } shown_caption;
static shown_caption g_text_shown = {-1, 0, 0, 0, 0}, g_nav_shown = {-1, 0, 0, 0, 0};

static void settle_caption(shown_caption *c, int *key, float *alpha, float *x, float *y, float *size) {
  int want = *alpha > 0 ? *key : -1;
  float want_alpha = want >= 0 ? *alpha : 0;
  if (want != c->key && c->alpha <= 0) {
    c->key = want;
    if (want >= 0) { c->x = *x; c->y = *y; c->size = *size; }
  }
  if (want != c->key) want_alpha = 0;
  float step = S.dt / (want_alpha > c->alpha ? 0.20f : 0.12f);
  c->alpha = want_alpha > c->alpha ? fminf(want_alpha, c->alpha + step) : fmaxf(want_alpha, c->alpha - step);
  *key = c->alpha > 0 ? c->key : -1;
  *alpha = c->alpha;
  *x = c->x;
  *y = c->y;
  *size = c->size;
}

static bool g_calm_motion;
void hs_director_set_calm_motion(bool calm) { g_calm_motion = calm; }

void hs_director_start(const hs_mesh_pack *pack, hs_chapter first, bool real_time) {
  g_asking_quit = g_wants_quit = false;
  hs_fx_reset();
  g_text_shown = g_nav_shown = (shown_caption){-1, 0, 0, 0, 0};
  memset(&S, 0, sizeof S);
  S.pack = pack;
  S.real_time = real_time;
  S.hand = S.shown = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  S.frame_scale = 1;
  S.hurry = 1;
  S.camera_speed = 1.6f;
  S.env = 1;
  S.backdrop = 1;
  S.exposure = 1;
  S.hand_weight = 1;
  S.scene = hs_scene_default();
  S.scene.touch_color = simd_make_float3(0.06f, 0.72f, 0.26f);
  S.lattice_parts[0] = hs_mesh_pack_find(pack, "lattice");
  S.lattice_parts[1] = hs_mesh_pack_find(pack, "lattice_nodes");
  S.clear_parts[0] = hs_mesh_pack_find(pack, "body");
  S.clear_parts[1] = hs_mesh_pack_find(pack, "body_inner");
  S.clear_parts[2] = hs_mesh_pack_find(pack, "dome");
  S.hand_share_now = 1;
  for (uint32_t i = 0; i < pack->part_count && i < HS_MAX_PART_POSES; i++) {
    const hs_part *part = &pack->parts[i];
    float x = (part->bounds_min[0] + part->bounds_max[0]) / 2, z = (part->bounds_min[2] + part->bounds_max[2]) / 2 - kHallCenterZ;
    S.outside_hall[i] = part->parent < 0 && hypotf(x, z) > 0.031f;
  }
  S.scene.part_hidden = S.part_hidden;
  S.needle = 0;
  S.mover_range = 0.3f;
  S.mover_color = simd_make_float3(1.0f, 0.7f, 0.5f);
  hs_crew_init();
  hs_world_create();
  hs_hands_init();
  hs_sound_init();
  enter_chapter(first);
  // Start where the chapter's own shot is, so that the first frame is already composed.
  simd_float3 focus = simd_act(S.shown, S.shot.focus);
  S.eye = focus + S.shot.offset;
  S.target = focus;
  S.fov = S.shot.fov;
}

void hs_director_frame(const hs_frame_in *in, hs_scene **out) {
  S.dt = in->dt;
  S.now_ms = in->now_ms;
  S.seconds += in->dt;
  read_input(in);
  hs_scene *s = &S.scene;
  // Reduced motion: the camera moves more slowly and nothing tumbles past it; the object stays with the hand.
  S.camera_speed = fminf(S.camera_speed, g_calm_motion ? 1.0f : 99.0f);
  s->spawn_count = 0;
  s->capsule_count = 0;
  s->text_key = -1;
  s->nav_key = -1;

  // Losing the controller or the focus is a pause with its own face.
  S.disconnected = !in->connected;
  if (in->focus_lost && !S.paused) { S.paused = true; S.paused_by_focus = true; S.pause_since = S.seconds; }
  if (!in->focus_lost && S.paused && S.paused_by_focus) { S.paused = false; S.paused_by_focus = false; }   // back where you left it
  // Nobody there: after five minutes the crew sit down and the camera drifts; after fifteen the display may sleep as usual.
  bool someone = S.pressed || hypotf(S.lx, S.ly) + hypotf(S.rx, S.ry) > 0.1f || S.l2 + S.r2 > 0.05f || S.touching[0] || simd_length(S.hand_turn) > 0.25f;
  S.idle_seconds = someone ? 0 : S.idle_seconds + S.dt;
  if (S.idle_seconds > 300) {
    S.orbit_yaw += sinf((float)S.seconds * 0.05f) * 0.0002f;
    if (!S.crew_dozing) hs_crew_set_awake(0.5f);
    S.crew_dozing = true;
  } else if (S.crew_dozing) {
    hs_crew_set_awake(1);
    S.crew_dozing = false;
  }
  // Said once, quietly, and the filament is turned down a little to ask less of the battery's patience.
  if (S.connected && !S.input.charging && S.input.battery_percent > 0 && S.input.battery_percent <= 10 && !S.battery_said) { S.battery_said = true; S.battery_said_at = S.seconds; }
  S.pause_mix += (((S.paused || S.disconnected) ? 1.0f : 0.0f) - S.pause_mix) * (1 - expf(-S.dt / 0.08f));

  update_hand(in);
  if (!in->focus_lost) handle_keys();      // a press made while the picture is away, in another app, is not meant for here
  bool frozen = S.paused || S.disconnected;
  if (!frozen) {
    advance();
    const hs_chapter_def *d = chapter_def(S.chapter);
    if (d->every_frame) d->every_frame();
  }
  hs_fx_set_paused(frozen);

  update_rig();
  update_camera();
  set_default_light();
  // The lattice is a clear conductor in the dome's underside: nothing to see until it wakes, and then only its light.
  if (s->part_hidden == S.part_hidden) {
    float touched = fmaxf(s->touch[0].z, s->touch[1].z);
    bool show = s->lattice_wake > 0.01f || touched > 0.01f || (S.chapter == HS_CH_LATTICE);
    for (int i = 0; i < 2; i++) if (S.lattice_parts[i] >= 0) S.part_hidden[S.lattice_parts[i]] = !show;
    for (int i = 0; i < 3; i++) if (S.clear_parts[i] >= 0) S.part_hidden[S.clear_parts[i]] = S.hide_clear;
    for (int i = 0; i < HS_MAX_PART_POSES; i++) if (S.outside_hall[i]) S.part_hidden[i] = S.hide_clear;
  }
  s->object = S.object;
  s->seconds = S.seconds;
  s->exposure = S.exposure;
  s->env_strength = S.env;
  s->backdrop_glow = S.backdrop;
  s->dim = S.pause_mix * 0.5f;
  simd_float3 eye_local = simd_act(simd_inverse(S.shown), S.eye);
  hs_crew_pose(s, eye_local, S.dt);
  float ball_position[3], ball_rotation[4], ball_velocity[3];
  hs_world_pose(HS_WORLD_BALL, ball_position, ball_rotation, ball_velocity);
  simd_float4x4 ball = simd_matrix4x4(simd_quaternion(ball_rotation[0], ball_rotation[1], ball_rotation[2], ball_rotation[3]));
  ball.columns[3] = simd_make_float4(ball_position[0], ball_position[1], ball_position[2], 1);
  if (S.chapter != HS_CH_ASSEMBLY) s->crew_bones[HS_CREW][0] = ball;     // in the opening it does not exist yet
  if (S.battery_said && S.seconds - S.battery_said_at < 6 && !S.paused) hs_show_text(HS_TEXT_BATTERY, fminf(1, fminf((float)(S.seconds - S.battery_said_at), (float)(6 - (S.seconds - S.battery_said_at)))), 0.07f, 0.94f, 26);
  if (g_asking_quit) hs_show_text(HS_TEXT_QUIT_ASK, S.pause_mix, 0.07f, 0.88f, 34);
  else if (S.paused) hs_show_text(g_keyboard_only ? HS_TEXT_PAUSED_KEYS : S.chapter == HS_CH_WORKSHOP ? HS_TEXT_PAUSED_FREE : HS_TEXT_PAUSED, S.pause_mix, 0.07f, 0.88f, 34);
  else if (S.scene.text_key < 0) show_guidance();
  if (g_keyboard_only && !S.paused && S.chapter != HS_CH_ASSEMBLY) hs_show_nav(S.chapter == HS_CH_WORKSHOP ? HS_TEXT_KEYBOARD_FREE : HS_TEXT_KEYBOARD);
  else if (!S.paused && S.chapter == HS_CH_ASSEMBLY && S.phase_time < 30) hs_show_nav(HS_TEXT_SKIP);
  else if (!S.paused && !S.inside && S.chapter != HS_CH_ASSEMBLY && S.chapter != HS_CH_LISTENING && !(S.chapter == HS_CH_FINALE && S.phase_time >= 18)) hs_show_nav(S.chapter == HS_CH_WORKSHOP ? HS_TEXT_WORKSHOP_KEYS : S.chapter == HS_CH_BREATH && hs_mic_get_state() == HS_MIC_NOT_ASKED ? HS_TEXT_ALLOW_MIC : S.chapter == HS_CH_COILS ? HS_TEXT_MOVE_ON_FIRST : HS_TEXT_MOVE_ON);
  if (S.disconnected && !S.paused && !g_asking_quit) hs_show_text(in->on_cable ? HS_TEXT_UNPLUG : HS_TEXT_TURN_ON, S.pause_mix, 0.07f, 0.88f, 34);
  settle_caption(&g_text_shown, &s->text_key, &s->text_alpha, &s->text_x, &s->text_y, &s->text_size);
  float nav_x = 0, nav_y = 0, nav_size = 0;
  settle_caption(&g_nav_shown, &s->nav_key, &s->nav_alpha, &nav_x, &nav_y, &nav_size);
  float wants_shade = s->text_key >= 0 && s->text_alpha > 0 ? 1.0f : s->nav_key >= 0 ? 0.85f : 0.0f;
  s->caption_shade += (wants_shade - s->caption_shade) * (1 - expf(-S.dt * 4));
  *out = s;
}

hs_chapter hs_director_chapter(void) { return S.chapter; }
float hs_director_idle_seconds(void) { return S.idle_seconds; }
bool hs_director_paused(void) { return S.paused; }
double hs_director_phase_time(void) { return S.phase_time; }
hs_phase hs_director_phase(void) { return S.phase; }
bool hs_director_wants_to_close(void) { return S.leaving_to_home; }

void hs_director_finish(void) {
  // Leaving is leaving a chapter like any other: whatever it set up, it takes down.
  const hs_chapter_def *d = chapter_def(S.chapter);
  if (d && d->exit) d->exit();
  hs_fx_reset();
  hs_hands_silence();
  hs_sound_set_master(0, 0.3f);
  const hs_trigger_effect off = hs_trigger_off();
  hs_fx_set_triggers(&off, &off);
}
