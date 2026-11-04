// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The full-screen experience: a window the whole panel wide at its native mode, the frame loop, and the threads that serve it.
#pragma once
#include "experience/stage.h"
#include <stdbool.h>

void hs_experience_open(hs_chapter first);     // from the main thread. The first image is ready before anything shows
void hs_experience_close(void);
bool hs_experience_is_open(void);
bool hs_experience_in_front(void);             // open, and its picture is what the person is looking at
void hs_experience_ask_quit(void);             // asks on the picture itself

// For the frame-pacing gate: where input comes from instead of a controller, and what the frames did.
typedef void (*hs_experience_script)(hs_input *input, double seconds);
void hs_experience_set_script(hs_experience_script script);
typedef struct {
  long frames, skipped;
  double p50, p95, p99, worst;       // milliseconds between frames
  long drawable_width, drawable_height;
  double main_worst_ms;              // longest time spent preparing one frame on the main thread
} hs_frame_report;
hs_frame_report hs_experience_frame_report(int chapter);
