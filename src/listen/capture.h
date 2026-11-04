// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Hears what the Mac plays, through a system audio tap. Nothing is kept: audio passes through a one-second ring and is gone.
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct hs_capture hs_capture;

// mute_mac: the Mac's speakers go quiet, for playing on the controller instead. A delay above zero also mutes the system's
// own path and replays the sound that much later, to land with the hands. Returns NULL on failure, not on missing consent:
// macOS then delivers silence, so the caller watches for it.
hs_capture *hs_capture_start(bool mute_mac, double delay_ms);
void hs_capture_stop(hs_capture *capture);

double hs_capture_rate(const hs_capture *capture);
size_t hs_capture_available(const hs_capture *capture);
size_t hs_capture_read(hs_capture *capture, float *stereo, size_t frames);   // realtime safe, one reader
void hs_capture_skip_to_newest(hs_capture *capture, size_t keep_frames);
double hs_capture_last_signal_ms(const hs_capture *capture);
void hs_capture_set_delay(hs_capture *capture, double delay_ms);             // eases over seconds, so it never clicks
