// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The report builders against bytes the controller is known to accept, and the link's state machine with no controller.
#include "link/link_core.c"
#include "link/protocol.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(condition, ...) do { if (!(condition)) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static bool expected(const char *name, uint8_t *out, size_t *length) {
  FILE *f = fopen("test/fixtures/report_bytes.txt", "r");
  assert(f);
  static char line[4096];
  bool found = false;
  while (fgets(line, sizeof line, f)) {
    char id[64], hex[2048];
    size_t n;
    if (line[0] == '#' || sscanf(line, "%63s %zu %2047s", id, &n, hex) != 3 || strcmp(id, name)) continue;
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
    *length = n;
    found = true;
  }
  fclose(f);
  return found;
}

static void compare(const char *name, const uint8_t *report, size_t length) {
  uint8_t want[256];
  size_t n = 0;
  CHECK(expected(name, want, &n), "no fixture %s", name);
  CHECK(n == length, "%s length %zu against %zu", name, length, n);
  for (size_t i = 0; i < length && i < n; i++) CHECK(report[i] == want[i], "%s byte %zu is %02x, wanted %02x", name, i, report[i], want[i]);
}

static void builders(void) {
  uint8_t r[HS_STREAM_LEN];
  hs_control c = {.lights_off = true};
  hs_build_control(r, &c, 0); compare("control_lights_off", r, HS_CONTROL_LEN);
  c.light_bar_off = true;
  hs_build_control(r, &c, 1); compare("control_lights_and_bar_off", r, HS_CONTROL_LEN);
  hs_control t = {.set_right = true, .set_left = true, .right = hs_trigger_raw(255), .left = hs_trigger_raw(255)};
  hs_build_control(r, &t, 2); compare("control_triggers_hardest", r, HS_CONTROL_LEN);
  hs_control o = {.set_right = true, .set_left = true, .right = hs_trigger_off(), .left = hs_trigger_off()};
  hs_build_control(r, &o, 3); compare("control_triggers_release", r, HS_CONTROL_LEN);
  hs_control on = {.route = true, .volume = 100, .path = HS_ROUTE_SPEAKER, .preamp = HS_SPEAKER_BOOST};
  hs_build_control(r, &on, 4); compare("control_route_speaker_100", r, HS_CONTROL_LEN);
  hs_control off = {.route = true};
  hs_build_control(r, &off, 5); compare("control_route_off", r, HS_CONTROL_LEN);
  uint8_t opus[2][63];
  for (int i = 0; i < 63; i++) { opus[0][i] = (uint8_t)i; opus[1][i] = (uint8_t)(200 - i); }
  hs_build_speaker(r, (const uint8_t (*)[63])opus, 4, 6); compare("stream_speaker_counter4", r, HS_STREAM_LEN);
  hs_control rumble = {.rumble = true, .rumble_big = 200, .rumble_small = 100};
  hs_build_control(r, &rumble, 7); compare("control_rumble_big200_small100", r, HS_CONTROL_LEN);
  hs_control right = {.set_right = true, .right = hs_trigger_raw(255)};
  hs_build_control(r, &right, 8); compare("control_trigger_right_raw255", r, HS_CONTROL_LEN);
  int8_t s[128];
  for (int i = 0; i < 128; i++) s[i] = (int8_t)(i - 64);
  hs_build_waveform(r, s, 6, 9); compare("stream_waveform_counter6", r, HS_STREAM_LEN);
}

static void input(void) {
  uint8_t r[78] = {0x31, 7, 10, 20, 30, 40, 50, 60, 9, 0x20 | 0x02, 0x41, 0x05};
  int16_t gyro[3] = {100, -200, 300}, accel[3] = {-8192, 0, 8192};
  memcpy(r + 17, gyro, 6);
  memcpy(r + 23, accel, 6);
  r[29] = 0x78; r[30] = 0x56; r[31] = 0x34; r[32] = 0x12;
  r[34] = 0x05; r[35] = 0xAB; r[36] = 0x5C; r[37] = 0x12;
  r[38] = 0x80;
  r[54] = 0x17;
  hs_input in;
  CHECK(hs_parse_input(r, sizeof r, &in), "parse with id");
  CHECK(in.left_x == 10 && in.right_y == 40 && in.left_trigger == 50 && in.right_trigger == 60, "axes");
  CHECK(in.keys & HS_KEY_RIGHT && in.keys & HS_KEY_SOUTH, "hat and south: %x", in.keys);
  CHECK(in.keys & HS_KEY_L1 && in.keys & HS_KEY_L3 && in.keys & HS_KEY_HOME && in.keys & HS_KEY_MUTE, "keys: %x", in.keys);
  CHECK(in.gyro[1] == -200 && in.accel[2] == 8192 && in.sensor_clock == 0x12345678, "sensors");
  CHECK(in.touch[0].down && in.touch[0].x == 0xCAB && in.touch[0].y == 0x125, "touch %d %x %x", in.touch[0].down, in.touch[0].x, in.touch[0].y);
  CHECK(!in.touch[1].down, "second touch up");
  CHECK(in.battery_percent == 75 && in.charging, "battery %d", in.battery_percent);
  hs_input without;
  CHECK(hs_parse_input(r + 1, 77, &without) && without.left_x == 10 && without.touch[0].x == 0xCAB, "parse without id");
  CHECK(!hs_parse_input(r, 40, &without), "too short");
}

// ---------------------------------------------------------------------------- The state machine
typedef struct { int streams, waveforms, speakers, controls, routes_on, routes_off, rumbles, triggers_off; unsigned last_sequence; bool seen; } Log;

static bool record(void *context, uint8_t id, const uint8_t *r, size_t length) {
  Log *log = context;
  if (log->seen) CHECK(((log->last_sequence + 1) & 15) == (unsigned)(r[1] >> 4), "sequence jumps");
  log->last_sequence = r[1] >> 4;
  log->seen = true;
  if (id == HS_REPORT_STREAM) {
    CHECK(length == HS_STREAM_LEN, "stream length");
    log->streams++;
    if (r[8] == 0xD2) log->waveforms++;
    if (r[10] == 0xD3) log->speakers++;
  } else {
    CHECK(length == HS_CONTROL_LEN, "control length");
    log->controls++;
    if (r[3] & 0x80) { if (r[10] == HS_ROUTE_SPEAKER) log->routes_on++; if (r[10] == 0) log->routes_off++; }
    if (r[3] & 0x02) log->rumbles++;
    if ((r[3] & 0x0C) == 0x0C && r[13] == 0x05 && r[24] == 0x05) log->triggers_off++;
  }
  return true;
}

static void run(hs_link_core *core, Log *log, double *now, int ticks) {
  for (int i = 0; i < ticks; i++) { hs_core_tick(core, *now, 0, record, log); *now += HS_STREAM_PERIOD_MS; }
}

static void state_machine(void) {
  static hs_link_core core;
  hs_core_init(&core);
  Log log = {0};
  double now = 1000;
  atomic_store(&core.link_wide, true);
  hs_core_tick(&core, now, 0, record, &log);
  CHECK(log.controls == 0 && log.streams == 0, "silent without a controller");
  hs_core_connected(&core, now);

  atomic_store(&core.wanted_mode, HS_MODE_WAVEFORM);
  run(&core, &log, &now, 50);
  CHECK(log.waveforms >= 49, "waveform streams every tick: %d", log.waveforms);
  CHECK(core.stats.worst_stream_gap_ms < HS_STREAM_PERIOD_MS * 1.01, "no gap: %f", core.stats.worst_stream_gap_ms);

  // Waveform to speaker and back: the stream never stops, the route goes on once and off once.
  memset(&log, 0, sizeof log);
  atomic_store(&core.silence_ready, true);
  atomic_store(&core.wanted_mode, HS_MODE_SPEAKER);
  run(&core, &log, &now, 40);
  CHECK(log.routes_on == 1, "route on once: %d", log.routes_on);
  CHECK(log.speakers >= 39, "speaker streams every tick: %d", log.speakers);
  atomic_store(&core.wanted_mode, HS_MODE_WAVEFORM);
  memset(&log, 0, sizeof log);
  core.stats.worst_stream_gap_ms = 0;
  run(&core, &log, &now, 40);
  CHECK(log.routes_off == 1, "route off once: %d", log.routes_off);
  CHECK(core.stats.worst_stream_gap_ms < HS_STREAM_PERIOD_MS * 2.5, "handover gap %f", core.stats.worst_stream_gap_ms);

  // Rumble bytes never go out beside the waveform.
  memset(&log, 0, sizeof log);
  atomic_store(&core.rumble, 0x4040);
  run(&core, &log, &now, 40);
  CHECK(log.rumbles == 0, "rumble beside waveform: %d", log.rumbles);
  atomic_store(&core.wanted_mode, HS_MODE_RUMBLE);
  run(&core, &log, &now, 10);
  CHECK(log.rumbles >= 1, "rumble in its mode");

  // A game in front: triggers let go once, then not one report.
  atomic_store(&core.lights_quiet, true);
  atomic_store(&core.wanted_mode, HS_MODE_YIELD);
  memset(&log, 0, sizeof log);
  hs_trigger_effect hard = hs_trigger_raw(255);
  hs_core_set_triggers(&core, &hard, &hard);
  run(&core, &log, &now, 5);
  CHECK(log.triggers_off >= 1, "triggers let go for a game");
  memset(&log, 0, sizeof log);
  run(&core, &log, &now, 100);
  CHECK(log.controls == 0 && log.streams == 0, "yield writes nothing: %d %d", log.controls, log.streams);

  // A narrow link: never more than it carries.
  atomic_store(&core.wanted_mode, HS_MODE_WAVEFORM);
  atomic_store(&core.link_wide, false);
  memset(&log, 0, sizeof log);
  run(&core, &log, &now, 470);
  CHECK(log.streams <= 340, "narrow link carries 33.5 a second, got %d in 10 s", log.streams);

  // A full queue holds reports back instead of adding latency.
  memset(&log, 0, sizeof log);
  long before = core.stats.skipped_in_flight;
  hs_core_tick(&core, now, HS_MOST_UNANSWERED, record, &log);
  CHECK(log.streams == 0 && log.controls == 0 && core.stats.skipped_in_flight == before + 1, "held back at the in-flight limit");

  // Leaving always lets the triggers go.
  memset(&log, 0, sizeof log);
  hs_core_release(&core, record, &log);
  CHECK(log.triggers_off == 1, "release");
}

int main(void) {
  builders();
  input();
  state_machine();
  if (failures) { printf("%d failures\n", failures); return 1; }
  printf("link_test: pass\n");
  return 0;
}
