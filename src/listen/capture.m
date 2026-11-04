// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "listen/capture.h"
#import "link/link.h"
#import "listen/audio_property.h"
#import "listen/output_device.h"
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>
#include <stdatomic.h>

extern NSString *hs_output_builtin_uid(void);

enum { kHistoryFrames = 1 << 17 };      // about 2.7 seconds at 48 kHz; a power of two, so that positions wrap by masking

struct hs_capture {
  float *history;                       // pairs of samples, overwritten as it goes round; nothing is ever kept longer
  _Atomic size_t written, consumed;     // running totals, in frames
  AudioObjectID tap, device;
  AudioDeviceIOProcID callback;
  double rate;
  bool separate_channels, plays_back;
  double late_by;                       // frames behind, moving toward the wish
  _Atomic double late_by_wanted;
  _Atomic double last_sound_ms;
};

// A view of two channels in whichever layout the system hands them over.
typedef struct { const float *left, *right; size_t step, frames; } Channels;

static Channels channels_in(const AudioBufferList *list, bool separate) {
  const AudioBuffer *one = &list->mBuffers[0];
  if (separate && list->mNumberBuffers >= 2) return (Channels){one->mData, list->mBuffers[1].mData, 1, one->mDataByteSize / sizeof(float)};
  size_t width = one->mNumberChannels ? one->mNumberChannels : 1;
  const float *data = one->mData;
  return (Channels){data, width >= 2 ? data + 1 : data, width, one->mDataByteSize / sizeof(float) / width};
}

static void remember(hs_capture *c, Channels in) {
  size_t total = atomic_load_explicit(&c->written, memory_order_relaxed);
  bool heard = false;
  for (size_t n = 0; n < in.frames; n++) {
    float *pair = &c->history[2 * ((total + n) & (kHistoryFrames - 1))];
    pair[0] = in.left[n * in.step];
    pair[1] = in.right[n * in.step];
    heard = heard || pair[0] != 0 || pair[1] != 0;
  }
  if (heard) atomic_store_explicit(&c->last_sound_ms, hs_now_ms(), memory_order_relaxed);
  atomic_store_explicit(&c->written, total + in.frames, memory_order_release);
}

// Plays what was heard, from further back in the history. The delay moves by at most one part in a hundred, far below what an ear follows.
static void play_later(hs_capture *c, AudioBufferList *out) {
  bool separate = out->mNumberBuffers >= 2;
  float *left = out->mBuffers[0].mData, *right = separate ? out->mBuffers[1].mData : left + 1;
  UInt32 width = separate ? 1 : (out->mBuffers[0].mNumberChannels ? out->mBuffers[0].mNumberChannels : 1);
  if (width < 2 && !separate) right = NULL;
  UInt32 frames = out->mBuffers[0].mDataByteSize / sizeof(float) / width;
  double wish = atomic_load_explicit(&c->late_by_wanted, memory_order_relaxed);
  double position = (double)atomic_load_explicit(&c->written, memory_order_relaxed) - (double)frames;
  for (UInt32 n = 0; n < frames; n++, position += 1) {
    c->late_by += fmax(-0.01, fmin(0.01, wish - c->late_by));
    double reading = position - c->late_by;      // a place in the history's running count, between two frames
    float pair[2] = {0, 0};
    if (reading >= 0) {
      size_t before = (size_t)reading;
      float towards_next = (float)(reading - (double)before);
      const float *a = &c->history[2 * (before & (kHistoryFrames - 1))], *b = &c->history[2 * ((before + 1) & (kHistoryFrames - 1))];
      pair[0] = a[0] + (b[0] - a[0]) * towards_next;
      pair[1] = a[1] + (b[1] - a[1]) * towards_next;
    }
    left[n * width] = pair[0];
    if (right) right[n * width] = pair[1];
  }
}

static OSStatus heard_something(AudioObjectID device, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *in_time, AudioBufferList *out, const AudioTimeStamp *out_time, void *context) {
  (void)device; (void)now; (void)in_time; (void)out_time;
  hs_capture *c = context;
  if (in->mNumberBuffers > 0) remember(c, channels_in(in, c->separate_channels));
  if (c->plays_back && out->mNumberBuffers > 0) play_later(c, out);
  return noErr;
}

void hs_capture_stop(hs_capture *c) {
  if (!c) return;
  if (c->callback) {
    AudioDeviceStop(c->device, c->callback);
    AudioDeviceDestroyIOProcID(c->device, c->callback);
  }
  if (c->device) AudioHardwareDestroyAggregateDevice(c->device);
  if (c->tap) AudioHardwareDestroyProcessTap(c->tap);
  free(c->history);
  free(c);
}

// The output whose clock everything follows: the real one, even if the Sound list has this app's own entry selected.
static NSString *output_to_follow(void) {
  if (hs_output_is_selected()) return hs_output_builtin_uid();
  return CFBridgingRelease(hs_audio_copy_uid(hs_audio_default_output()));
}

static AudioObjectID this_process(void) {
  pid_t me = getpid();
  AudioObjectID object = kAudioObjectUnknown;
  AudioObjectPropertyAddress where = {kAudioHardwarePropertyTranslatePIDToProcessObject, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 bytes = sizeof object;
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &where, sizeof me, &me, &bytes, &object);
  return object;
}

hs_capture *hs_capture_start(bool mute_mac, double delay_ms) {
  NSString *output = output_to_follow();
  if (!output) return NULL;
  hs_capture *c = calloc(1, sizeof *c);
  c->history = calloc(kHistoryFrames * 2, sizeof(float));
  c->plays_back = delay_ms > 0;
  // Hearing itself play would be an echo that never ends, so this process is the one thing the tap leaves out.
  AudioObjectID me = this_process();
  CATapDescription *wish = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:me == kAudioObjectUnknown ? @[] : @[@(me)]];
  wish.name = @"Handsong";
  wish.privateTap = YES;
  wish.muteBehavior = mute_mac || c->plays_back ? CATapMutedWhenTapped : CATapUnmuted;
  AudioStreamBasicDescription format = {0};
  bool ready = AudioHardwareCreateProcessTap(wish, &c->tap) == noErr && hs_audio_read(c->tap, kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal, &format, sizeof format);
  if (ready) {
    c->rate = format.mSampleRate;
    c->separate_channels = format.mFormatFlags & kAudioFormatFlagIsNonInterleaved;
    c->late_by = delay_ms * c->rate / 1000;
    atomic_store(&c->late_by_wanted, c->late_by);
    NSDictionary *device = @{@kAudioAggregateDeviceUIDKey: NSUUID.UUID.UUIDString, @kAudioAggregateDeviceNameKey: @"Handsong capture",
                             @kAudioAggregateDeviceIsPrivateKey: @YES, @kAudioAggregateDeviceIsStackedKey: @NO, @kAudioAggregateDeviceTapAutoStartKey: @YES,
                             @kAudioAggregateDeviceMainSubDeviceKey: output, @kAudioAggregateDeviceSubDeviceListKey: @[@{@kAudioSubDeviceUIDKey: output}],
                             @kAudioAggregateDeviceTapListKey: @[@{@kAudioSubTapUIDKey: wish.UUID.UUIDString, @kAudioSubTapDriftCompensationKey: @YES}]};
    ready = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)device, &c->device) == noErr &&
            AudioDeviceCreateIOProcID(c->device, heard_something, c, &c->callback) == noErr &&
            AudioDeviceStart(c->device, c->callback) == noErr;
  }
  if (!ready) {
    hs_capture_stop(c);
    return NULL;
  }
  return c;
}

double hs_capture_rate(const hs_capture *c) { return c->rate; }

size_t hs_capture_available(const hs_capture *c) {
  return atomic_load_explicit(&c->written, memory_order_acquire) - atomic_load_explicit(&c->consumed, memory_order_relaxed);
}

size_t hs_capture_read(hs_capture *c, float *stereo, size_t frames) {
  size_t from = atomic_load_explicit(&c->consumed, memory_order_relaxed), there = atomic_load_explicit(&c->written, memory_order_acquire) - from;
  if (there < frames) frames = there;
  for (size_t n = 0; n < frames; n++) memcpy(&stereo[2 * n], &c->history[2 * ((from + n) & (kHistoryFrames - 1))], 2 * sizeof(float));
  atomic_store_explicit(&c->consumed, from + frames, memory_order_release);
  return frames;
}

void hs_capture_skip_to_newest(hs_capture *c, size_t keep_frames) {
  size_t total = atomic_load_explicit(&c->written, memory_order_acquire);
  atomic_store_explicit(&c->consumed, total > keep_frames ? total - keep_frames : 0, memory_order_release);
}

double hs_capture_last_signal_ms(const hs_capture *c) { return atomic_load_explicit(&c->last_sound_ms, memory_order_relaxed); }
void hs_capture_set_delay(hs_capture *c, double delay_ms) { atomic_store_explicit(&c->late_by_wanted, delay_ms * c->rate / 1000, memory_order_relaxed); }
