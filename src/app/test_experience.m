// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// handsong --test experience: the whole tour with no window, no controller and no sound output, deterministic to the sample.
//   --script tour|rest|fuzz:SEED   --seconds N   --chapter K
//   --sound path.f32    every sample the Mac would play, 32-bit float stereo at 48 kHz
//   --hands path.s8     every block the hands would get, signed 8-bit stereo at 3 kHz
//   --speaker path.f32  what would go to the controller's speaker, stereo at 45 kHz
//   --marks path.txt    when each chapter and phase began
//   --shot prefix --shot-at t1,t2,... --size WxH   frames as PNG, at those times
//   --gpu-report path   the true cost of frames along the way
#import "app/app_delegate.h"
#import "app/test_modes.h"
#include "experience/lights.h"
#import "app/test_video.h"
#import "experience/director.h"
#import "experience/voice_out.h"
#include "experience/wall_predict.h"
#import "hands/hands.h"
#import "motion/motion.h"
#import "render/renderer.h"
#import "sound/sound.h"
#import "sound/output.h"
#import "sound/stems.h"
#import "world/crew.h"
#import "experience/experience.h"
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <mach/mach.h>

static const float kFrame = 1.0f / 60;

typedef struct { uint32_t state; } Random;
static float next01(Random *r) {
  r->state = r->state * 1664525u + 1013904223u;
  return (float)(r->state >> 8) / 16777216.0f;
}

// A held controller: slow believable tilts, and now and then a real shake.
typedef struct { simd_quatf q; float pitch, roll, yaw; bool shake; } Hand;
static Hand g_hand;

static void feed_hand(double t, float amount, bool shake) {
  float pitch = sinf((float)t * 0.61f) * 0.22f * amount, roll = sinf((float)t * 0.43f + 1) * 0.28f * amount, yaw = sinf((float)t * 0.27f) * 0.15f * amount;
  if (shake) { pitch += sinf((float)t * 19) * 0.3f; roll += sinf((float)t * 23) * 0.3f; }
  simd_quatf q = simd_mul(simd_quaternion(yaw, simd_make_float3(0, 1, 0)), simd_mul(simd_quaternion(pitch, simd_make_float3(1, 0, 0)), simd_quaternion(roll, simd_make_float3(0, 0, 1))));
  const float dt = 1.0f / 133;
  simd_quatf delta = simd_mul(simd_inverse(g_hand.q), q);
  float angle = 2 * atan2f(simd_length(simd_imag(delta)), simd_real(delta));
  simd_float3 axis = simd_length(simd_imag(delta)) > 1e-6f ? simd_normalize(simd_imag(delta)) : simd_make_float3(0, 1, 0);
  simd_float3 rate = axis * (angle / dt) * (float)(180 / M_PI);
  simd_float3 felt = simd_act(simd_inverse(q), simd_make_float3(0, 1, 0));
  const float rotation[3] = {rate.x, rate.y, rate.z}, acceleration[3] = {felt.x, felt.y, felt.z};
  hs_motion_feed(rotation, acceleration, dt, t * 1000);
  g_hand.q = q;
}

static void set_key(hs_input *in, uint32_t key, bool down) {
  if (down) in->keys |= key;
  else in->keys &= ~key;
}

// ---------------------------------------------------------------------------- The tour: a player who tries each chapter's own thing, then moves on
static void tour_input(hs_input *in, double t, bool *moved) {
  hs_chapter c = hs_director_chapter();
  hs_phase p = hs_director_phase();
  static hs_chapter seen = HS_CH_COUNT;
  static double since;
  static hs_phase seen_phase = HS_PHASE_AWAKEN;
  if (c != seen || p != seen_phase) { seen = c; seen_phase = p; since = t; }
  double in_phase = t - since;
  in->left_x = in->left_y = in->right_x = in->right_y = 128;
  in->left_trigger = in->right_trigger = 0;
  in->touch[0].down = in->touch[1].down = false;
  in->keys = 0;
  *moved = true;
  if (p != HS_PHASE_PLAY) return;
  static const double kPlaySeconds[HS_CH_COUNT] = {0, 30, 26, 20, 26, 22, 34, 0, 26, 0};
  bool next = (int)c >= 0 && c < HS_CH_COUNT && in_phase > kPlaySeconds[c] && in_phase < kPlaySeconds[c] + 0.1f;
  set_key(in, HS_KEY_SOUTH, next);
  switch (c) {
    case HS_CH_COILS: {
      // Keep the ball moving round all four surfaces.
      float a = (float)in_phase * 0.5f;
      if (fmod(in_phase, 7.0) < 0.05) set_key(in, HS_KEY_NORTH, true);
      in->touch[0].down = true;
      in->touch[0].x = (uint16_t)(960 + 800 * cosf(a * 1.4f));
      in->touch[0].y = (uint16_t)(540 + 480 * sinf(a * 1.4f));
      in->left_trigger = in_phase > 24 && in_phase < 26 ? 180 : 0;
      break;
    }
    case HS_CH_GEARS: {
      // Pre-travel, a long pull to the wall, a hold, a slow release, a sudden one, flutter, and both at once.
      double u = fmod(in_phase, 12.0);
      float v = u < 2 ? (float)u / 2 : u < 3 ? 1 : u < 5 ? 1 - (float)(u - 3) / 2 : u < 6 ? (float)(u - 5) * 1.0f : u < 6.1 ? 1 : u < 7 ? 0 : u < 9 ? 0.5f + 0.3f * sinf((float)u * 40) : u < 10.5 ? fminf(1, (float)(u - 9) * 2) : 0;
      in->right_trigger = (uint8_t)(v * 255);
      in->left_trigger = u > 9 ? (uint8_t)(v * 255) : (uint8_t)(u < 2 ? 0 : v * 200);
      break;
    }
    case HS_CH_LATTICE: {
      float a = (float)in_phase;
      in->touch[0].down = fmod(in_phase, 5) < 4.2;
      in->touch[0].x = (uint16_t)(960 + 700 * sinf(a * 1.3f));
      in->touch[0].y = (uint16_t)(540 + 400 * cosf(a * 0.9f));
      in->touch[1].down = fmod(in_phase, 8) > 5 && fmod(in_phase, 8) < 7;
      in->touch[1].x = (uint16_t)(960 - 600 * sinf(a * 0.7f));
      in->touch[1].y = (uint16_t)(540 - 300 * cosf(a * 0.5f));
      set_key(in, HS_KEY_TOUCH, fmod(in_phase, 5) < 0.05);
      break;
    }
    case HS_CH_BALANCE:
      if (fmod(in_phase, 6) < 0.05) set_key(in, HS_KEY_WEST, true);
      if (fmod(in_phase + 3, 11) < 0.05) set_key(in, HS_KEY_NORTH, true);
      // Going inside, for a look round and back out.
      if ((in_phase > 8 && in_phase < 8.05) || (in_phase > 16 && in_phase < 16.05)) set_key(in, HS_KEY_TOUCH, true);
      if (in_phase > 10 && in_phase < 16) { in->left_y = 90; in->right_x = 150; }
      break;
    case HS_CH_BREATH:
      hs_mic_simulate(HS_MIC_GRANTED, 0.5f + 0.5f * sinf((float)in_phase * 0.9f), 120 + 250 * (0.5f + 0.5f * sinf((float)in_phase * 0.3f)));
      break;
    case HS_CH_VOICE:
      if (fmod(in_phase, 3) < 0.05) set_key(in, HS_KEY_WEST, true);
      break;
    case HS_CH_WORKSHOP:
      // Someone playing: a note every so often, and a pedal pulled to the stop and let go now and then, never held for ever.
      if (fmod(in_phase, 0.75) < 0.03) set_key(in, (int)(in_phase * 4) % 3 == 0 ? HS_KEY_SOUTH : HS_KEY_WEST, true);
      in->right_trigger = fmod(in_phase, 8) < 2 ? (uint8_t)(fmod(in_phase, 8) / 2 * 255) : 0;
      // And once in a while everything at once, the heaviest this place can be: both pedals at the stop, steam, two fingers striking an arc.
      if (fmod(in_phase, 16) > 12) {
        in->left_trigger = in->right_trigger = 255;
        in->touch[0].down = in->touch[1].down = true;
        in->touch[0].x = (uint16_t)(960 + 800 * sinf((float)in_phase * 1.3f));
        in->touch[0].y = 300;
        in->touch[1].x = (uint16_t)(960 - 800 * sinf((float)in_phase * 0.9f));
        in->touch[1].y = 800;
      }
      break;
    default: break;
  }
}

// ---------------------------------------------------------------------------- The fuzzer: everything, at any time
static void fuzz_input(hs_input *in, Random *r, double t) {
  (void)t;
  in->keys = 0;
  if (next01(r) < 0.4f) for (int i = 0; i < 3; i++) if (next01(r) < 0.5f) in->keys |= 1u << (int)(next01(r) * 19);
  in->left_x = (uint8_t)(next01(r) * 255);
  in->left_y = (uint8_t)(next01(r) * 255);
  in->right_x = (uint8_t)(next01(r) * 255);
  in->right_y = (uint8_t)(next01(r) * 255);
  in->left_trigger = next01(r) < 0.5f ? (uint8_t)(next01(r) * 255) : 0;
  in->right_trigger = next01(r) < 0.5f ? (uint8_t)(next01(r) * 255) : 0;
  for (int i = 0; i < 2; i++) {
    in->touch[i].down = next01(r) < 0.3f;
    in->touch[i].x = (uint16_t)(next01(r) * 1919);
    in->touch[i].y = (uint16_t)(next01(r) * 1079);
  }
}

static double resident_mb(void) {
  struct mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
  return (double)info.resident_size / 1e6;
}

static bool all_finite(const hs_scene *s) {
  for (int i = 0; i < 4; i++) if (!isfinite(s->object.columns[i].x) || !isfinite(s->object.columns[i].y) || !isfinite(s->object.columns[i].z)) return false;
  if (!isfinite(s->eye.x) || !isfinite(s->eye.y) || !isfinite(s->eye.z) || !isfinite(s->target.x) || !isfinite(s->fov_degrees)) return false;
  for (int who = 0; who <= HS_CREW; who++) for (int b = 0; b < HS_CREW_BONES; b++) if (!isfinite(s->crew_bones[who][b].columns[3].x)) return false;
  return true;
}

// ---------------------------------------------------------------------------- Whether the realtime paths ever ask the allocator for anything
typedef void (malloc_logger_t)(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t result, uint32_t skip);
extern malloc_logger_t *malloc_logger;
static volatile bool g_in_realtime;     // one thread here; a thread-local would itself allocate on first use
static _Atomic long g_realtime_allocations;
static void count_allocations(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t result, uint32_t skip) {
  (void)type; (void)arg1; (void)arg2; (void)arg3; (void)result; (void)skip;
  if (g_in_realtime) atomic_fetch_add(&g_realtime_allocations, 1);
}

int hs_test_realtime_audit(void) {
  NSString *resources = NSBundle.mainBundle.resourcePath;
  static hs_mesh_pack pack;
  if (!hs_mesh_pack_load([resources stringByAppendingPathComponent:@"instrument.hsmesh"].fileSystemRepresentation, &pack)) return 1;
  if (!hs_stems_load([resources stringByAppendingPathComponent:@"score"].fileSystemRepresentation)) return 1;
  hs_link_simulate();
  hs_motion_reset();
  g_hand.q = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  hs_mic_simulate(HS_MIC_DENIED, 0, 0);
  hs_director_start(&pack, HS_CH_COILS, false);
  hs_sound_set_stems(hs_stems_table(), hs_stems_count());
  long voice_frames = 0;
  const int16_t *voice = hs_stems_voice_45k(&voice_frames);
  hs_voice_load(voice, voice_frames);
  hs_input in = {0};
  in.left_x = in.left_y = in.right_x = in.right_y = 128;
  int8_t waveform[HS_WAVE_BYTES];
  static float audio[HS_SOUND_BLOCK * 2], speaker[HS_SPEAKER_PACKET_FRAMES * 2 * 2];
  double t = 0, next_block = 0, fed = 0, spent_block = 0, worst_block = 0, spent_sound = 0, worst_sound = 0;
  long audio_frames = 0, blocks = 0, sound_blocks = 0;
  Random random = {7};
  malloc_logger = count_allocations;
  for (long frame = 0; frame < 60 * 240; frame++) {
    if (frame == 60 * 20) atomic_store(&g_realtime_allocations, 0);     // the physics may settle its first contacts; only what follows counts
    fuzz_input(&in, &random, t);
    while (fed < t) { feed_hand(fed, 1, fmod(t, 7) < 1); fed += 1.0 / 133; }
    hs_frame_in frame_in = {.input = in, .connected = true, .now_ms = t * 1000, .target_ms = t * 1000 + 16, .dt = kFrame};
    hs_scene *scene;
    hs_director_frame(&frame_in, &scene);
    while (next_block <= t) {
      double start = CACurrentMediaTime();
      g_in_realtime = true;
      hs_live_block(next_block * 1000, waveform, NULL, NULL, NULL);
      if (hs_voice_active()) hs_voice_block(speaker);
      g_in_realtime = false;
      double used = CACurrentMediaTime() - start;
      spent_block += used;
      worst_block = fmax(worst_block, used);
      blocks++;
      next_block += HS_STREAM_PERIOD_MS / 1000;
    }
    long want = (long)((t + kFrame) * HS_SOUND_RATE);
    while (audio_frames < want) {
      double start = CACurrentMediaTime();
      g_in_realtime = true;
      hs_sound_render(audio, HS_SOUND_BLOCK);
      g_in_realtime = false;
      double used = CACurrentMediaTime() - start;
      spent_sound += used;
      worst_sound = fmax(worst_sound, used);
      sound_blocks++;
      audio_frames += HS_SOUND_BLOCK;
    }
    t += kFrame;
  }
  malloc_logger = NULL;
  long allocations = atomic_load(&g_realtime_allocations);
  printf("realtime paths under the fuzzer: %ld allocations after the first 20 s (must be 0)\n", allocations);
  printf("physics, crew and hands per 21.33 ms block: mean %.2f, worst %.2f ms (mean at most 1.1, worst at most 3.5)\n", spent_block / (double)blocks * 1000, worst_block * 1000);
  printf("sound per 256-frame block: mean %.3f, worst %.3f ms (worst at most 2)\n", spent_sound / (double)sound_blocks * 1000, worst_sound * 1000);
  return allocations == 0 && spent_block / (double)blocks * 1000 <= 1.1 && worst_block * 1000 <= 3.5 && worst_sound * 1000 <= 2 ? 0 : 1;
}

// ---------------------------------------------------------------------------- How well a pull's wall is seen coming
int hs_test_trigger_prediction(void) {
  // Pulls at many speeds, each a curve as a finger makes it: a fast start easing a little into the wall. The real test is whether the
  // blow, sent 110 ms ahead, lands when the pedal stops, and whether a pull that stops short gets no blow at all.
  const float wall = 0.985f;
  double error_sum = 0;
  long predictions = 0, wrong = 0, shorts = 0;
  for (int i = 0; i < 400; i++) {
    bool reaches = i % 4 != 0;
    float seconds = 0.10f + 0.9f * (float)(i % 50) / 50;
    float stop_at = reaches ? 1.0f : 0.45f + 0.45f * (float)(i % 7) / 7;
    float value = 0, speed = 0, top_speed = 1.0f / seconds;
    hs_wall_predictor predictor = {0};
    double predicted_at = -1, arrived_at = -1;
    const float report = 1.0f / 133;     // as the input thread sees it, once per report on a wide link
    for (double t = 0; t < seconds + 0.5; t += report) {
      // A finger's pull: picks up speed in 30 ms and holds it. Against the wall the pedal stops dead; a finger stopping by itself
      // short of the wall slows down over about 80 ms, as muscle does.
      float want = top_speed;
      float left = stop_at - value;
      if (!reaches && left < top_speed * 0.08f * 0.5f + 0.001f) want = 0;
      speed += (want - speed) * (want > speed ? fminf(1, report / 0.03f) : fminf(1, report / 0.04f));
      value = fminf(reaches ? 1.0f : stop_at, value + speed * report);
      if (reaches && value >= 1.0f) speed = 0;
      if (hs_wall_will_hit(&predictor, value, report, wall, 0.105f) && predicted_at < 0) predicted_at = t;
      if (arrived_at < 0 && value >= wall) arrived_at = t;
    }
    if (!reaches) {
      shorts++;
      if (predicted_at >= 0) wrong++;
      continue;
    }
    if (predicted_at >= 0 && arrived_at >= 0) {
      error_sum += fabs((predicted_at + 0.110) - arrived_at);
      predictions++;
    }
  }
  double mean_ms = predictions ? error_sum / (double)predictions * 1000 : 999;
  double false_share = (double)wrong / (double)shorts;
  printf("wall prediction: mean error %.1f ms over %ld pulls (at most 20), false blows %.1f%% of %ld short pulls (at most 2%%)\n", mean_ms, predictions, false_share * 100, shorts);
  return mean_ms <= 20 && false_share <= 0.02 ? 0 : 1;
}

// How long from a report to the first sample of a direct click, and of a click meant for the hand.
int hs_test_press_latency(void) {
  hs_sound_init();
  double room = hs_sound_output_latency_ms();
  hs_sound_set_room_latency((int)(room * 48));
  static float audio[HS_SOUND_BLOCK * 2];
  double direct_worst = 0, aligned_worst = 0, direct_sum = 0;
  for (int k = 0; k < 40; k++) hs_sound_render(audio, HS_SOUND_BLOCK);
  for (int i = 0; i < 200; i++) {
    int offset = i % HS_SOUND_BLOCK;       // a report can land anywhere in a block
    (void)offset;
    for (int k = 0; k < 40; k++) hs_sound_render(audio, HS_SOUND_BLOCK);
    hs_sound_strike_now(HS_MAT_SOUND_CERAMIC, 1000, 0.3f, 0);
    long waited = 0;
    bool found = false;
    while (!found && waited < 48000) {
      hs_sound_render(audio, HS_SOUND_BLOCK);
      for (int f = 0; f < HS_SOUND_BLOCK && !found; f++) {
        if (fabsf(audio[f * 2]) > 1e-4f) { found = true; waited += f; }
      }
      if (!found) waited += HS_SOUND_BLOCK;
    }
    double ms = (double)(waited + offset) / 48 + room;    // the part inside the app, the wait for a block, and the output's own delay
    direct_worst = fmax(direct_worst, ms);
    direct_sum += ms;
    for (int k = 0; k < 400; k++) hs_sound_render(audio, HS_SOUND_BLOCK);
    hs_sound_strike(HS_MAT_SOUND_CERAMIC, 1000, 0.3f, 0, HS_TIMING_ALIGNED);
    waited = 0;
    found = false;
    while (!found && waited < 48000) {
      hs_sound_render(audio, HS_SOUND_BLOCK);
      for (int f = 0; f < HS_SOUND_BLOCK && !found; f++) {
        if (fabsf(audio[f * 2]) > 1e-4f) { found = true; waited += f; }
      }
      if (!found) waited += HS_SOUND_BLOCK;
    }
    aligned_worst = fmax(aligned_worst, (double)waited / 48 + room);
    for (int k = 0; k < 400; k++) hs_sound_render(audio, HS_SOUND_BLOCK);
  }
  printf("the output adds %.1f ms\n", room);
  printf("press to sound through the app and output: direct mean %.1f worst %.1f ms (mean at most 26, worst at most 32); with the hand %.1f ms (110 wanted, within 15)\n", direct_sum / 200, direct_worst, aligned_worst);
  return direct_sum / 200 <= 26 && direct_worst <= 32 && fabs(aligned_worst - 110) <= 15 ? 0 : 1;
}

// ---------------------------------------------------------------------------- Each scenario a script can reach, with what must be true after it
static hs_mesh_pack g_scenario_pack;

static void run_frames(double seconds, hs_input *input, bool connected, bool focus_lost, double *clock) {
  static int8_t waveform[HS_WAVE_BYTES];
  static float audio[HS_SOUND_BLOCK * 2];
  static double next_block;
  long frames = (long)(seconds * 60);
  for (long i = 0; i < frames; i++) {
    while (*clock > next_block) {
      hs_live_block(next_block * 1000, waveform, NULL, NULL, NULL);
      hs_sound_render(audio, HS_SOUND_BLOCK);
      hs_sound_render(audio, HS_SOUND_BLOCK);
      hs_sound_render(audio, HS_SOUND_BLOCK);
      hs_sound_render(audio, HS_SOUND_BLOCK);
      next_block += HS_STREAM_PERIOD_MS / 1000;
    }
    hs_frame_in in = {.input = *input, .connected = connected, .now_ms = *clock * 1000, .target_ms = *clock * 1000 + 16, .dt = kFrame, .focus_lost = focus_lost};
    hs_scene *scene;
    hs_director_frame(&in, &scene);
    *clock += kFrame;
  }
}

int hs_test_scenarios(void) {
  NSString *resources = NSBundle.mainBundle.resourcePath;
  if (!hs_mesh_pack_load([resources stringByAppendingPathComponent:@"instrument.hsmesh"].fileSystemRepresentation, &g_scenario_pack)) return 1;
  if (!hs_stems_load([resources stringByAppendingPathComponent:@"score"].fileSystemRepresentation)) return 1;
  hs_link_simulate();
  hs_motion_reset();
  hs_director_start(&g_scenario_pack, HS_CH_COILS, false);
  hs_sound_set_stems(hs_stems_table(), hs_stems_count());
  hs_mic_simulate(HS_MIC_DENIED, 0, 0);
  hs_input in = {0};
  in.left_x = in.left_y = in.right_x = in.right_y = 128;
  double clock = 0;
  int failures = 0;
#define CHECK(name, ok) do { bool passed = (ok); printf("%s %s\n", passed ? "pass" : "FAIL", name); if (!passed) failures++; } while (0)
  // No controller at launch: nothing moves on, and it says what to do.
  run_frames(2, &in, false, false, &clock);
  double held = hs_director_phase_time();
  run_frames(3, &in, false, false, &clock);
  CHECK("no controller: the story waits", hs_director_phase_time() == held);
  run_frames(1, &in, true, false, &clock);
  CHECK("it connects: the story goes on", hs_director_phase_time() > held);
  // Dropping in the middle of a chapter, holding a trigger.
  run_frames(9, &in, true, false, &clock);
  hs_chapter before = hs_director_chapter();
  in.left_trigger = 255;
  run_frames(0.5, &in, true, false, &clock);
  run_frames(2, &in, false, false, &clock);
  CHECK("dropped: no trigger force is left on", hs_link_triggers_are_off());
  in.left_trigger = 0;
  run_frames(1, &in, true, false, &clock);
  CHECK("back: the same chapter", hs_director_chapter() == before);
  // Focus goes and comes back.
  run_frames(1, &in, true, true, &clock);
  CHECK("focus lost: paused", hs_director_paused());
  run_frames(1, &in, true, false, &clock);
  CHECK("focus back: goes on by itself", !hs_director_paused());
  // Options pauses, the lower key resumes.
  in.keys = HS_KEY_OPTIONS;
  run_frames(0.1, &in, true, false, &clock);
  in.keys = 0;
  run_frames(0.5, &in, true, false, &clock);
  CHECK("options: paused", hs_director_paused());
  in.keys = HS_KEY_SOUTH;
  run_frames(0.1, &in, true, false, &clock);
  in.keys = 0;
  run_frames(0.5, &in, true, false, &clock);
  CHECK("lower key: resumes", !hs_director_paused());
  // The triggers let go in a pause, and take up again after it: losing them for good feels like a broken controller.
  hs_director_set_chapter(HS_CH_GEARS);
  run_frames(1, &in, true, false, &clock);
  CHECK("gears: the triggers are hard", !hs_link_triggers_are_off());
  in.keys = HS_KEY_OPTIONS;
  run_frames(0.1, &in, true, false, &clock);
  in.keys = 0;
  run_frames(0.5, &in, true, false, &clock);
  CHECK("paused: the triggers let go", hs_link_triggers_are_off());
  in.keys = HS_KEY_SOUTH;
  run_frames(0.1, &in, true, false, &clock);
  in.keys = 0;
  run_frames(0.5, &in, true, false, &clock);
  CHECK("resumed: the triggers are hard again", !hs_link_triggers_are_off());
  run_frames(1, &in, true, true, &clock);
  run_frames(1, &in, true, false, &clock);
  CHECK("focus back: the triggers are hard again", !hs_link_triggers_are_off());
  run_frames(2, &in, false, false, &clock);
  run_frames(1, &in, true, false, &clock);
  CHECK("reconnected: the triggers are hard again", !hs_link_triggers_are_off());
  // A bass beat in one hand is seen in the magnet on that side: it jumps, and comes back to rest.
  hs_director_set_chapter(HS_CH_COILS);
  float coil_peak[2] = {0, 0};
  for (int i = 0; i < 60 && hs_director_phase_time() < 2.4; i++) {
    run_frames(1.0 / 30, &in, true, false, &clock);
    for (int side = 0; side < 2; side++) coil_peak[side] = fmaxf(coil_peak[side], fabsf(S.scene.coil[side]));
  }
  printf("coil peak on the beats: left %.2f right %.2f\n", coil_peak[0], coil_peak[1]);
  CHECK("bass beat: the magnet on that side jumps", coil_peak[0] > 0.6f);
  CHECK("bass beat: the other magnet stays put", coil_peak[1] < coil_peak[0] * 0.5f);
  run_frames(1.4, &in, true, false, &clock);
  // What the bottom line says stays true in every phase: back goes back during a chapter's opening and in the finale too.
  hs_director_set_chapter(HS_CH_LATTICE);
  run_frames(1, &in, true, false, &clock);
  CHECK("opening: still scripted", hs_director_phase() == HS_PHASE_AWAKEN);
  in.keys = HS_KEY_EAST;
  run_frames(0.05, &in, true, false, &clock);
  in.keys = 0;
  run_frames(5, &in, true, false, &clock);
  CHECK("opening: back goes back", hs_director_chapter() == HS_CH_GEARS);
  hs_director_set_chapter(HS_CH_FINALE);
  run_frames(3, &in, true, false, &clock);
  in.keys = HS_KEY_EAST;
  run_frames(0.05, &in, true, false, &clock);
  in.keys = 0;
  run_frames(1, &in, true, false, &clock);
  CHECK("finale: back goes back", hs_director_chapter() == HS_CH_VOICE);
  // The share key restarts nothing: no key moves you unless the screen says it does.
  hs_director_set_chapter(HS_CH_COILS);
  run_frames(10, &in, true, false, &clock);
  double before_create = hs_director_phase_time();
  in.keys = HS_KEY_CREATE;
  run_frames(0.05, &in, true, false, &clock);
  in.keys = 0;
  run_frames(0.5, &in, true, false, &clock);
  CHECK("share: stays where it is", hs_director_chapter() == HS_CH_COILS && hs_director_phase() == HS_PHASE_PLAY && hs_director_phase_time() > before_create);
  // Free play: every key, one tap at a time, and it stays; a short tap of the way out does nothing; only the hold leaves.
  hs_director_set_chapter(HS_CH_WORKSHOP);
  run_frames(2, &in, true, false, &clock);
  CHECK("free play: the triggers are hard", !hs_link_triggers_are_off());
  bool stayed = true;
  for (uint32_t bit = 1; bit && bit <= HS_KEY_LEFT; bit <<= 1) {
    if (bit == HS_KEY_OPTIONS) continue;
    in.keys = bit;
    run_frames(0.1, &in, true, false, &clock);
    in.keys = 0;
    run_frames(0.1, &in, true, false, &clock);
    stayed = stayed && hs_director_chapter() == HS_CH_WORKSHOP && !hs_director_wants_to_close() && !hs_director_paused();
  }
  CHECK("free play: no key sends you anywhere", stayed);
  in.keys = HS_KEY_EAST;
  run_frames(1.0, &in, true, false, &clock);
  in.keys = 0;
  run_frames(0.2, &in, true, false, &clock);
  CHECK("free play: letting go in time stays", !hs_director_wants_to_close());
  in.keys = HS_KEY_EAST;
  run_frames(1.4, &in, true, false, &clock);
  in.keys = 0;
  CHECK("free play: holding leaves", hs_director_wants_to_close());
  S.leaving_to_home = false;
  failures += hs_test_mic_gate();
  // The bar and the model's filament go green with the lattice, the same green as the wire, and leave it behind afterwards.
  hs_director_set_chapter(HS_CH_LATTICE);
  for (int i = 0; i < 180; i++) { run_frames(1.0 / 60, &in, true, false, &clock); hs_lights_frame(&S.scene); }
  simd_float3 lit_tint = S.scene.filament_tint;
  CHECK("lattice: the bar is green", lit_tint.y > lit_tint.x * 2 && lit_tint.y > lit_tint.z);
  hs_director_set_chapter(HS_CH_GEARS);
  in.left_trigger = 255;
  for (int i = 0; i < 180; i++) { run_frames(1.0 / 60, &in, true, false, &clock); hs_lights_frame(&S.scene); }
  in.left_trigger = 0;
  CHECK("gears: the bar is hot metal", S.scene.filament_tint.x > S.scene.filament_tint.z * 1.5);
  hs_lights_stop();
  // Coils is the first chapter: back has nowhere to go, and its bottom line does not offer it.
  hs_director_set_chapter(HS_CH_COILS);
  run_frames(10, &in, true, false, &clock);
  in.keys = HS_KEY_EAST;
  run_frames(0.05, &in, true, false, &clock);
  in.keys = 0;
  run_frames(1, &in, true, false, &clock);
  CHECK("coils: back stays", hs_director_chapter() == HS_CH_COILS && hs_director_phase() == HS_PHASE_PLAY);
  // A press made while another app is in front does nothing here.
  run_frames(0.5, &in, true, true, &clock);
  in.keys = HS_KEY_EAST;
  run_frames(0.1, &in, true, true, &clock);
  in.keys = 0;
  run_frames(0.1, &in, true, true, &clock);
  run_frames(0.5, &in, true, false, &clock);
  CHECK("away: a key pressed elsewhere does not leave", !hs_director_wants_to_close());
  CHECK("away: and the story goes on", !hs_director_paused());
  // Back while moving on turns it round.
  hs_director_set_chapter(HS_CH_LATTICE);
  run_frames(5, &in, true, false, &clock);
  in.keys = HS_KEY_SOUTH;
  run_frames(0.05, &in, true, false, &clock);
  in.keys = 0;
  run_frames(0.1, &in, true, false, &clock);
  in.keys = HS_KEY_EAST;
  run_frames(0.05, &in, true, false, &clock);
  in.keys = 0;
  run_frames(5, &in, true, false, &clock);
  CHECK("moving on: back turns it round", hs_director_chapter() == HS_CH_LATTICE);
  // Words never snap, however fast the keys go.
  hs_director_set_chapter(HS_CH_GEARS);
  float biggest_step = 0, was_alpha = S.scene.text_alpha;
  for (int i = 0; i < 300; i++) {
    in.keys = i % 20 == 0 ? HS_KEY_SOUTH : i % 20 == 10 ? HS_KEY_EAST : 0;
    run_frames(1.0 / 60, &in, true, false, &clock);
    biggest_step = fmaxf(biggest_step, fabsf(S.scene.text_alpha - was_alpha));
    was_alpha = S.scene.text_alpha;
  }
  in.keys = 0;
  printf("largest caption step in a frame while spamming: %.3f\n", biggest_step);
  CHECK("spamming: the words fade, never snap", biggest_step < 0.2f);
  // Next and back as fast as a thumb can go, through every chapter and home again.
  for (int i = 0; i < 400; i++) {
    in.keys = i % 3 == 2 ? HS_KEY_EAST : HS_KEY_SOUTH;
    run_frames(0.05, &in, true, false, &clock);
    in.keys = 0;
    run_frames(0.05, &in, true, false, &clock);
  }
  CHECK("spamming next and back: still in a valid chapter", hs_director_chapter() < HS_CH_COUNT);
  run_frames(10, &in, true, false, &clock);
  CHECK("spamming: settles", hs_director_phase() == HS_PHASE_PLAY || hs_director_phase() == HS_PHASE_AWAKEN);
  // Every chapter, entered with both triggers held and both fingers down, and left the same way.
  in.left_trigger = in.right_trigger = 200;
  in.touch[0].down = in.touch[1].down = true;
  in.touch[0].x = 400; in.touch[0].y = 300; in.touch[1].x = 1500; in.touch[1].y = 800;
  for (int c = 1; c < HS_CH_LISTENING; c++) {
    hs_director_set_chapter((hs_chapter)c);
    run_frames(1, &in, true, false, &clock);
  }
  in.left_trigger = in.right_trigger = 0;
  in.touch[0].down = in.touch[1].down = false;
  hs_director_finish();
  CHECK("quitting at any instant: no trigger force stays", hs_link_triggers_are_off());
  // Microphone never asked, or refused: the chapter stays alive through the bellows.
  hs_mic_simulate(HS_MIC_NOT_ASKED, 0, 0);
  hs_director_set_chapter(HS_CH_BREATH);
  run_frames(5, &in, true, false, &clock);
  CHECK("microphone not asked: nothing asks before the key is pressed", hs_mic_get_state() == HS_MIC_NOT_ASKED);
  hs_mic_simulate(HS_MIC_DENIED, 0, 0);
  in.left_trigger = 200;
  run_frames(2, &in, true, false, &clock);
  CHECK("microphone refused: the trigger drives the plate", S.scene.chladni_shake > 0.3f);
  in.left_trigger = 0;
  // Long idle: 30 minutes with nobody there.
  hs_director_set_chapter(HS_CH_BALANCE);
  run_frames(1800, &in, true, false, &clock);
  CHECK("30 minutes idle: nothing has gone wrong", hs_world_rescues() == 0 && hs_director_idle_seconds() > 1000);
  printf("%d failures\n", failures);
  return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------- The same tour in the real window, in real time, silent
static double g_live_fed;
static void live_script(hs_input *in, double seconds) {
  bool moved;
  tour_input(in, seconds, &moved);
  bool shaking = hs_director_chapter() == HS_CH_BALANCE && fmod(seconds, 13) < 1.0;
  while (g_live_fed < seconds) {
    feed_hand(g_live_fed, 1.0f, shaking);
    g_live_fed += 1.0 / 133;
  }
}

int hs_test_full_screen(int argc, char **argv) {
  double seconds = 320;
  for (int i = 0; i + 1 < argc; i += 2) if (!strcmp(argv[i], "--seconds")) seconds = atof(argv[i + 1]);
  hs_link_simulate();
  hs_link_start();
  g_hand.q = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  hs_experience_set_script(live_script);
  NSApplication *app = NSApplication.sharedApplication;
  [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
  hs_app_install_menu();
  hs_experience_open(HS_CH_ASSEMBLY);
  // Heard by nobody: this gate measures frames, and people may be asleep.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.2 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ hs_sound_set_master(0, 0.002f); });
  [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) { (void)t; hs_sound_set_master(0, 0.002f); }];
  __block double peak_thermal = 0;
  [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *t) { (void)t; peak_thermal = fmax(peak_thermal, (double)NSProcessInfo.processInfo.thermalState); }];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    long frames = 0, skipped = 0;
    for (int c = 0; c < HS_CH_COUNT; c++) {
      hs_frame_report r = hs_experience_frame_report(c);
      if (r.frames == 0) continue;
      printf("chapter %d frames %ld skipped %ld interval p50 %.2f p95 %.2f p99 %.2f worst %.2f main worst %.2f ms drawable %ldx%ld\n", c, r.frames, r.skipped, r.p50, r.p95, r.p99, r.worst, r.main_worst_ms, r.drawable_width, r.drawable_height);
      frames += r.frames;
      skipped += r.skipped;
    }
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    printf("total frames %ld skipped %ld thermal %d resident %.0f MB\n", frames, skipped, (int)peak_thermal, (double)info.resident_size / 1e6);
    fflush(stdout);
    hs_experience_close();
    hs_link_stop();
    exit(0);
  });
  [app run];
  return 0;
}

// For a film of the app: which moments to draw, and from where. One line each: from to, eye and target at the start, eye and target at the end, field of view.
typedef struct { double from, to; simd_float3 eye[2], target[2]; float fov; } VideoShot;
static VideoShot g_shots[64];
static int g_shot_count;

static void load_shot_list(const char *path) {
  if (!path) return;
  FILE *f = fopen(path, "r");
  if (!f) return;
  float e[12], fov;
  double a, b;
  while (g_shot_count < 64 && fscanf(f, "%lf %lf %f %f %f %f %f %f %f %f %f %f %f %f %f", &a, &b, &e[0], &e[1], &e[2], &e[3], &e[4], &e[5], &e[6], &e[7], &e[8], &e[9], &e[10], &e[11], &fov) == 15)
    g_shots[g_shot_count++] = (VideoShot){a, b, {simd_make_float3(e[0], e[1], e[2]), simd_make_float3(e[6], e[7], e[8])}, {simd_make_float3(e[3], e[4], e[5]), simd_make_float3(e[9], e[10], e[11])}, fov};
  fclose(f);
}

int hs_test_experience(int argc, char **argv) {
  const char *video_path = NULL;
  const char *script = "rest", *sound_path = NULL, *hands_path = NULL, *speaker_path = NULL, *marks_path = NULL;
  NSString *shot = nil;
  NSMutableArray<NSNumber *> *shot_at = [NSMutableArray array];
  double seconds = 30, gpu_every = 0;
  int chapter = 0, width = 2560, height = 1664, seed = 0;
  bool mic_denied = false;
  for (int i = 0; i + 1 < argc; i += 2) {
    const char *k = argv[i], *v = argv[i + 1];
    if (!strcmp(k, "--script")) script = v;
    else if (!strcmp(k, "--seconds")) seconds = atof(v);
    else if (!strcmp(k, "--chapter")) chapter = atoi(v);
    else if (!strcmp(k, "--sound")) sound_path = v;
    else if (!strcmp(k, "--video")) video_path = v;
    else if (!strcmp(k, "--hands")) hands_path = v;
    else if (!strcmp(k, "--speaker")) speaker_path = v;
    else if (!strcmp(k, "--marks")) marks_path = v;
    else if (!strcmp(k, "--shot")) shot = @(v);
    else if (!strcmp(k, "--size")) sscanf(v, "%dx%d", &width, &height);
    else if (!strcmp(k, "--gpu-every")) gpu_every = atof(v);
    else if (!strcmp(k, "--mic")) mic_denied = !strcmp(v, "denied");
    else if (!strcmp(k, "--shot-at")) for (NSString *item in [@(v) componentsSeparatedByString:@","]) [shot_at addObject:@(item.doubleValue)];
  }
  bool fuzz = !strncmp(script, "fuzz:", 5), tour = !strcmp(script, "tour");
  if (fuzz) seed = atoi(script + 5);
  Random random = {(uint32_t)seed * 2654435761u + 1};

  NSString *resources = NSBundle.mainBundle.resourcePath;
  static hs_mesh_pack pack;
  NSString *model = [resources stringByAppendingPathComponent:@"instrument.hsmesh"];
  if (getenv("HANDSONG_MODEL")) model = @(getenv("HANDSONG_MODEL"));
  if (!hs_mesh_pack_load(model.fileSystemRepresentation, &pack)) { fprintf(stderr, "no model\n"); return 1; }
  if (!hs_stems_load([resources stringByAppendingPathComponent:@"score"].fileSystemRepresentation)) { fprintf(stderr, "no score\n"); return 1; }
  HSRenderer *renderer = nil;
  if (shot.length || gpu_every > 0 || video_path) {
    renderer = [[HSRenderer alloc] initWithDevice:MTLCreateSystemDefaultDevice() resources:resources meshPack:&pack];
    if (!renderer) return 1;
    for (int i = 0; i < HS_TEXT_COUNT; i++) [renderer prepareText:@(hs_text_line(i)) key:i];
    [renderer waitForText];
  }
  if (getenv("HANDSONG_SHOTS")) { load_shot_list(getenv("HANDSONG_SHOTS")); hs_sound_mute_music(); }
  if (video_path) {
    if (!hs_video_open(video_path, width / 2, height / 2)) { fprintf(stderr, "cannot write the video\n"); return 1; }
    renderer.frameSink = ^(id<MTLTexture> frame) { hs_video_add(frame); };
  }
  hs_link_simulate();
  hs_motion_reset();
  g_hand.q = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  hs_mic_simulate(mic_denied ? HS_MIC_DENIED : HS_MIC_GRANTED, 0, 0);
  hs_director_start(&pack, (hs_chapter)chapter, false);
  hs_sound_set_stems(hs_stems_table(), hs_stems_count());
  long voice_frames = 0;
  const int16_t *voice = hs_stems_voice_45k(&voice_frames);
  hs_voice_load(voice, voice_frames);

  if (getenv("HANDSONG_SOUND_EVENTS")) hs_sound_log_events_to(getenv("HANDSONG_SOUND_EVENTS"));
  FILE *sound_file = sound_path ? fopen(sound_path, "wb") : NULL;
  FILE *speaker_file = speaker_path ? fopen(speaker_path, "wb") : NULL;
  FILE *marks = marks_path ? fopen(marks_path, "w") : NULL;
  const char *crew_log_path = getenv("HANDSONG_CREW_LOG");
  FILE *crew_log = crew_log_path ? fopen(crew_log_path, "w") : NULL;
  if (hands_path) hs_hands_log_to(hands_path);
  static float audio[HS_SOUND_BLOCK * 16];
  static float speaker[HS_SPEAKER_PACKET_FRAMES * 2 * 2];
  int8_t waveform[HS_WAVE_BYTES];
  double t = 0, next_block = 0, next_shot_gpu = 0;
  long audio_frames = 0, frames = 0;
  double fed = 0;
  hs_chapter last_chapter = HS_CH_COUNT;
  hs_phase last_phase = HS_PHASE_LEAVING;
  NSUInteger shot_index = 0;
  long bad = 0;
  double worst_ms = 0;
  hs_input input = {0};
  input.left_x = input.left_y = input.right_x = input.right_y = 128;
  double gpu_sum[HS_CH_COUNT] = {0}, gpu_worst[HS_CH_COUNT] = {0};
  double memory_early = 0, stuck_since = 0;
  long stuck = 0;
  hs_chapter stuck_chapter = HS_CH_COUNT;
  hs_phase stuck_phase = HS_PHASE_PLAY;
  long gpu_n[HS_CH_COUNT] = {0};
  while (t < seconds) {
    bool moved = true;
    if (tour) tour_input(&input, t, &moved);
    else if (fuzz) fuzz_input(&input, &random, t);
    bool shaking = hs_director_chapter() == HS_CH_BALANCE && fmod(t, 13) < 1.0;
    if (fuzz) shaking = fmod(t * 0.37 + seed, 5.0) < 1.0;
    float amount = tour || fuzz ? 1.0f : 0.0f;
    while (fed < t) {
      feed_hand(fed, amount, shaking);
      fed += 1.0 / 133;
    }
    hs_frame_in in = {.input = input, .connected = !(fuzz && fmod(t + seed, 41) < 2), .now_ms = t * 1000, .target_ms = t * 1000 + 16, .dt = kFrame, .focus_lost = fuzz && fmod(t + seed * 3, 37) < 0.5};
    hs_scene *scene;
    hs_director_frame(&in, &scene);
    hs_lights_frame(scene);
    if (!all_finite(scene)) {
      if (bad++ < 3) {
        float p[3], q[4], v[3];
        hs_world_pose(0, p, q, v);
        fprintf(stderr, "invalid numbers at %.2f s in chapter %d: eye %f %f %f fov %f object %f, body0 %f %f %f, q %f %f %f %f, felt\n", t, (int)hs_director_chapter(), scene->eye.x, scene->eye.y, scene->eye.z, scene->fov_degrees, scene->object.columns[0].x, p[0], p[1], p[2], q[0], q[1], q[2], q[3]);
      }
    }
    while (next_block <= t) {
      double started = CACurrentMediaTime();
      hs_live_block(next_block * 1000, waveform, NULL, NULL, NULL);
      double spent = (CACurrentMediaTime() - started) * 1000;
      if (spent > worst_ms) worst_ms = spent;
      if (hs_voice_active()) {
        hs_voice_block(speaker);
        if (speaker_file) fwrite(speaker, sizeof(float), HS_SPEAKER_PACKET_FRAMES * 2 * 2, speaker_file);
      }
      next_block += HS_STREAM_PERIOD_MS / 1000;
    }
    long want = (long)((t + kFrame) * HS_SOUND_RATE);
    while (audio_frames < want) {
      hs_sound_render(audio, HS_SOUND_BLOCK);
      if (sound_file) fwrite(audio, sizeof(float), HS_SOUND_BLOCK * 2, sound_file);
      audio_frames += HS_SOUND_BLOCK;
    }
    if (marks && (hs_director_chapter() != last_chapter || hs_director_phase() != last_phase)) {
      last_chapter = hs_director_chapter();
      last_phase = hs_director_phase();
      fprintf(marks, "%.3f %d %d\n", t, (int)last_chapter, (int)last_phase);
    }
    if (video_path && g_shot_count > 0) {
      // A shot list: only these moments are drawn, each through its own camera, which eases from its first place to its second.
      scene->text_key = scene->nav_key = -1;
      scene->caption_shade = 0;
      for (int i = 0; i < g_shot_count; i++) {
        const VideoShot *v = &g_shots[i];
        if (t < v->from || t >= v->to) continue;
        float u = (float)((t - v->from) / (v->to - v->from));
        u = u * u * (3 - 2 * u) * 0.5f + u * 0.5f;
        if (v->fov > 0) {      // no field of view: the story's own camera
          scene->eye = v->eye[0] + (v->eye[1] - v->eye[0]) * u;
          scene->target = v->target[0] + (v->target[1] - v->target[0]) * u;
          scene->fov_degrees = v->fov;
        }
        [renderer draw:scene into:nil size:CGSizeMake(width, height)];
        [renderer waitUntilIdle];
      }
    } else if (video_path) {
      if (getenv("HANDSONG_NO_WORDS")) { scene->text_key = scene->nav_key = -1; scene->caption_shade = 0; }
      static bool voice_said;
      if (!voice_said && hs_voice_active()) { voice_said = true; fprintf(stderr, "the speaker's voice begins at %.4f s\n", t); }
      [renderer draw:scene into:nil size:CGSizeMake(width, height)];
      [renderer waitUntilIdle];
    } else if (renderer && shot_index < shot_at.count && t >= shot_at[shot_index].doubleValue) {
      [renderer saveNextFrameTo:[NSString stringWithFormat:@"%@%.1f.png", shot, shot_at[shot_index].doubleValue]];
      [renderer draw:scene into:nil size:CGSizeMake(width, height)];
      [renderer waitUntilIdle];
      shot_index++;
    } else if (renderer && gpu_every > 0 && t >= next_shot_gpu) {
      // The GPU clocks up only under load, so the first frames measure the clock and not the frame: the median of a burst's last frames counts.
      double burst[9];
      for (int repeat = 0; repeat < 30; repeat++) {
        if (repeat == 21) [renderer resetPassReport];
        [renderer draw:scene into:nil size:CGSizeMake(width, height)];
        [renderer waitUntilIdle];
        if (repeat >= 21) {
          int place = repeat - 21;
          double value = renderer.lastGpuSeconds * 1000;
          while (place > 0 && burst[place - 1] > value) { burst[place] = burst[place - 1]; place--; }
          burst[place] = value;
        }
      }
      double gpu = burst[4];
      int c = (int)hs_director_chapter();
      gpu_sum[c] += gpu;
      gpu_n[c]++;
      if (gpu > gpu_worst[c]) gpu_worst[c] = gpu;
      if (getenv("HANDSONG_GPU_TRACE")) printf("t %.1f chapter %d phase %d gpu %.2f %s\n", t, c, (int)hs_director_phase(), gpu, renderer.passReport.UTF8String);
      next_shot_gpu = t + gpu_every;
    }
    if (hs_director_phase() == HS_PHASE_PLAY || hs_director_paused() || !in.connected || hs_director_chapter() != stuck_chapter || hs_director_phase() != stuck_phase) {
      stuck_since = t;
      stuck_chapter = hs_director_chapter();
      stuck_phase = hs_director_phase();
    } else if (t - stuck_since > 60) {
      stuck++;
      stuck_since = t;
    }
    if (memory_early == 0 && t > 60) memory_early = resident_mb();
    if (crew_log && frames % 60 == 0) {
      fprintf(crew_log, "%.1f %d", t, (int)hs_director_chapter());
      for (int who = 0; who < HS_CREW; who++) {
        float p[3], q[4], v[3];
        hs_world_pose(who, p, q, v);
        fprintf(crew_log, " %.1f,%.1f", p[0] * 1000, (p[2] + 0.010f) * 1000);
      }
      fprintf(crew_log, "\n");
    }
    t += kFrame;
    frames++;
  }
  double cluster;
  hs_crew_cluster_report(&cluster);
  double memory_late = resident_mb();
  hs_director_finish();
  bool trigger_free = hs_link_triggers_are_off();
  if (fuzz) printf("fuzz seed %d: stuck %ld, memory growth %.1f MB, trigger force left %d\n", seed, stuck, memory_late - memory_early, !trigger_free);
  printf("frames %ld, invalid %ld, rescues %ld, worst block %.2f ms, longest cluster %.1f s, sound blocks %ld, limiter over 1 dB %ld\n", frames, bad, hs_world_rescues(), worst_ms, cluster, hs_sound_blocks(), hs_sound_clipped_blocks());
  for (int c = 0; c < HS_CH_COUNT; c++) if (gpu_n[c]) printf("chapter %d GPU mean %.2f worst %.2f ms over %ld samples\n", c, gpu_sum[c] / (double)gpu_n[c], gpu_worst[c], gpu_n[c]);
  if (video_path) { [renderer waitUntilIdle]; hs_video_close(); }
  if (sound_file) fclose(sound_file);
  if (speaker_file) fclose(speaker_file);
  if (marks) fclose(marks);
  hs_hands_log_to(NULL);
  if (fuzz && (stuck || memory_late - memory_early > 2.0 || !trigger_free || hs_world_rescues() > 0)) return 1;
  return bad ? 1 : 0;
}

// ---------------------------------------------------------------------------- Going elsewhere and coming back, in the real window
static void focus_check(const char *what, bool ok, int *failures) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) (*failures)++;
}

static size_t desktop_points_wide(void) {
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
  size_t width = CGDisplayModeGetWidth(mode);
  CGDisplayModeRelease(mode);
  return width;
}

int hs_test_focus(void) {
  hs_link_simulate();
  hs_link_start();
  g_hand.q = simd_quaternion(0.0f, simd_make_float3(0, 1, 0));
  hs_experience_set_script(live_script);
  NSApplication *app = NSApplication.sharedApplication;
  [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
  hs_app_install_menu();
  size_t desktop = desktop_points_wide();
  hs_experience_open(HS_CH_ASSEMBLY);
  [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) { (void)t; if (hs_experience_in_front()) hs_sound_set_master(0, 0.002f); }];
  static int failures;
  // The system alone may bring an app forward now, as the app switcher and the Dock do, and this asks it to in the same way.
  void (^bring_forward)(void) = ^{ [NSTask launchedTaskWithLaunchPath:@"/usr/bin/open" arguments:@[NSBundle.mainBundle.bundlePath]]; };
  NSRunningApplication *finder = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.finder"].firstObject;
  __block double at = 0;
  void (^after)(double, dispatch_block_t) = ^(double seconds, dispatch_block_t step) {
    at += seconds;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(at * NSEC_PER_SEC)), dispatch_get_main_queue(), step);
  };
  after(4, ^{
    focus_check("the picture is in front", hs_experience_in_front() && NSApp.active, &failures);
    printf("     desktop %zu points wide, now %zu\n", desktop, desktop_points_wide());
    [finder activateWithOptions:0];
  });
  after(1.5, ^{
    focus_check("another app in front: the picture steps back", !hs_experience_in_front() && !NSApp.active, &failures);
    focus_check("the desktop has its own display mode again", desktop_points_wide() == desktop, &failures);
    focus_check("no window of ours is left over it", NSApp.mainWindow == nil, &failures);
    focus_check("the story is paused", hs_director_paused(), &failures);
    bring_forward();
  });
  after(2, ^{
    focus_check("back again: the picture is in front", hs_experience_in_front() && NSApp.active, &failures);
    focus_check("and the story goes on by itself", !hs_director_paused(), &failures);
    [NSApp hide:nil];
  });
  after(1.5, ^{
    focus_check("Command-H: steps back as well", !hs_experience_in_front(), &failures);
    bring_forward();
  });
  after(2, ^{
    focus_check("unhidden: in front", hs_experience_in_front(), &failures);
    hs_experience_ask_quit();
  });
  after(0.5, ^{
    focus_check("Command-Q asks first", hs_director_paused() && !hs_director_wants_to_quit(), &failures);
    hs_experience_ask_quit();
    focus_check("asked twice is an answer", hs_director_wants_to_quit(), &failures);
    hs_experience_close();
    hs_link_stop();
    focus_check("the desktop is back after closing", desktop_points_wide() == desktop, &failures);
    printf("%d failures\n", failures);
    fflush(stdout);
    exit(failures ? 1 : 0);
  });
  [app run];
  return 0;
}

// ---------------------------------------------------------------------------- Do the hands really move? The controller's own sensor says.
// With the owner there: on the real controller, in the real window, silent. The actuators shake the accelerometer, so this needs no one's hands.
int hs_test_hands_proof(bool voice) {
  hs_link_start();
  usleep(800000);
  if (hs_link_state() == HS_CONTROLLER_NONE) { printf("no controller\n"); return 2; }
  NSApplication *app = NSApplication.sharedApplication;
  [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
  hs_app_install_menu();
  hs_experience_open(HS_CH_COILS);
  if (voice) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(9 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    // With a tone asked for, the speaker plays that instead of its music, so a microphone can say how clean the wide link's path is.
    if (getenv("HANDSONG_TONE")) {
      static int16_t tone[45000];
      double hertz = atof(getenv("HANDSONG_TONE")), gain = getenv("HANDSONG_TONE_GAIN") ? atof(getenv("HANDSONG_TONE_GAIN")) : 0.2;
      long period = lround(45000 / hertz * floor(hertz));      // a whole number of cycles, so the loop has no seam
      for (long i = 0; i < 45000; i++) tone[i] = (int16_t)(sin(2 * M_PI * floor(hertz) * (double)i / 45000) * gain * 32767);
      (void)period;
      hs_voice_load(tone, 45000);
    }
    hs_go_to(HS_CH_VOICE);
  });
  [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer *t) { (void)t; hs_sound_set_master(0, 0.002f); }];
  static double start, sum, sum2;
  static long n, last_sent;
  start = hs_now_ms();
  [NSTimer scheduledTimerWithTimeInterval:0.004 repeats:YES block:^(NSTimer *timer) {
    (void)timer;
    static long seen;
    hs_input in;
    hs_link_snapshot(&in);
    long stamp = hs_link_get_stats().stream_sent + (long)in.accel[0] * 3 + (long)in.accel[1] * 7 + (long)in.accel[2] * 11;
    if (stamp == seen) return;
    seen = stamp;
    double size = sqrt((double)in.accel[0] * in.accel[0] + (double)in.accel[1] * in.accel[1] + (double)in.accel[2] * in.accel[2]);
    sum += size;
    sum2 += size * size;
    n++;
  }];
  // A sweep at full strength: where do these actuators answer most?
  static const float sweep[] = {30, 40, 50, 60, 70, 80, 100, 120, 140, 160, 180, 200, 240, 280, 320, 400};
  [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *timer) {
    (void)timer;
    double t = (hs_now_ms() - start) / 1000;
    int step = (int)(t / 2) - 2;
    if (!voice && step >= 0 && step < 16 && (int)(t * 2) % 4 == 1) hs_hands_hit(sweep[step], 0.5f, 1.0f, 0);
    else if (!voice && step >= 0 && step < 16 && (int)(t * 2) % 4 == 2) hs_hands_hit(sweep[step], 0.5f, 1.0f, 0);
  }];
  [NSTimer scheduledTimerWithTimeInterval:2 repeats:YES block:^(NSTimer *timer) {
    (void)timer;
    double t = (hs_now_ms() - start) / 1000;
    hs_link_stats st = hs_link_get_stats();
    double variance = n > 1 ? sum2 / (double)n - (sum / (double)n) * (sum / (double)n) : 0;
    float left, right;
    hs_hands_view(&left, &right);
    hs_input in;
    hs_link_snapshot(&in);
    int sweep_step = (int)((t - 2) / 2) - 2;
    if (sweep_step >= 0 && sweep_step < 16) printf("%5.0f Hz: ", sweep[sweep_step]);
    printf("t %4.0f wide %d mode %d rate %3.0f/s battery %d sent +%ld fail %ld under %ld speaker-under %ld held %ld narrow %ld gap %.0f | shake rms %7.1f counts over %ld | view %.2f %.2f\n", t, hs_link_wide(), (int)hs_link_mode(), hs_link_input_rate(), in.battery_percent,
           st.stream_sent - last_sent, st.send_failures, st.waveform_underruns, st.speaker_underruns, st.skipped_in_flight, st.dropped_narrow, st.worst_stream_gap_ms, sqrt(fmax(variance, 0)), n, left, right);
    fflush(stdout);
    last_sent = st.stream_sent;
    sum = sum2 = 0;
    n = 0;
    if (t > 40) { hs_experience_close(); hs_link_stop(); exit(0); }
  }];
  [app run];
  return 0;
}
