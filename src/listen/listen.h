// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Daily listening: music in the hands, music on the controller's speaker. One switch each, nothing to tune.
#pragma once
#include "listen/feel.h"

typedef enum {
  HS_LISTEN_OFF,
  HS_LISTEN_WAITING,        // on, and nothing has played for a while
  HS_LISTEN_FEELING,        // full waveform in the hands
  HS_LISTEN_FEELING_SOFT,   // the link is narrow, so the hands get the softer path
  HS_LISTEN_SPEAKER,
  HS_LISTEN_SPEAKER_NEEDS_ROOM,  // the speaker needs the wider link only the full-screen Listening Room gets
  HS_LISTEN_STEPPED_ASIDE,  // a game or the experience has the controller
} hs_listen_state;

typedef struct {
  hs_listen_state state;
  float left, right, hit, speech;
  bool speaker_chosen;
  bool no_sound_seen;       // on for a while, nothing ever heard: either silence or no permission yet
} hs_listen_status;

void hs_listen_start(void);
void hs_listen_stop(void);
void hs_listen_set_feel(bool on);
void hs_listen_set_speaker(bool on);
void hs_listen_step_aside(bool aside);          // a game or the experience is in front
void hs_listen_set_video_in_front(bool video);  // keeps the sound closer to the picture, the hands a little behind
void hs_listen_hold_off(double ms);              // leave the link alone for a moment: something short is using it
void hs_listen_output_changed(void);
hs_listen_status hs_listen_get_status(void);
