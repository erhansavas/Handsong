// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The Mac's microphone, for the Breath chapter only. Two numbers come out, a level and a pitch. No sample is stored anywhere.
#include "sound/sound.h"
#include "experience/mic_gate.h"
#import "experience/voice_out.h"
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#include <stdatomic.h>

static AudioQueueRef g_queue;
static _Atomic float g_level, g_pitch;
static hs_mic_gate g_gate;
static _Atomic float g_decibels = -120;
float hs_mic_decibels(void) { return atomic_load(&g_decibels); }
static _Atomic int g_state = -1, g_status = -1;
static bool g_simulated;

static dispatch_queue_t g_starting(void) {
  static dispatch_queue_t queue;
  static dispatch_once_t once;
  dispatch_once(&once, ^{ queue = dispatch_queue_create("handsong.mic", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0)); });
  return queue;
}

static void on_buffer(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer, const AudioTimeStamp *start, UInt32 packets, const AudioStreamPacketDescription *descriptions) {
  (void)user; (void)start; (void)descriptions;
  const float *x = buffer->mAudioData;
  int n = (int)packets;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += x[i] * x[i];
  float rms = n ? sqrtf(sum / (float)n) : 0;
  // Pitch by how the signal repeats, over the range of a voice.
  float best = 0, best_lag = 0;
  for (int lag = 60; lag < 600 && lag < n / 2; lag += 2) {
    float c = 0;
    for (int i = 0; i + lag < n; i += 4) c += x[i] * x[i + lag];
    if (c > best) { best = c; best_lag = (float)lag; }
  }
  float db = 20 * log10f(fmaxf(rms, 1e-6f));
  atomic_store(&g_decibels, db);
  float level = hs_mic_gate_step(&g_gate, db, hs_sound_output_db());
  atomic_store(&g_level, level);
  atomic_store(&g_pitch, level > 0 && best_lag > 0 ? 48000.0f / best_lag : 0);
  AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static void start_queue(void) {
  if (g_queue) return;
  g_gate = (hs_mic_gate){0};       // every opening learns the room afresh
  AudioStreamBasicDescription format = {.mSampleRate = 48000, .mFormatID = kAudioFormatLinearPCM, .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
                                        .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4, .mChannelsPerFrame = 1, .mBitsPerChannel = 32};
  if (AudioQueueNewInput(&format, on_buffer, NULL, NULL, kCFRunLoopCommonModes, 0, &g_queue) != noErr) { g_queue = NULL; return; }
  for (int i = 0; i < 3; i++) {
    AudioQueueBufferRef buffer;
    AudioQueueAllocateBuffer(g_queue, 2048 * 4, &buffer);
    AudioQueueEnqueueBuffer(g_queue, buffer, 0, NULL);
  }
  AudioQueueStart(g_queue, NULL);
}

// The first question to the system loads a framework and costs several milliseconds, so it is asked while loading, not in a frame.
void hs_mic_prepare(void) { atomic_store(&g_status, (int)[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]); }

hs_mic_state hs_mic_get_state(void) {
  int state = atomic_load(&g_state);
  if (state >= 0) return (hs_mic_state)state;
  int known = atomic_load(&g_status);
  AVAuthorizationStatus status = known >= 0 ? (AVAuthorizationStatus)known : [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status == AVAuthorizationStatusAuthorized) {
    atomic_store(&g_state, HS_MIC_GRANTED);
    return HS_MIC_GRANTED;
  }
  if (status == AVAuthorizationStatusNotDetermined) return HS_MIC_NOT_ASKED;
  atomic_store(&g_state, HS_MIC_DENIED);
  return HS_MIC_DENIED;
}

void hs_mic_request(void) {
  if (g_simulated) return;
  atomic_store(&g_state, HS_MIC_ASKING);
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
    atomic_store(&g_state, granted ? HS_MIC_GRANTED : HS_MIC_DENIED);
  }];
}

// Permission is not listening. The microphone is open only while the one chapter that uses it is listening, and closed again the moment it is not.
static bool g_listening;
static void close_queue(void) {
  if (!g_queue) return;
  AudioQueueStop(g_queue, true);
  AudioQueueDispose(g_queue, true);
  g_queue = NULL;
  atomic_store(&g_level, 0);      // after the last buffer has been heard, or it would be read when listening resumes
  atomic_store(&g_pitch, 0);
}

void hs_mic_set_listening(bool on) {
  if (g_simulated || on == g_listening) return;
  g_listening = on;
  dispatch_async(g_starting(), ^{ if (on) start_queue(); else close_queue(); });     // both can take longer than a frame
}

void hs_mic_read(float *level, float *pitch) {
  *level = atomic_load(&g_level);
  *pitch = atomic_load(&g_pitch);
}

void hs_mic_stop(void) {
  hs_mic_set_listening(false);
  if (!g_simulated) { atomic_store(&g_state, -1); atomic_store(&g_status, -1); }
  atomic_store(&g_level, 0);
}

void hs_mic_simulate(hs_mic_state state, float level, float pitch) {
  g_simulated = true;
  atomic_store(&g_state, (int)state);
  atomic_store(&g_level, level);
  atomic_store(&g_pitch, pitch);
}
