// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The controller's reports as plain bytes. No I/O here, so all of it can be tested without a controller.
// docs/PROTOCOL_NOTES.md records where each fact comes from.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  HS_VENDOR = 0x054C,
  HS_PRODUCT_STANDARD = 0x0CE6,
  HS_PRODUCT_EDGE = 0x0DF2,
  HS_REPORT_CONTROL = 0x31,
  HS_REPORT_STREAM = 0x32,
  HS_REPORT_INPUT = 0x31,
  HS_INPUT_LEN = 78,
  HS_CONTROL_LEN = 78,
  HS_STREAM_LEN = 142,
  HS_CALIBRATION_REPORT = 0x05,
  HS_CALIBRATION_LEN = 41,
  HS_WAVE_FRAMES = 64,                // stereo frames in one stream report, 21.33 ms at 3 kHz
  HS_WAVE_RATE = 3000,
  HS_WAVE_BYTES = HS_WAVE_FRAMES * 2,
  HS_SPEAKER_PACKET_BYTES = 63,
  HS_SPEAKER_PACKETS = 2,
  HS_SPEAKER_CLOCK = 45000,
  HS_SPEAKER_PACKET_FRAMES = 480,
  HS_ROUTE_SPEAKER = 0x30,
  HS_SPEAKER_BOOST = 2,
};

#define HS_STREAM_PERIOD_MS (1000.0 * HS_WAVE_FRAMES / HS_WAVE_RATE)

// ---------------------------------------------------------------------------- Trigger effects
typedef struct { uint8_t bytes[11]; } hs_trigger_effect;

hs_trigger_effect hs_trigger_off(void);
hs_trigger_effect hs_trigger_raw(uint8_t force);                       // force from the first millimetre, 255 the ceiling
hs_trigger_effect hs_trigger_spring(int from_zone, int to_zone, int start, int end);

// ---------------------------------------------------------------------------- Control report
typedef struct {
  bool lights_off;       // player lights and microphone light off, microphone muted at volume 0
  bool light_bar_off;    // also the light bar; needs lights_off
  bool set_right, set_left;
  hs_trigger_effect right, left;
  bool lights_show;      // the light bar's colour, the player lights and the microphone's light as given here
  uint8_t bar_red, bar_green, bar_blue, player_lights;   // five bits, one for each player light
  uint8_t mic_light;     // 0 off, 1 on
  bool rumble;
  uint8_t rumble_big, rumble_small;
  bool route;            // set the audio route; a path of 0 turns the speaker off
  uint8_t volume, path, preamp;
} hs_control;

void hs_build_control(uint8_t report[HS_CONTROL_LEN], const hs_control *control, unsigned sequence);

// ---------------------------------------------------------------------------- Stream reports
void hs_build_waveform(uint8_t report[HS_STREAM_LEN], const int8_t samples[HS_WAVE_BYTES], unsigned counter, unsigned sequence);
void hs_build_speaker(uint8_t report[HS_STREAM_LEN], const uint8_t opus[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES], unsigned counter,
                      unsigned sequence);

// ---------------------------------------------------------------------------- Input
enum {
  HS_KEY_WEST = 1u << 0, HS_KEY_SOUTH = 1u << 1, HS_KEY_EAST = 1u << 2, HS_KEY_NORTH = 1u << 3,
  HS_KEY_L1 = 1u << 4, HS_KEY_R1 = 1u << 5, HS_KEY_L2 = 1u << 6, HS_KEY_R2 = 1u << 7,
  HS_KEY_CREATE = 1u << 8, HS_KEY_OPTIONS = 1u << 9, HS_KEY_L3 = 1u << 10, HS_KEY_R3 = 1u << 11,
  HS_KEY_HOME = 1u << 12, HS_KEY_TOUCH = 1u << 13, HS_KEY_MUTE = 1u << 14,
  HS_KEY_UP = 1u << 15, HS_KEY_RIGHT = 1u << 16, HS_KEY_DOWN = 1u << 17, HS_KEY_LEFT = 1u << 18,
};

typedef struct { bool down; uint16_t x, y; } hs_touch;

typedef struct {
  uint8_t left_x, left_y, right_x, right_y, left_trigger, right_trigger;
  uint32_t keys;
  hs_touch touch[2];
  int16_t gyro[3], accel[3];
  uint32_t sensor_clock;     // thirds of a microsecond
  uint8_t battery_percent;   // 0 to 100
  bool charging;
} hs_input;

// Accepts a report with or without its leading ID, as the system may deliver either.
bool hs_parse_input(const uint8_t *report, size_t length, hs_input *out);

typedef struct { float gyro_scale[3], gyro_zero[3], accel_scale[3], accel_zero[3]; } hs_imu_calibration;

hs_imu_calibration hs_imu_nominal(void);
// Falls back to the sensors' nominal ranges for anything the report leaves unbelievable.
hs_imu_calibration hs_parse_calibration(const uint8_t *report, size_t length);
