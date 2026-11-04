// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The one writer on the controller's link, as a state machine with no I/O of its own. Everything that wants something from
// the controller asks here. Each tick sends at most one stream report and one control report, the most the link carries.
#pragma once
#include "link/protocol.h"
#include <stdatomic.h>

typedef enum {
  HS_MODE_IDLE,       // control reports only
  HS_MODE_WAVEFORM,   // the body actuators, as a stream
  HS_MODE_SPEAKER,    // the controller's speaker, as a stream
  HS_MODE_RUMBLE,     // the firmware's two rumble bytes, which fit a narrow link
  HS_MODE_YIELD,      // a game is in front: not one report leaves here
} hs_mode;

enum { HS_RING_BLOCKS = 16, HS_POOL_REPORTS = 16, HS_MOST_UNANSWERED = 4, HS_SETTLE_REPORTS = 12, HS_WIND_DOWN_REPORTS = 8 };

// Returns false when the system refused the report. The report memory stays valid until HS_POOL_REPORTS reports later.
typedef bool (*hs_send_fn)(void *context, uint8_t report_id, const uint8_t *report, size_t length);

typedef struct {
  long stream_sent, control_sent, send_failures;
  long skipped_in_flight;     // the link was behind, so a report was held back
  long waveform_underruns;    // nothing was queued in waveform mode; silence went out, so the stream never stopped
  long speaker_underruns;
  long dropped_narrow;        // the link cannot carry a stream at full rate right now
  double worst_stream_gap_ms;
} hs_link_stats;

typedef struct {
  // Producers, each from one thread only.
  int8_t waveform[HS_RING_BLOCKS][HS_WAVE_BYTES];
  _Atomic unsigned waveform_head, waveform_tail;
  uint8_t speaker[HS_RING_BLOCKS][HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES];
  _Atomic unsigned speaker_head, speaker_tail;
  uint8_t speaker_silence[HS_SPEAKER_PACKET_BYTES];   // a packet of true silence from the same encoder, sent whenever nothing is queued
  _Atomic bool silence_ready;

  // Wishes, latest wins.
  _Atomic int wanted_mode;
  _Atomic uint64_t lights_show;    // bar red, green, blue, player lights, microphone light, and a top byte that is 1 while a show runs
  _Atomic bool lights_quiet;       // keep player lights, microphone and its light off
  _Atomic bool light_bar_quiet;
  _Atomic int speaker_volume;      // 0 to 100
  _Atomic uint32_t rumble;         // big << 8 | small
  _Atomic uint64_t trigger_generation;
  hs_trigger_effect trigger_right, trigger_left;   // written under trigger_lock, read at a tick
  atomic_flag trigger_lock;
  _Atomic bool link_wide;          // Game Mode has widened the link

  // Owned by the tick.
  hs_mode mode;
  int stage, stage_left;
  hs_mode target;
  unsigned sequence, counter;
  uint64_t lights_show_sent;
  double next_show_ms;
  double last_stream_ms, next_lights_ms, next_triggers_ms, next_rumble_ms;
  uint64_t triggers_sent;
  uint32_t rumble_sent;
  int volume_sent;
  uint8_t pool[HS_POOL_REPORTS][HS_STREAM_LEN];
  unsigned pool_next;
  bool connected;
  hs_link_stats stats;
} hs_link_core;

void hs_core_init(hs_link_core *core);
void hs_core_connected(hs_link_core *core, double now_ms);      // a controller arrived: forget what was sent to the last one
void hs_core_disconnected(hs_link_core *core);

// One tick, every 21.33 ms from one thread. in_flight is how many reports the system has not yet completed.
void hs_core_tick(hs_link_core *core, double now_ms, int in_flight, hs_send_fn send, void *context);

// The last act before leaving: trigger force off, speaker off.
void hs_core_release(hs_link_core *core, hs_send_fn send, void *context);

// Producers.
bool hs_core_push_waveform(hs_link_core *core, const int8_t samples[HS_WAVE_BYTES]);
bool hs_core_push_speaker(hs_link_core *core, const uint8_t opus[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES]);
void hs_core_set_triggers(hs_link_core *core, const hs_trigger_effect *right, const hs_trigger_effect *left);
