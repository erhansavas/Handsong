// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Reading one property of a Core Audio object, which is nearly all this app ever asks of it.
#pragma once
#import <CoreAudio/CoreAudio.h>
#include <stdbool.h>

static inline bool hs_audio_read(AudioObjectID object, AudioObjectPropertySelector what, AudioObjectPropertyScope scope, void *into, UInt32 bytes) {
  AudioObjectPropertyAddress where = {what, scope, kAudioObjectPropertyElementMain};
  return AudioObjectGetPropertyData(object, &where, 0, NULL, &bytes, into) == noErr;
}

static inline AudioObjectID hs_audio_default_output(void) {
  AudioObjectID device = kAudioObjectUnknown;
  hs_audio_read(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, &device, sizeof device);
  return device;
}

static inline CFStringRef hs_audio_copy_uid(AudioObjectID device) {
  CFStringRef identifier = NULL;
  hs_audio_read(device, kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, &identifier, sizeof identifier);
  return identifier;
}
