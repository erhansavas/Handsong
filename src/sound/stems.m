// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "sound/stems.h"
#import "experience/stage.h"
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

static hs_stem g_stems[HS_STEM_COUNT];
static int16_t *g_voice;
static long g_voice_frames;

static int16_t *decode(NSString *path, double rate, int channels, long *frames_out) {
  ExtAudioFileRef file = NULL;
  if (ExtAudioFileOpenURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], &file) != noErr) return NULL;
  AudioStreamBasicDescription file_format = {0}, client = {0};
  UInt32 size = sizeof file_format;
  ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &file_format);
  client.mSampleRate = rate;
  client.mFormatID = kAudioFormatLinearPCM;
  client.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  client.mBitsPerChannel = 16;
  client.mChannelsPerFrame = (UInt32)channels;
  client.mFramesPerPacket = 1;
  client.mBytesPerFrame = client.mBytesPerPacket = 2 * (UInt32)channels;
  ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof client, &client);
  SInt64 file_frames = 0;
  size = sizeof file_frames;
  ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &size, &file_frames);
  long capacity = (long)((double)file_frames * rate / file_format.mSampleRate) + 4096;
  int16_t *samples = malloc((size_t)capacity * (size_t)channels * 2);
  long at = 0;
  while (at < capacity) {
    AudioBufferList list = {.mNumberBuffers = 1};
    list.mBuffers[0].mNumberChannels = (UInt32)channels;
    list.mBuffers[0].mData = samples + at * channels;
    list.mBuffers[0].mDataByteSize = (UInt32)((capacity - at) * channels * 2);
    UInt32 frames = (UInt32)(capacity - at);
    if (ExtAudioFileRead(file, &frames, &list) != noErr || frames == 0) break;
    at += frames;
  }
  ExtAudioFileDispose(file);
  *frames_out = at;
  return samples;
}

bool hs_stems_load(const char *folder) {
  static NSString *const names[HS_STEM_COUNT] = {@"open_rise", @"open_open", @"coils_bed", @"coils_top", @"gears_bed", @"gears_motion", @"lattice_bed", @"lattice_top",
                                                 @"balance_bed", @"balance_motion", @"breath_bed", @"finale", @"workshop_bed", @"stop_up", @"stop_down", @"bridge", @"voice"};
  NSString *base = @(folder);
  for (int i = 0; i < HS_STEM_COUNT; i++) {
    long frames = 0;
    g_stems[i].samples = decode([[base stringByAppendingPathComponent:names[i]] stringByAppendingPathExtension:@"caf"], HS_SOUND_RATE, 2, &frames);
    g_stems[i].frames = frames;
    if (!g_stems[i].samples) { NSLog(@"no score stem %@", names[i]); return false; }
  }
  g_voice = decode([base stringByAppendingPathComponent:@"voice.caf"], 45000, 1, &g_voice_frames);
  return g_voice != NULL;
}

const hs_stem *hs_stems_table(void) { return g_stems; }
int hs_stems_count(void) { return HS_STEM_COUNT; }

const int16_t *hs_stems_voice_45k(long *frames) {
  *frames = g_voice_frames;
  return g_voice;
}

