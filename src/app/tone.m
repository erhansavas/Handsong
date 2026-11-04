// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// handsong --test tone [hertz] [seconds] [gain]: a tone on the controller's speaker, for test/verify.sh with the owner present.
#import "app/test_modes.h"
#import "link/link.h"
#import "listen/speaker_chain.h"
#import <Foundation/Foundation.h>
#include <math.h>

int hs_test_tone(double hertz, double seconds, double gain) {
  hs_link_start();
  double start = hs_now_ms();
  while (hs_link_state() != HS_CONTROLLER_READY && hs_now_ms() - start < 3000) usleep(20000);
  if (hs_link_state() != HS_CONTROLLER_READY) { printf("no controller connected\n"); hs_link_stop(); return 2; }
  static hs_speaker_chain chain;
  if (!hs_speaker_init(&chain)) return 70;
  uint8_t silence[HS_SPEAKER_PACKET_BYTES];
  hs_speaker_silence_packet(silence);
  hs_link_set_speaker_silence(silence);
  hs_speaker_set_open(&chain, true);
  hs_link_set_mode(HS_MODE_SPEAKER);
  usleep(400000);
  double phase = 0, next = hs_now_ms();
  long blocks = (long)(seconds * 1000 / HS_STREAM_PERIOD_MS);
  static float block[HS_FEEL_FRAMES * 2];
  uint8_t packets[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES];
  for (long b = 0; b < blocks; b++) {
    for (int i = 0; i < HS_FEEL_FRAMES; i++) {
      float v = (float)(sin(phase) * gain);
      phase = fmod(phase + 2 * M_PI * hertz / HS_SPEAKER_CLOCK, 2 * M_PI);
      block[2 * i] = block[2 * i + 1] = v;
    }
    if (b == blocks - 4) hs_speaker_set_open(&chain, false);
    hs_speaker_process(&chain, block, packets);
    while (!hs_link_push_speaker((const uint8_t (*)[HS_SPEAKER_PACKET_BYTES])packets)) usleep(1000);
    next += HS_STREAM_PERIOD_MS;
    double wait = next - hs_now_ms();
    if (wait > 0) usleep((useconds_t)(wait * 1000));
  }
  hs_link_set_mode(HS_MODE_IDLE);
  usleep(400000);
  hs_link_stats stats = hs_link_get_stats();
  printf("sent %ld, underruns %ld, held back %ld, narrow %ld, worst gap %.1f ms\n", stats.stream_sent, stats.speaker_underruns, stats.skipped_in_flight, stats.dropped_narrow, stats.worst_stream_gap_ms);
  hs_speaker_destroy(&chain);
  hs_link_stop();
  return 0;
}
