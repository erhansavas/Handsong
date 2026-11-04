// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "app/home/bridge.h"
#import "app/care.h"
#import "experience/experience.h"
#import "link/link.h"
#import "listen/listen.h"

static NSString *const kFeelKey = @"feelTheMusic", *const kSpeakerKey = @"playOnController", *const kCalmKey = @"calmLights";
static NSString *const kFlashKey = @"flashNoteSeen", *const kFirstRunKey = @"firstRunDone";

hs_home_status hs_home_get_status(void) {
  hs_home_status s = {0};
  hs_input in;
  hs_link_snapshot(&in);
  switch (hs_link_state()) {
    case HS_CONTROLLER_NONE: s.controller = 0; break;
    case HS_CONTROLLER_READY: s.controller = 1; break;
    case HS_CONTROLLER_ON_CABLE: s.controller = 2; break;
    case HS_CONTROLLER_UNTESTED: s.controller = 3; break;
  }
  s.battery_percent = in.battery_percent;
  s.charging = in.charging;
  s.link_wide = hs_link_wide();
  hs_listen_status listen = hs_listen_get_status();
  s.listen_state = (int)listen.state;
  s.left = listen.left;
  s.right = listen.right;
  s.hit = listen.hit;
  s.no_sound_seen = listen.no_sound_seen;
  NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
  s.feel_on = [d boolForKey:kFeelKey];
  s.speaker_on = [d boolForKey:kSpeakerKey];
  s.calm_lights = [d boolForKey:kCalmKey];
  s.experience_open = hs_experience_is_open();
  s.in_applications = hs_care_in_applications();
  s.login_item = hs_care_login_item();
  return s;
}

hs_home_input hs_home_get_input(void) {
  hs_input in;
  hs_link_snapshot(&in);
  hs_home_input out = {0};
  out.left_x = ((float)in.left_x - 127.5f) / 127.5f;
  out.left_y = ((float)in.left_y - 127.5f) / 127.5f;
  out.right_x = ((float)in.right_x - 127.5f) / 127.5f;
  out.right_y = ((float)in.right_y - 127.5f) / 127.5f;
  out.left_trigger = (float)in.left_trigger / 255;
  out.right_trigger = (float)in.right_trigger / 255;
  out.keys = in.keys;
  for (int i = 0; i < 2; i++) {
    out.touch_down[i] = in.touch[i].down;
    out.touch_x[i] = (float)in.touch[i].x / 1919;
    out.touch_y[i] = (float)in.touch[i].y / 1079;
  }
  return out;
}

// The link can carry the hands' stream or the speaker's, never both, so the two switches are one choice.
void hs_home_set_feel(bool on) {
  [NSUserDefaults.standardUserDefaults setBool:on forKey:kFeelKey];
  hs_listen_set_feel(on);
  if (on) {
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:kSpeakerKey];
    hs_listen_set_speaker(false);
  }
}

void hs_home_set_speaker(bool on) {
  [NSUserDefaults.standardUserDefaults setBool:on forKey:kSpeakerKey];
  hs_listen_set_speaker(on);
  if (on) {
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:kFeelKey];
    hs_listen_set_feel(false);
  }
}

bool hs_home_light_follows_music(void) { return [NSUserDefaults.standardUserDefaults boolForKey:@"lightFollowsMusic"]; }
void hs_home_set_light_follows_music(bool on) { [NSUserDefaults.standardUserDefaults setBool:on forKey:@"lightFollowsMusic"]; }

void hs_home_apply_calm_lights(void) { hs_link_set_lights_quiet([NSUserDefaults.standardUserDefaults boolForKey:kCalmKey], false); }

void hs_home_set_calm_lights(bool on) {
  [NSUserDefaults.standardUserDefaults setBool:on forKey:kCalmKey];
  hs_link_set_lights_quiet(on, false);
}

void hs_home_set_login_item(bool on) { hs_care_set_login_item(on); }
void hs_home_open_experience(void) { hs_experience_open(HS_CH_ASSEMBLY); }
void hs_home_open_free_play(void) { hs_experience_open(HS_CH_WORKSHOP); }
bool hs_home_flash_warning_needed(void) { return ![NSUserDefaults.standardUserDefaults boolForKey:kFlashKey]; }
void hs_home_flash_warning_seen(void) { [NSUserDefaults.standardUserDefaults setBool:YES forKey:kFlashKey]; }
void hs_home_open_listening_room(void) {
  // Full Feel with the Mac's sound going nowhere would be an empty room that says it is full.
  NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
  if (![d boolForKey:kFeelKey] && ![d boolForKey:kSpeakerKey]) hs_home_set_feel(true);
  hs_experience_open(HS_CH_LISTENING);
}

// A short push through one hand, or a wave from left to right, with the two rumble bytes: they fit any link.
static void pulse(uint8_t big, uint8_t small, int ms) {
  hs_mode before = hs_link_mode();
  if (before == HS_MODE_WAVEFORM || before == HS_MODE_SPEAKER) return;
  hs_listen_hold_off(ms + 100);
  hs_link_set_mode(HS_MODE_RUMBLE);
  hs_link_set_rumble(big, small);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, ms * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    hs_link_set_rumble(0, 0);
    hs_link_set_mode(before);
  });
}

void hs_home_pulse_left(void) { pulse(200, 0, 180); }
void hs_home_pulse_right(void) { pulse(0, 200, 180); }

void hs_home_open_bluetooth_settings(void) { [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.BluetoothSettings"]]; }
void hs_home_remove_everything(void) { hs_care_remove_everything(); }
