// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#include "link/link_core.h"
#include <string.h>

enum { STAGE_RUN, STAGE_PREROLL, STAGE_FADE_OUT };

static const double kNarrowStreamMs = 30.0;       // measured: 33.5 reports of 142 bytes per second without Game Mode
static const double kTriggerRepeatMs = 1500;      // the effect is sent again now and then, in case a report was lost
static const double kLightsRepeatMs = 1000, kShowMs = 43;
static const double kRumbleMs = 30;               // about 33 updates a second

void hs_core_init(hs_link_core *core) {
  memset(core, 0, sizeof *core);
  atomic_flag_clear(&core->trigger_lock);
  core->trigger_right = core->trigger_left = hs_trigger_off();
  atomic_store(&core->speaker_volume, 100);
}

void hs_core_connected(hs_link_core *core, double now_ms) {
  core->connected = true;
  core->mode = HS_MODE_IDLE;
  core->stage = STAGE_RUN;
  core->triggers_sent = 0;
  core->rumble_sent = 0;
  core->volume_sent = -1;
  core->last_stream_ms = 0;
  core->next_lights_ms = now_ms;
  core->next_triggers_ms = now_ms;
  atomic_fetch_add(&core->trigger_generation, 1);
}

void hs_core_disconnected(hs_link_core *core) { core->connected = false; }

static uint8_t *next_report(hs_link_core *core) { return core->pool[core->pool_next++ % HS_POOL_REPORTS]; }

static void send_control(hs_link_core *core, const hs_control *control, hs_send_fn send, void *context) {
  uint8_t *report = next_report(core);
  hs_build_control(report, control, core->sequence++);
  if (send(context, HS_REPORT_CONTROL, report, HS_CONTROL_LEN)) core->stats.control_sent++;
  else core->stats.send_failures++;
}

static void note_stream_gap(hs_link_core *core, double now_ms) {
  if (core->last_stream_ms > 0 && now_ms - core->last_stream_ms > core->stats.worst_stream_gap_ms)
    core->stats.worst_stream_gap_ms = now_ms - core->last_stream_ms;
  core->last_stream_ms = now_ms;
}

static void send_waveform(hs_link_core *core, double now_ms, hs_send_fn send, void *context) {
  static const int8_t silence[HS_WAVE_BYTES] = {0};
  unsigned tail = atomic_load_explicit(&core->waveform_tail, memory_order_relaxed);
  unsigned head = atomic_load_explicit(&core->waveform_head, memory_order_acquire);
  const int8_t *samples = silence;
  if (tail != head) {
    samples = core->waveform[tail % HS_RING_BLOCKS];
    atomic_store_explicit(&core->waveform_tail, tail + 1, memory_order_release);
  } else {
    core->stats.waveform_underruns++;
  }
  uint8_t *report = next_report(core);
  hs_build_waveform(report, samples, core->counter, core->sequence++);
  core->counter = (core->counter + 2) & 255;
  if (send(context, HS_REPORT_STREAM, report, HS_STREAM_LEN)) core->stats.stream_sent++;
  else core->stats.send_failures++;
  note_stream_gap(core, now_ms);
}

static void send_speaker(hs_link_core *core, double now_ms, bool silent_only, hs_send_fn send, void *context) {
  uint8_t packets[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES];
  unsigned tail = atomic_load_explicit(&core->speaker_tail, memory_order_relaxed);
  unsigned head = atomic_load_explicit(&core->speaker_head, memory_order_acquire);
  if (!silent_only && tail != head) {
    memcpy(packets, core->speaker[tail % HS_RING_BLOCKS], sizeof packets);
    atomic_store_explicit(&core->speaker_tail, tail + 1, memory_order_release);
  } else {
    if (!silent_only) core->stats.speaker_underruns++;
    memcpy(packets[0], core->speaker_silence, HS_SPEAKER_PACKET_BYTES);
    memcpy(packets[1], core->speaker_silence, HS_SPEAKER_PACKET_BYTES);
  }
  uint8_t *report = next_report(core);
  hs_build_speaker(report, (const uint8_t (*)[HS_SPEAKER_PACKET_BYTES])packets, core->counter, core->sequence++);
  core->counter = (core->counter + 2) & 255;
  if (send(context, HS_REPORT_STREAM, report, HS_STREAM_LEN)) core->stats.stream_sent++;
  else core->stats.send_failures++;
  note_stream_gap(core, now_ms);
}

static void discard_queued(hs_link_core *core) {
  atomic_store(&core->waveform_tail, atomic_load(&core->waveform_head));
  atomic_store(&core->speaker_tail, atomic_load(&core->speaker_head));
}

// Moves between modes without a gap on the link or a thump in the speaker: the amplifier wakes on silence, and it is turned
// down before the stream stops.
static void advance_mode(hs_link_core *core, hs_send_fn send, void *context) {
  hs_mode wanted = (hs_mode)atomic_load(&core->wanted_mode);
  if (core->stage == STAGE_FADE_OUT) {
    if (--core->stage_left > 0) {
      hs_control turn_down = {.route = true, .volume = (uint8_t)(atomic_load(&core->speaker_volume) * core->stage_left / HS_WIND_DOWN_REPORTS),
                              .path = HS_ROUTE_SPEAKER, .preamp = HS_SPEAKER_BOOST};
      send_control(core, &turn_down, send, context);
      return;
    }
    hs_control off = {.route = true};
    send_control(core, &off, send, context);
    core->mode = HS_MODE_IDLE;
    core->stage = STAGE_RUN;
    discard_queued(core);
    return;
  }
  if (core->stage == STAGE_PREROLL) {
    if (wanted != HS_MODE_SPEAKER) { core->stage = STAGE_FADE_OUT; core->stage_left = HS_WIND_DOWN_REPORTS; return; }
    if (--core->stage_left <= 0) core->stage = STAGE_RUN;
    return;
  }
  if (wanted == core->mode) return;
  if (core->mode == HS_MODE_SPEAKER) { core->stage = STAGE_FADE_OUT; core->stage_left = HS_WIND_DOWN_REPORTS; return; }
  if (core->mode == HS_MODE_RUMBLE) {
    hs_control still = {.rumble = true};
    send_control(core, &still, send, context);
    core->rumble_sent = 0;
  }
  if (wanted == HS_MODE_YIELD) {
    hs_control release = {.set_right = true, .set_left = true, .right = hs_trigger_off(), .left = hs_trigger_off()};
    send_control(core, &release, send, context);
    core->triggers_sent = 0;
  }
  discard_queued(core);
  core->mode = wanted;
  if (wanted == HS_MODE_SPEAKER) {
    hs_control on = {.route = true, .volume = (uint8_t)atomic_load(&core->speaker_volume), .path = HS_ROUTE_SPEAKER,
                     .preamp = HS_SPEAKER_BOOST};
    send_control(core, &on, send, context);
    core->volume_sent = atomic_load(&core->speaker_volume);
    core->stage = STAGE_PREROLL;
    core->stage_left = HS_SETTLE_REPORTS;
  }
}

void hs_core_tick(hs_link_core *core, double now_ms, int in_flight, hs_send_fn send, void *context) {
  if (!core->connected) return;
  if (in_flight >= HS_MOST_UNANSWERED) {
    core->stats.skipped_in_flight++;
    return;
  }
  long control_before = core->stats.control_sent;
  advance_mode(core, send, context);
  if (core->mode == HS_MODE_YIELD) return;

  bool wide = atomic_load(&core->link_wide);
  bool stream_due = wide || core->last_stream_ms == 0 || now_ms - core->last_stream_ms >= kNarrowStreamMs;
  if (core->mode == HS_MODE_WAVEFORM) {
    if (stream_due) send_waveform(core, now_ms, send, context);
    else core->stats.dropped_narrow++;
  } else if (core->mode == HS_MODE_SPEAKER) {
    if (stream_due) send_speaker(core, now_ms, core->stage != STAGE_RUN, send, context);
    else core->stats.dropped_narrow++;
  }

  // At most one control report a tick. A stream on a narrow link leaves room only now and then.
  if (core->stats.control_sent != control_before) return;
  if (core->mode == HS_MODE_WAVEFORM && !wide && stream_due) return;
  if (core->mode == HS_MODE_SPEAKER && !wide && stream_due) return;
  bool pending_triggers = core->triggers_sent != atomic_load(&core->trigger_generation);
  if (pending_triggers || now_ms >= core->next_triggers_ms) {
    hs_control c = {.set_right = true, .set_left = true};
    while (atomic_flag_test_and_set(&core->trigger_lock)) {}
    c.right = core->trigger_right;
    c.left = core->trigger_left;
    core->triggers_sent = atomic_load(&core->trigger_generation);
    atomic_flag_clear(&core->trigger_lock);
    send_control(core, &c, send, context);
    core->next_triggers_ms = now_ms + kTriggerRepeatMs;
    return;
  }
  if (core->mode == HS_MODE_SPEAKER && core->stage == STAGE_RUN && core->volume_sent != atomic_load(&core->speaker_volume)) {
    core->volume_sent = atomic_load(&core->speaker_volume);
    hs_control v = {.route = true, .volume = (uint8_t)core->volume_sent, .path = HS_ROUTE_SPEAKER, .preamp = HS_SPEAKER_BOOST};
    send_control(core, &v, send, context);
    return;
  }
  if (core->mode == HS_MODE_RUMBLE && now_ms >= core->next_rumble_ms) {
    uint32_t rumble = atomic_load(&core->rumble);
    if (rumble != core->rumble_sent) {
      core->rumble_sent = rumble;
      hs_control r = {.rumble = true, .rumble_big = (uint8_t)(rumble >> 8), .rumble_small = (uint8_t)rumble};
      send_control(core, &r, send, context);
      core->next_rumble_ms = now_ms + kRumbleMs;
      return;
    }
  }
  // The lights move only on a wide link, and at most this often: the hands' stream always comes first.
  uint64_t show = atomic_load(&core->lights_show);
  if (show != core->lights_show_sent && atomic_load(&core->link_wide) && now_ms >= core->next_show_ms) {
    core->lights_show_sent = show;
    if (show >> 40) {
      hs_control l = {.lights_show = true, .bar_red = (uint8_t)show, .bar_green = (uint8_t)(show >> 8), .bar_blue = (uint8_t)(show >> 16), .player_lights = (uint8_t)(show >> 24), .mic_light = (uint8_t)(show >> 32)};
      send_control(core, &l, send, context);
    } else {
      hs_control l = {.lights_off = true, .light_bar_off = true};      // the show is over: everything out, for whatever comes next to set
      send_control(core, &l, send, context);
    }
    core->next_show_ms = now_ms + kShowMs;
    return;
  }
  if (!(show >> 40) && atomic_load(&core->lights_quiet) && now_ms >= core->next_lights_ms) {
    hs_control l = {.lights_off = true, .light_bar_off = atomic_load(&core->light_bar_quiet)};
    send_control(core, &l, send, context);
    core->next_lights_ms = now_ms + kLightsRepeatMs;
  }
}

void hs_core_release(hs_link_core *core, hs_send_fn send, void *context) {
  if (!core->connected) return;
  hs_control release = {.set_right = true, .set_left = true, .right = hs_trigger_off(), .left = hs_trigger_off()};
  send_control(core, &release, send, context);
  if (core->mode == HS_MODE_SPEAKER) {
    hs_control off = {.route = true};
    send_control(core, &off, send, context);
  }
}

bool hs_core_push_waveform(hs_link_core *core, const int8_t samples[HS_WAVE_BYTES]) {
  unsigned head = atomic_load_explicit(&core->waveform_head, memory_order_relaxed);
  if (head - atomic_load_explicit(&core->waveform_tail, memory_order_acquire) >= HS_RING_BLOCKS) return false;
  memcpy(core->waveform[head % HS_RING_BLOCKS], samples, HS_WAVE_BYTES);
  atomic_store_explicit(&core->waveform_head, head + 1, memory_order_release);
  return true;
}

bool hs_core_push_speaker(hs_link_core *core, const uint8_t opus[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES]) {
  unsigned head = atomic_load_explicit(&core->speaker_head, memory_order_relaxed);
  if (head - atomic_load_explicit(&core->speaker_tail, memory_order_acquire) >= HS_RING_BLOCKS) return false;
  memcpy(core->speaker[head % HS_RING_BLOCKS], opus, HS_SPEAKER_PACKETS * HS_SPEAKER_PACKET_BYTES);
  atomic_store_explicit(&core->speaker_head, head + 1, memory_order_release);
  return true;
}

void hs_core_set_triggers(hs_link_core *core, const hs_trigger_effect *right, const hs_trigger_effect *left) {
  while (atomic_flag_test_and_set(&core->trigger_lock)) {}
  if (right) core->trigger_right = *right;
  if (left) core->trigger_left = *left;
  atomic_flag_clear(&core->trigger_lock);
  atomic_fetch_add(&core->trigger_generation, 1);
}
