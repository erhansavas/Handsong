// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The state of the experience, shared by the director and its chapters. Only the frame thread touches it.
#pragma once
#include "link/link.h"
#include "motion/motion.h"
#include "render/mesh_pack.h"
#include "render/scene.h"
#include "world/world.h"

typedef enum {
  HS_CH_ASSEMBLY, HS_CH_COILS, HS_CH_GEARS, HS_CH_LATTICE, HS_CH_BALANCE, HS_CH_BREATH, HS_CH_VOICE, HS_CH_FINALE, HS_CH_WORKSHOP,
  HS_CH_LISTENING, HS_CH_COUNT,
} hs_chapter;

typedef enum { HS_PHASE_AWAKEN, HS_PHASE_PLAY, HS_PHASE_LEAVING } hs_phase;

// Where the camera wants to be: looking at a point of the object from an offset in the room. A framed shot keeps the whole
// object in the picture at any grip; a close shot is allowed to fill the screen with the thing it looks at.
typedef struct { simd_float3 focus, offset; float fov; bool framed; } hs_shot;

typedef struct {
  // This frame's input.
  hs_input input;
  bool connected;
  uint32_t down, pressed, released;
  float lx, ly, rx, ry, l2, r2, l2_speed, r2_speed;
  bool touching[2], touch_began[2], touch_ended[2];
  float touch_x[2], touch_z[2], touch_speed[2];     // on the dome, metres in the object's frame
  float touch_age[2];
  double now_ms, seconds;
  float dt;
  bool real_time;                                  // false in a scripted run

  // Where the story is.
  hs_chapter chapter, next_chapter;
  hs_phase phase;
  double phase_time;
  float hurry;                                     // time runs this much faster while the player asks to move on
  float south_held, east_held;
  int ball_surface;     // glass, gravel, water, wood, lid: the bar takes its colour
  float awake;
  bool paused, paused_by_focus, disconnected, leaving_to_home, hide_clear, inside, listening_now, crew_dozing, voice_on_mac;
  int clear_parts[3];
  uint8_t outside_hall[HS_MAX_PART_POSES];   // what is left out of the picture while standing in the hall
  float hand_share_now;          // how much of the hand's turn reaches the picture; close shots would be thrown about by all of it
  float idle_seconds;
  bool battery_said;
  double battery_said_at;
  float pause_mix, disconnect_mix;
  double pause_since;

  // The hand.
  simd_quatf hand, shown;
  float hand_weight, hold_pitch, hold_known;
  simd_float3 hand_turn;
  simd_float4x4 object;

  // The camera.
  hs_shot shot;
  float camera_speed;                              // radians per second of the spring
  simd_float3 eye, eye_v, target, target_v;
  float fov, fov_v, frame_scale, frame_scale_v;
  float orbit_yaw, orbit_pitch, closer, closer_v;
  float closer_want;

  // Rig state.
  float keys[4], keys_v[4], cross[2], cross_v[2], shoulders[2], pills[2], home, mute, dome_press, dome_press_v;
  float stick_press[2];
  float gear_turn[2], gear_spin[2];
  float bellows[2], needle, cone, lid, coil[2];
  float filament;
  bool stick_at_limit[2];

  const hs_mesh_pack *pack;
  uint8_t part_hidden[HS_MAX_PART_POSES];
  int lattice_parts[2];
  hs_scene scene;
  float mover_power, mover_range;
  simd_float3 mover;
  simd_float3 mover_color;
  float env, backdrop, exposure;
} hs_stage;

extern hs_stage S;

typedef struct {
  float awaken_seconds, leave_seconds;
  void (*enter)(void);
  void (*every_frame)(void);        // any phase
  void (*exit)(void);
  bool (*press)(uint32_t key);      // true when the chapter used the key; otherwise it gets the part's plain reaction
} hs_chapter_def;

extern const hs_chapter_def hs_montage_def, hs_coils_def, hs_gears_def, hs_lattice_def, hs_balance_def, hs_breath_def, hs_voice_def, hs_finale_def, hs_workshop_def, hs_listening_def;

// Helpers the chapters share.
void hs_set_shot(simd_float3 focus, simd_float3 offset, float fov, bool framed);
void hs_go_to(hs_chapter chapter);
void hs_go_to_quietly(hs_chapter chapter);      // the story has already led here, so there is no stop to pull
void hs_show_text(int key, float alpha, float x, float y, float size);
void hs_show_nav(int key);     // the small line of what the buttons do, under whatever else is said
void hs_spawn(int kind, simd_float3 position, simd_float3 velocity, float spread, float speed_spread, simd_float3 color, float size, float life, int count);
simd_float3 hs_socket(const char *name);          // a named place on the model, in the object's frame
float hs_stage_ease(float t);                     // 0 to 1, slow at both ends
float hs_stage_ramp(float x, float from, float to);

// Words the experience may show. The renderer's text cache holds one line per key.
enum { HS_TEXT_PAUSED, HS_TEXT_TURN_ON, HS_TEXT_BREATH_ASK, HS_TEXT_BREATH_STOPPED, HS_TEXT_YOURS, HS_TEXT_SOFTER, HS_TEXT_BATTERY, HS_TEXT_QUIT_ASK, HS_TEXT_KEYBOARD,
       HS_TEXT_MOVE_ON, HS_TEXT_SKIP, HS_TEXT_HOLD_IT, HS_TEXT_COILS, HS_TEXT_GEARS, HS_TEXT_LATTICE, HS_TEXT_BALANCE, HS_TEXT_BREATH, HS_TEXT_BREATH_NO_MIC, HS_TEXT_VOICE, HS_TEXT_WORKSHOP, HS_TEXT_LISTENING, HS_TEXT_INSIDE, HS_TEXT_WORKSHOP_KEYS, HS_TEXT_PAUSED_FREE, HS_TEXT_UNPLUG, HS_TEXT_MOVE_ON_FIRST, HS_TEXT_PAUSED_KEYS, HS_TEXT_KEYBOARD_FREE, HS_TEXT_LISTENING_SPEAKER, HS_TEXT_VOICE_MAC, HS_TEXT_ALLOW_MIC, HS_TEXT_COUNT };
const char *hs_text_line(int key);

// Designed events: picture aside, what the ear and the hands get from each thing that happens.
void hs_fx_on_input(uint32_t pressed);           // on the input thread: the press itself, heard at once
void hs_fx_input_thread_clicks(bool on);
void hs_fx_key(int which);                       // west, south, east, north
void hs_fx_cross(float x, float y);
void hs_fx_shoulder(int side);
void hs_fx_stick_press(int side);
void hs_fx_stick_limit(int side);
void hs_fx_dome_click(float x);
void hs_fx_home(void);
void hs_fx_pill(int side);
void hs_fx_mute(void);
void hs_fx_faint(void);                          // before the parts exist: only a shimmer
void hs_fx_organ_stop(bool forward);
void hs_fx_hurry(void);
void hs_fx_set_speaker_mode(bool on);            // clicks go to the controller's speaker
// What the chapters teach one at a time, and free play has all at once.
void hs_ball_play_frame(void);
void hs_pedals_play_begin(void);
void hs_pedals_play_frame(void);
void hs_pedals_play_end(void);
void hs_finger_play_frame(void);
void hs_finger_play_end(void);
void hs_fx_reset(void);    // a new session owes nothing to how the last one ended
void hs_fx_set_triggers(const hs_trigger_effect *left, const hs_trigger_effect *right);    // kept through a pause
void hs_fx_set_muted(bool quiet);                // the listening room: keys make no sound of their own
void hs_speaker_pluck(float hertz, float gain);  // a tine on the controller's own speaker

// The score's stems by name.
enum {
  HS_STEM_OPEN_RISE, HS_STEM_OPEN_OPEN, HS_STEM_COILS_BED, HS_STEM_COILS_TOP, HS_STEM_GEARS_BED, HS_STEM_GEARS_MOTION, HS_STEM_LATTICE_BED, HS_STEM_LATTICE_TOP,
  HS_STEM_BALANCE_BED, HS_STEM_BALANCE_MOTION, HS_STEM_BREATH_BED, HS_STEM_FINALE, HS_STEM_WORKSHOP_BED, HS_STEM_STOP_UP, HS_STEM_STOP_DOWN, HS_STEM_BRIDGE,
  HS_STEM_VOICE, HS_STEM_COUNT,
};
