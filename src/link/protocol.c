// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "link/protocol.h"
#include <stdlib.h>
#include <string.h>

// The checksum the controller asks for is the ordinary CRC-32. It also covers one byte in front of the report that is never sent, and
// goes in the report's last four bytes, lowest first.
enum { kHiddenFirstByte = 0xA2, kChecksumBytes = 4 };

static uint32_t checksum_byte(uint32_t state, uint8_t byte) {
  state ^= byte;
  for (int bit = 0; bit < 8; bit++) {
    bool out = state & 1u;
    state >>= 1;
    if (out) state ^= 0xEDB88320u;
  }
  return state;
}

static void store_checksum(uint8_t *report, size_t length) {
  size_t body = length - kChecksumBytes;
  uint32_t state = checksum_byte(0xFFFFFFFFu, kHiddenFirstByte);
  for (size_t at = 0; at < body; at++) state = checksum_byte(state, report[at]);
  state = ~state;
  for (size_t at = body; at < length; at++, state >>= 8) report[at] = (uint8_t)state;
}

hs_trigger_effect hs_trigger_off(void) {
  hs_trigger_effect e = {{0x05}};
  return e;
}

hs_trigger_effect hs_trigger_raw(uint8_t force) {
  hs_trigger_effect e = {{0x01, 0x00, force}};
  return e;
}

// Ten places along the pull. Bytes 1 and 2 say which act, bytes 3 to 6 hold how hard, three bits for each, one below the strength.
static hs_trigger_effect ten_place_effect(uint8_t kind, const uint8_t strength[10], uint8_t frequency) {
  hs_trigger_effect effect = {{kind}};
  for (int place = 0; place < 10; place++) {
    if (strength[place] == 0) continue;
    effect.bytes[1 + place / 8] |= (uint8_t)(1 << (place % 8));
    unsigned value = (unsigned)strength[place] - 1u;
    for (int bit = 0; bit < 3; bit++) {
      int position = place * 3 + bit;
      if (value >> bit & 1u) effect.bytes[3 + position / 8] |= (uint8_t)(1 << (position % 8));
    }
  }
  effect.bytes[9] = frequency;
  return effect;
}

// A push that grows from one strength to another between two places, and holds to the end of the pull.
hs_trigger_effect hs_trigger_spring(int from_zone, int to_zone, int start, int end) {
  uint8_t strength[10] = {0};
  int length = to_zone > from_zone ? to_zone - from_zone : 1;
  for (int place = from_zone; place < 10; place++) {
    int along = place < to_zone ? place - from_zone : length;
    strength[place] = (uint8_t)(start + (end - start) * along / length);
  }
  return ten_place_effect(0x21, strength, 0);
}

// Where things live in the control report, and the flags that say which of them to take notice of.
enum {
  kAtSequence = 1, kAtReportKind = 2, kAtFlagsA = 3, kAtFlagsB = 4, kAtRumbleSmall = 5, kAtRumbleBig = 6, kAtVolume = 8, kAtPath = 10, kAtMuteLight = 12,
  kAtTriggerRight = 13, kAtTriggerLeft = 24, kAtPreamp = 40, kAtFlagsC = 41, kAtLightBarState = 44,
  kFlagRumble = 0x02, kFlagTriggerRight = 0x04, kFlagTriggerLeft = 0x08, kFlagRouteAndBoost = 0x20, kFlagLights = 0x40, kFlagSpeakerVolume = 0x80,
  kFlagMuteLight = 0x01, kFlagMicMute = 0x02, kFlagLightBar = 0x04, kFlagPlayerLights = 0x10, kFlagTriggerMotors = 0x40, kFlagRoute = 0x80,
  kFlagBarOff = 0x02, kFlagNewRumble = 0x04,
};

enum { kAtMicLight = 11, kAtPlayerLights = 46, kAtLightBarColour = 47, kFlagMuteLightValid = 0x01 };

void hs_build_control(uint8_t report[HS_CONTROL_LEN], const hs_control *control, unsigned sequence) {
  memset(report, 0, HS_CONTROL_LEN);
  report[0] = HS_REPORT_CONTROL;
  report[kAtSequence] = (uint8_t)((sequence & 15) << 4);
  report[kAtReportKind] = 0x10;
  uint8_t flags_a = 0, flags_b = 0, flags_c = 0;
  if (control->set_right) {
    flags_a |= kFlagTriggerRight;
    memcpy(report + kAtTriggerRight, control->right.bytes, sizeof control->right.bytes);
  }
  if (control->set_left) {
    flags_a |= kFlagTriggerLeft;
    memcpy(report + kAtTriggerLeft, control->left.bytes, sizeof control->left.bytes);
  }
  if (control->set_right || control->set_left) flags_b |= kFlagTriggerMotors;
  if (control->rumble) {
    flags_a |= kFlagRumble;
    flags_c |= kFlagNewRumble;
    report[kAtRumbleSmall] = control->rumble_small;
    report[kAtRumbleBig] = control->rumble_big;
  }
  if (control->lights_off) {
    flags_a |= kFlagLights;
    flags_b |= kFlagMuteLight | kFlagMicMute | kFlagPlayerLights;
    report[kAtMuteLight] = 0x10;
    if (control->light_bar_off) {
      flags_b |= kFlagLightBar;
      flags_c |= kFlagBarOff;
      report[kAtLightBarState] = 0x02;
    }
  }
  if (control->lights_show) {
    flags_b |= kFlagLightBar | kFlagPlayerLights | kFlagMuteLightValid;
    report[kAtMicLight] = control->mic_light;
    report[kAtPlayerLights] = control->player_lights | 0x20;    // at once, with no fade of its own: the fading is done here
    report[kAtLightBarColour] = control->bar_red;
    report[kAtLightBarColour + 1] = control->bar_green;
    report[kAtLightBarColour + 2] = control->bar_blue;
  }
  if (control->route) {
    flags_a |= kFlagSpeakerVolume | kFlagRouteAndBoost;
    flags_b |= kFlagRoute;
    report[kAtVolume] = control->volume;
    report[kAtPath] = control->path;
    report[kAtPreamp] = control->preamp;
  }
  report[kAtFlagsA] = flags_a;
  report[kAtFlagsB] = flags_b;
  report[kAtFlagsC] = flags_c;
  store_checksum(report, HS_CONTROL_LEN);
}

void hs_build_waveform(uint8_t r[HS_STREAM_LEN], const int8_t samples[HS_WAVE_BYTES], unsigned counter, unsigned sequence) {
  memset(r, 0, HS_STREAM_LEN);
  r[0] = HS_REPORT_STREAM;
  r[1] = (uint8_t)((sequence & 15) << 4);
  r[2] = 0x91;
  r[3] = 4;
  r[4] = 0x66;
  r[5] = r[6] = 64;
  r[7] = (uint8_t)counter;
  r[8] = 0xD2;
  r[9] = 64;
  memcpy(r + 10, samples, HS_WAVE_BYTES);
  store_checksum(r, HS_STREAM_LEN);
}

void hs_build_speaker(uint8_t r[HS_STREAM_LEN], const uint8_t opus[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES], unsigned counter,
                      unsigned sequence) {
  memset(r, 0, HS_STREAM_LEN);
  r[0] = HS_REPORT_STREAM;
  r[1] = (uint8_t)((sequence & 15) << 4);
  r[2] = 0x91;
  r[3] = 6;
  r[4] = 0x7E;
  memset(r + 5, 48, 4);
  r[9] = (uint8_t)counter;
  r[10] = 0xD3;
  r[11] = HS_SPEAKER_PACKET_BYTES;
  memcpy(r + 12, opus, HS_SPEAKER_PACKETS * HS_SPEAKER_PACKET_BYTES);
  store_checksum(r, HS_STREAM_LEN);
}

static int16_t i16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static hs_touch touch_point(const uint8_t *p) {
  hs_touch t = {(p[0] & 0x80) == 0, (uint16_t)(p[1] | (p[2] & 0x0F) << 8), (uint16_t)(p[2] >> 4 | p[3] << 4)};
  return t;
}

bool hs_parse_input(const uint8_t *report, size_t length, hs_input *out) {
  // "body" starts at the byte after the ID, which the system does not always deliver. Offsets below count from the ID, as
  // the notes do, so each is one less here.
  const uint8_t *body;
  if (length >= HS_INPUT_LEN && report[0] == HS_REPORT_INPUT) body = report + 1;
  else if (length >= HS_INPUT_LEN - 1) body = report;
  else return false;
  memset(out, 0, sizeof *out);
  out->left_x = body[1];
  out->left_y = body[2];
  out->right_x = body[3];
  out->right_y = body[4];
  out->left_trigger = body[5];
  out->right_trigger = body[6];
  uint32_t keys = 0;
  static const uint32_t hat[8] = {HS_KEY_UP, HS_KEY_UP | HS_KEY_RIGHT, HS_KEY_RIGHT, HS_KEY_RIGHT | HS_KEY_DOWN,
                                  HS_KEY_DOWN, HS_KEY_DOWN | HS_KEY_LEFT, HS_KEY_LEFT, HS_KEY_LEFT | HS_KEY_UP};
  if ((body[8] & 0x0F) < 8) keys |= hat[body[8] & 0x0F];
  if (body[8] & 0x10) keys |= HS_KEY_WEST;
  if (body[8] & 0x20) keys |= HS_KEY_SOUTH;
  if (body[8] & 0x40) keys |= HS_KEY_EAST;
  if (body[8] & 0x80) keys |= HS_KEY_NORTH;
  static const uint32_t second[8] = {HS_KEY_L1, HS_KEY_R1, HS_KEY_L2, HS_KEY_R2, HS_KEY_CREATE, HS_KEY_OPTIONS, HS_KEY_L3, HS_KEY_R3};
  for (int bit = 0; bit < 8; bit++) if (body[9] & (1 << bit)) keys |= second[bit];
  if (body[10] & 0x01) keys |= HS_KEY_HOME;
  if (body[10] & 0x02) keys |= HS_KEY_TOUCH;
  if (body[10] & 0x04) keys |= HS_KEY_MUTE;
  out->keys = keys;
  for (int axis = 0; axis < 3; axis++) {
    out->gyro[axis] = i16(body + 16 + 2 * axis);
    out->accel[axis] = i16(body + 22 + 2 * axis);
  }
  out->sensor_clock = (uint32_t)body[28] | (uint32_t)body[29] << 8 | (uint32_t)body[30] << 16 | (uint32_t)body[31] << 24;
  out->touch[0] = touch_point(body + 33);
  out->touch[1] = touch_point(body + 37);
  int level = body[53] & 0x0F, state = body[53] >> 4;
  out->battery_percent = (uint8_t)(level >= 10 ? 100 : level * 10 + 5);
  out->charging = state == 1 || state == 2;
  return true;
}

hs_imu_calibration hs_imu_nominal(void) {
  hs_imu_calibration c = {{0}, {0}, {0}, {0}};
  for (int axis = 0; axis < 3; axis++) {
    c.gyro_scale[axis] = 2000.0f / 32768;
    c.accel_scale[axis] = 1.0f / 8192;
  }
  return c;
}

// The controller's own factory measurements of its sensors, feature report 0x05. For the gyros: each axis at rest, and at a known turning
// rate in both directions. For the accelerometers: each axis at one g, both ways up. An axis that reports nonsense keeps its nominal range.
hs_imu_calibration hs_parse_calibration(const uint8_t *report, size_t length) {
  enum { kGyroRest = 1, kGyroFast = 7, kKnownRate = 19, kAccelOneG = 23 };
  hs_imu_calibration fitted = hs_imu_nominal();
  if (length < 35) return fitted;
  int known_rate = i16(report + kKnownRate) + i16(report + kKnownRate + 2);
  for (int axis = 0; axis < 3; axis++) {
    const uint8_t *fast = report + kGyroFast + axis * 4, *one_g = report + kAccelOneG + axis * 4;
    int rest = i16(report + kGyroRest + axis * 2);
    int swing = i16(fast) - i16(fast + 2);      // the full spread between the rated speed one way and the other
    if (known_rate > 0 && swing > 0) {
      fitted.gyro_zero[axis] = (float)rest;
      fitted.gyro_scale[axis] = (float)known_rate / (float)swing;
    }
    int up = i16(one_g), down = i16(one_g + 2);
    if (up > down) {
      fitted.accel_zero[axis] = (float)(up + down) / 2.0f;
      fitted.accel_scale[axis] = 2.0f / (float)(up - down);
    }
  }
  return fitted;
}
