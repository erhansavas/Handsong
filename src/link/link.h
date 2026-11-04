// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The process's single connection to the controller. One thread owns the device, and nothing else ever writes to it.
#pragma once
#include "link/link_core.h"

typedef enum {
  HS_CONTROLLER_NONE,
  HS_CONTROLLER_READY,
  HS_CONTROLLER_ON_CABLE,     // connected by USB: the app works over Bluetooth
  HS_CONTROLLER_UNTESTED,     // a model this app has not been tested with; basic input only
} hs_controller_state;

// Called on the link thread for every input report. Must not block, allocate or take a lock.
typedef void (*hs_input_handler)(const hs_input *input, double host_ms, void *context);

void hs_link_start(void);
void hs_link_stop(void);                 // lets the triggers go, turns the speaker off, ends the thread

hs_controller_state hs_link_state(void);
void hs_link_snapshot(hs_input *out);    // the newest input, from any thread, never blocks
double hs_link_input_rate(void);         // reports per second; about 132 means Game Mode has widened the link
bool hs_link_wide(void);                 // the link carries a full-rate stream right now
hs_imu_calibration hs_link_calibration(void);
double hs_now_ms(void);

void hs_link_set_input_handler(hs_input_handler handler, void *context);
void hs_link_set_mode(hs_mode mode);
hs_mode hs_link_mode(void);
void hs_link_set_triggers(const hs_trigger_effect *right, const hs_trigger_effect *left);   // NULL leaves one alone
void hs_link_set_rumble(uint8_t big, uint8_t small);
void hs_link_set_lights_quiet(bool quiet, bool light_bar_too);
void hs_link_set_lights_show(bool on, uint8_t red, uint8_t green, uint8_t blue, uint8_t player_lights, uint8_t mic_light);
void hs_link_set_speaker_volume(int volume);
void hs_link_set_speaker_silence(const uint8_t packet[HS_SPEAKER_PACKET_BYTES]);
bool hs_link_push_waveform(const int8_t samples[HS_WAVE_BYTES]);
bool hs_link_push_speaker(const uint8_t opus[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES]);
hs_link_stats hs_link_get_stats(void);
bool hs_link_triggers_are_off(void);     // for the robustness gate: no force is being asked for

// Tests with no controller: input arrives from here instead, and reports go nowhere.
void hs_link_simulate(void);
