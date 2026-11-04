// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "sound/output.h"
#import "sound/sound.h"
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#include <stdatomic.h>

static AudioUnit g_unit;
static float g_scratch[HS_SOUND_BLOCK * 8 * 2];

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *stamp, UInt32 bus, UInt32 frames, AudioBufferList *out) {
  (void)ref; (void)bus; (void)stamp; (void)flags;
  UInt32 left = frames, at = 0;
  float *a = out->mBuffers[0].mData, *b = out->mNumberBuffers > 1 ? out->mBuffers[1].mData : NULL;
  while (left > 0) {
    UInt32 n = left > HS_SOUND_BLOCK * 8 ? HS_SOUND_BLOCK * 8 : left;
    hs_sound_render(g_scratch, (int)n);
    for (UInt32 i = 0; i < n; i++) {
      a[at + i] = g_scratch[i * 2];
      if (b) b[at + i] = g_scratch[i * 2 + 1];
    }
    at += n;
    left -= n;
  }
  return noErr;
}

static int device_buffer_frames(void) {
  UInt32 size = sizeof(AudioDeviceID), buffer = HS_SOUND_BLOCK;
  AudioDeviceID device = 0;
  AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device) != noErr) return (int)buffer;
  address = (AudioObjectPropertyAddress){kAudioDevicePropertyBufferFrameSize, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
  size = sizeof buffer;
  AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &buffer);
  return (int)buffer;
}

static int device_latency_frames(void) {
  UInt32 size = sizeof(AudioDeviceID);
  AudioDeviceID device = 0;
  AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device) != noErr) return 1024;
  UInt32 latency = 0, safety = 0, stream_latency = 0, buffer = 256;
  size = sizeof latency;
  address = (AudioObjectPropertyAddress){kAudioDevicePropertyLatency, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
  AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &latency);
  address.mSelector = kAudioDevicePropertySafetyOffset;
  size = sizeof safety;
  AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &safety);
  address.mSelector = kAudioDevicePropertyBufferFrameSize;
  size = sizeof buffer;
  AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &buffer);
  AudioStreamID streams[4];
  size = sizeof streams;
  address.mSelector = kAudioDevicePropertyStreams;
  if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, streams) == noErr && size >= sizeof(AudioStreamID)) {
    address = (AudioObjectPropertyAddress){kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    size = sizeof stream_latency;
    AudioObjectGetPropertyData(streams[0], &address, 0, NULL, &size, &stream_latency);
  }
  return (int)(latency + safety + stream_latency + buffer);
}

static OSStatus on_default_changed(AudioObjectID object, UInt32 count, const AudioObjectPropertyAddress *addresses, void *context) {
  (void)object; (void)count; (void)addresses; (void)context;
  // The default output unit follows by itself, starting from silence; only the delay that lines sound up with the hands has changed.
  dispatch_async(dispatch_get_main_queue(), ^{ hs_sound_output_device_changed(); });
  return noErr;
}

static const AudioObjectPropertyAddress kDefaultOutput = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

bool hs_sound_output_start(void) {
  if (g_unit) return true;
  AudioObjectAddPropertyListener(kAudioObjectSystemObject, &kDefaultOutput, on_default_changed, NULL);
  AudioComponentDescription description = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0};
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (!component || AudioComponentInstanceNew(component, &g_unit) != noErr) { g_unit = NULL; return false; }
  AudioStreamBasicDescription format = {.mSampleRate = HS_SOUND_RATE, .mFormatID = kAudioFormatLinearPCM,
                                        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved,
                                        .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4, .mChannelsPerFrame = 2, .mBitsPerChannel = 32};
  AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof format);
  UInt32 frames = HS_SOUND_BLOCK;
  AudioUnitSetProperty(g_unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames, sizeof frames);
  AURenderCallbackStruct callback = {render, NULL};
  AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback);
  if (AudioUnitInitialize(g_unit) != noErr || AudioOutputUnitStart(g_unit) != noErr) {
    AudioComponentInstanceDispose(g_unit);
    g_unit = NULL;
    return false;
  }
  hs_sound_set_room_latency(device_latency_frames());
  return true;
}

void hs_sound_output_stop(void) {
  if (!g_unit) return;
  AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &kDefaultOutput, on_default_changed, NULL);
  AudioOutputUnitStop(g_unit);
  AudioUnitUninitialize(g_unit);
  AudioComponentInstanceDispose(g_unit);
  g_unit = NULL;
}

void hs_sound_output_device_changed(void) {
  if (!g_unit) return;
  hs_sound_set_room_latency(device_latency_frames());
}

double hs_sound_output_latency_ms(void) {
  // The buffer the system would give this app, not whatever it runs at now for someone else.
  return (double)(device_latency_frames() - device_buffer_frames() + HS_SOUND_BLOCK) / HS_SOUND_RATE * 1000;
}
