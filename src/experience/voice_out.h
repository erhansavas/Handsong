// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What goes to the controller's own speaker during the Voice chapter, and what the microphone hears during Breath.
#pragma once
#include "link/protocol.h"
#include <stdbool.h>
#include <stdint.h>

void hs_voice_load(const int16_t *mono_45k, long frames);
void hs_voice_start(void);
void hs_voice_stop(void);
void hs_voice_set_volume(float gain);
float hs_voice_level(void);                         // recent output level, 0 to 1, for the cone on screen
bool hs_voice_active(void);
// One block on the block thread: 960 stereo frames at 45 kHz.
void hs_voice_block(float *stereo);

typedef enum { HS_MIC_NOT_ASKED, HS_MIC_ASKING, HS_MIC_GRANTED, HS_MIC_DENIED } hs_mic_state;
void hs_mic_prepare(void);                          // while loading: the system's first answer takes longer than a frame should
hs_mic_state hs_mic_get_state(void);
float hs_mic_decibels(void);                         // how loud the last buffer was: for measuring
void hs_mic_request(void);                          // asks once, at the moment it means something
void hs_mic_set_listening(bool on);      // permission is not listening: open only while something is listening
void hs_mic_read(float *level, float *pitch);       // nothing is kept, only these two numbers
void hs_mic_stop(void);
void hs_mic_simulate(hs_mic_state state, float level, float pitch);   // scripted runs
