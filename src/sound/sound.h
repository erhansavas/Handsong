// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Everything the Mac plays during the experience, one continuous stream from the first second to quit: the score's stems, struck
// materials with their modes, and sounds that follow the hands in real time. The mix itself is deterministic and has no I/O, so a
// whole tour can be written to a file and measured.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { HS_SOUND_RATE = 48000, HS_SOUND_BLOCK = 256 };

typedef enum {
  HS_MAT_SOUND_BRASS, HS_MAT_SOUND_STEEL, HS_MAT_SOUND_CERAMIC, HS_MAT_SOUND_GLASS, HS_MAT_SOUND_WOOD, HS_MAT_SOUND_PAPER, HS_MAT_SOUND_BELL,
  HS_MAT_SOUND_COUNT,
} hs_sound_material;

typedef enum {
  HS_TIMING_DIRECT,    // belongs to the press itself: the next block
  HS_TIMING_ALIGNED,   // belongs to what the hand feels: delayed to land with the actuator, 110 ms after the report
} hs_sound_timing;

// A struck thing, from the frame thread or (with _rt) from the block thread.
void hs_sound_strike(hs_sound_material material, float hertz, float gain, float pan, hs_sound_timing timing);
void hs_sound_strike_rt(hs_sound_material material, float hertz, float gain, float pan);
void hs_sound_strike_now(hs_sound_material material, float hertz, float gain, float pan);   // from the input thread: the next block
void hs_sound_strike_with_hand(hs_sound_material material, float hertz, float gain, float pan);   // from the input thread: lands with the actuator

typedef enum {
  HS_SOUND_RUB,       // rubbing on glass: level, pitch
  HS_SOUND_ROLL,      // a ball rolling: level, brightness
  HS_SOUND_LOAD,      // the arm under load: level, pitch
  HS_SOUND_STEAM,     // level, brightness
  HS_SOUND_BELLOWS,   // level, brightness
  HS_SOUND_HISS,      // the filament and thin high air: level, brightness
  HS_SOUND_DRONE,     // a low steady note under the opening: level, pitch
  HS_SOUND_LAYER_COUNT,
} hs_sound_layer;

void hs_sound_set(hs_sound_layer layer, float level, float tone);

typedef struct { int16_t *samples; long frames; } hs_stem;   // interleaved stereo at the stream's rate

enum { HS_STEM_SLOTS = 4 };

// The score. Each slot crossfades on its own to a new stem and a new level, and loops until told otherwise.
void hs_sound_set_stems(const hs_stem *stems, int count);
void hs_sound_score(int slot, int stem, float gain, float seconds, bool loop);
void hs_sound_score_gain(int slot, float gain, float seconds);

void hs_sound_set_room_latency(int frames);   // how many frames the output adds, so aligned sounds land with the hands
void hs_sound_set_pause(bool paused);         // low pass and 12 dB down, gently, and back
void hs_sound_set_master(float gain, float seconds);
void hs_sound_mute_music(void);                       // a film's: everything but the music, which it lays in itself
void hs_sound_log_events_to(const char *path);      // a test's: when each struck sound and each piece of music began, in samples

void hs_sound_init(void);
void hs_sound_render(float *stereo, int frames);    // the audio thread, or a test
long hs_sound_clipped_blocks(void);                 // blocks in which the limiter had to reduce by more than 1 dB
long hs_sound_blocks(void);
float hs_sound_output_db(void);
