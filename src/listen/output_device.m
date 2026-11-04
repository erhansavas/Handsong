// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "listen/output_device.h"
#import "app/guardian_client.h"
#import "listen/audio_property.h"
#import <Foundation/Foundation.h>

static NSString *const kEntryIdentifier = @"io.github.erhansavas.handsong.output";
static NSString *const kEarlierOutputKey = @"outputToReturnTo";

static NSString *identifier_of(AudioObjectID device) { return CFBridgingRelease(hs_audio_copy_uid(device)); }

static AudioObjectID device_named(NSString *identifier) {
  AudioObjectID found = kAudioObjectUnknown;
  if (!identifier) return found;
  CFStringRef wanted = (__bridge CFStringRef)identifier;
  AudioObjectPropertyAddress where = {kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 bytes = sizeof found;
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &where, sizeof wanted, &wanted, &bytes, &found);
  return found;
}

static UInt32 connection_of(AudioObjectID device) {
  UInt32 kind = 0;
  hs_audio_read(device, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, &kind, sizeof kind);
  return kind;
}

static bool can_play(AudioObjectID device) {
  AudioObjectPropertyAddress where = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
  UInt32 bytes = 0;
  return AudioObjectGetPropertyDataSize(device, &where, 0, NULL, &bytes) == noErr && bytes > 0;
}

// The Mac's own speakers, which everything comes back to.
NSString *hs_output_builtin_uid(void);
NSString *hs_output_builtin_uid(void) {
  AudioObjectPropertyAddress where = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  UInt32 bytes = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &where, 0, NULL, &bytes) != noErr || bytes == 0) return nil;
  NSMutableData *space = [NSMutableData dataWithLength:bytes];
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &where, 0, NULL, &bytes, space.mutableBytes) != noErr) return nil;
  const AudioObjectID *device = space.bytes;
  for (UInt32 left = bytes / sizeof *device; left > 0; left--, device++) {
    if (connection_of(*device) == kAudioDeviceTransportTypeBuiltIn && can_play(*device)) return identifier_of(*device);
  }
  return nil;
}

bool hs_output_ensure(void) {
  if (device_named(kEntryIdentifier) != kAudioObjectUnknown) return true;
  NSString *speakers = hs_output_builtin_uid();
  if (!speakers) return false;
  // Not a driver: an ordinary aggregate of the Mac's own speakers, public so that it can be chosen in the Sound list.
  AudioObjectID made = kAudioObjectUnknown;
  NSDictionary *entry = @{@kAudioAggregateDeviceUIDKey: kEntryIdentifier, @kAudioAggregateDeviceNameKey: @"Handsong Speaker",
                          @kAudioAggregateDeviceSubDeviceListKey: @[@{@kAudioSubDeviceUIDKey: speakers}], @kAudioAggregateDeviceMainSubDeviceKey: speakers,
                          @kAudioAggregateDeviceIsStackedKey: @NO, @kAudioAggregateDeviceIsPrivateKey: @NO};
  return AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)entry, &made) == noErr;
}

bool hs_output_is_selected(void) { return [identifier_of(hs_audio_default_output()) isEqualToString:kEntryIdentifier]; }

static bool make_default(AudioObjectID device) {
  AudioObjectPropertyAddress where = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  return AudioObjectSetPropertyData(kAudioObjectSystemObject, &where, 0, NULL, sizeof device, &device) == noErr;
}

static bool take_over_output(void) {
  if (!hs_output_ensure()) return false;
  if (hs_output_is_selected()) return true;
  // Written down twice: for this process, and for the guardian in case this process dies with the output still ours.
  NSString *earlier = identifier_of(hs_audio_default_output());
  [NSUserDefaults.standardUserDefaults setObject:earlier forKey:kEarlierOutputKey];
  [earlier writeToFile:@(hs_guardian_output_file()) atomically:YES encoding:NSUTF8StringEncoding error:NULL];
  return make_default(device_named(kEntryIdentifier));
}

static bool give_back_output(void) {
  [NSFileManager.defaultManager removeItemAtPath:@(hs_guardian_output_file()) error:NULL];
  if (!hs_output_is_selected()) return true;
  AudioObjectID earlier = device_named([NSUserDefaults.standardUserDefaults stringForKey:kEarlierOutputKey]);
  if (earlier == kAudioObjectUnknown) earlier = device_named(hs_output_builtin_uid());
  return earlier != kAudioObjectUnknown && make_default(earlier);
}

bool hs_output_select(bool selected) { return selected ? take_over_output() : give_back_output(); }

void hs_output_remove(void) {
  give_back_output();
  AudioObjectID entry = device_named(kEntryIdentifier);
  if (entry != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(entry);
}

bool hs_output_is_bluetooth(void) {
  UInt32 kind = connection_of(hs_audio_default_output());
  return kind == kAudioDeviceTransportTypeBluetooth || kind == kAudioDeviceTransportTypeBluetoothLE;
}

double hs_output_latency_ms(void) {
  AudioObjectID device = hs_output_is_selected() ? device_named(hs_output_builtin_uid()) : hs_audio_default_output();
  // What the device itself reports it adds, what its stream adds, and one buffer.
  UInt32 inside = 0, stream_adds = 0, buffer = 0;
  Float64 rate = 48000;
  hs_audio_read(device, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, &inside, sizeof inside);
  hs_audio_read(device, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeOutput, &buffer, sizeof buffer);
  hs_audio_read(device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, &rate, sizeof rate);
  AudioStreamID first_stream = 0;
  if (hs_audio_read(device, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, &first_stream, sizeof first_stream) && first_stream)
    hs_audio_read(first_stream, kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, &stream_adds, sizeof stream_adds);
  return 1000.0 * (double)(inside + stream_adds + buffer) / rate;
}

void hs_output_watch(hs_output_changed changed, void *context) {
  AudioObjectPropertyAddress where = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &where, dispatch_get_main_queue(), ^(UInt32 how_many, const AudioObjectPropertyAddress *which) {
    (void)how_many; (void)which;
    changed(context);
  });
}
