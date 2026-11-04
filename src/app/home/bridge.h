// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What the Home window sees of the app, in plain C so that Swift can read it.
#pragma once
#include <stdbool.h>
#import <AppKit/AppKit.h>

typedef struct {
  int controller;         // 0 none, 1 ready, 2 on a cable, 3 an untested model
  int battery_percent;
  bool charging;
  bool link_wide;
  int listen_state;       // hs_listen_state
  float left, right, hit;
  bool no_sound_seen;
  bool feel_on, speaker_on;
  bool experience_open;
  bool in_applications;
  bool login_item;
  bool calm_lights;
} hs_home_status;

typedef struct {
  float left_x, left_y, right_x, right_y, left_trigger, right_trigger;
  unsigned keys;
  bool touch_down[2];
  float touch_x[2], touch_y[2];
} hs_home_input;

hs_home_status hs_home_get_status(void);
hs_home_input hs_home_get_input(void);
void hs_home_set_feel(bool on);
void hs_home_set_speaker(bool on);
void hs_home_set_calm_lights(bool on);
bool hs_home_light_follows_music(void);
void hs_home_set_light_follows_music(bool on);
void hs_home_set_login_item(bool on);
void hs_home_apply_calm_lights(void);
void hs_home_open_experience(void);
void hs_home_open_free_play(void);
bool hs_home_flash_warning_needed(void);
void hs_home_flash_warning_seen(void);
void hs_home_open_listening_room(void);
void hs_home_pulse_left(void);
void hs_home_pulse_right(void);
void hs_home_open_bluetooth_settings(void);
void hs_home_remove_everything(void);
NSView *hs_home_preview_view(void);

// From the app to Swift.
void hs_home_show(void);
