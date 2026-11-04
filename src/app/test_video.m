// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Writes what the renderer draws into a movie, for showing the app to people who do not have it yet. Never part of the app's own path.
// The picture comes in at twice the movie's size in Display P3, and goes out in the standard video colours most players assume.
#import "app/test_video.h"
#import <AVFoundation/AVFoundation.h>
#include <math.h>

static AVAssetWriter *g_writer;
static AVAssetWriterInput *g_input;
static AVAssetWriterInputPixelBufferAdaptor *g_adaptor;
static long g_frames;
static int g_width, g_height;
static float g_to_linear[1024];
static float g_to_video[4097];
static uint32_t g_random = 20261103;

static float srgb_to_linear(float x) { return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f); }
static float linear_to_srgb(float x) { return x <= 0.0031308f ? x * 12.92f : 1.055f * powf(x, 1 / 2.4f) - 0.055f; }

bool hs_video_open(const char *path, int width, int height) {
  g_width = width;
  g_height = height;
  for (int i = 0; i < 1024; i++) g_to_linear[i] = srgb_to_linear((float)i / 1023);
  for (int i = 0; i <= 4096; i++) g_to_video[i] = linear_to_srgb((float)i / 4096) * 255;
  NSURL *url = [NSURL fileURLWithPath:@(path)];
  [NSFileManager.defaultManager removeItemAtURL:url error:NULL];
  NSError *error = nil;
  g_writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeQuickTimeMovie error:&error];
  if (!g_writer) return false;
  NSDictionary *settings = @{AVVideoCodecKey: AVVideoCodecTypeH264, AVVideoWidthKey: @(width), AVVideoHeightKey: @(height),
                             AVVideoCompressionPropertiesKey: @{AVVideoAverageBitRateKey: @60000000, AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel},
                             AVVideoColorPropertiesKey: @{AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2, AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2, AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2}};
  g_input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
  g_input.expectsMediaDataInRealTime = NO;
  g_adaptor = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:g_input sourcePixelBufferAttributes:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA), (id)kCVPixelBufferWidthKey: @(width), (id)kCVPixelBufferHeightKey: @(height)}];
  [g_writer addInput:g_input];
  if (![g_writer startWriting]) return false;
  [g_writer startSessionAtSourceTime:kCMTimeZero];
  return true;
}

static inline float video_value(float linear, float noise) {
  float at = fmaxf(0, fminf(1, linear)) * 4096;
  return fmaxf(0, fminf(255, g_to_video[(int)at] + noise));
}

void hs_video_add(id<MTLTexture> texture) {
  static uint32_t *raw;
  if (!raw) raw = malloc((size_t)g_width * 2 * (size_t)g_height * 2 * 4);
  NSUInteger w = (NSUInteger)g_width * 2, h = (NSUInteger)g_height * 2;
  [texture getBytes:raw bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
  while (!g_input.readyForMoreMediaData) [NSThread sleepForTimeInterval:0.001];
  CVPixelBufferRef buffer = NULL;
  CVPixelBufferPoolCreatePixelBuffer(NULL, g_adaptor.pixelBufferPool, &buffer);
  CVPixelBufferLockBaseAddress(buffer, 0);
  uint8_t *out = CVPixelBufferGetBaseAddress(buffer);
  size_t stride = CVPixelBufferGetBytesPerRow(buffer);
  dispatch_apply((size_t)g_height, DISPATCH_APPLY_AUTO, ^(size_t y) {
    uint32_t random = g_random ^ (uint32_t)(y * 2654435761u) ^ (uint32_t)(g_frames * 40503u);
    const uint32_t *a = raw + y * 2 * w, *b = a + w;
    uint8_t *row = out + y * stride;
    for (int x = 0; x < g_width; x++) {
      float r = 0, g = 0, bl = 0;
      uint32_t four[4] = {a[2 * x], a[2 * x + 1], b[2 * x], b[2 * x + 1]};
      for (int i = 0; i < 4; i++) {
        bl += g_to_linear[four[i] & 1023];
        g += g_to_linear[(four[i] >> 10) & 1023];
        r += g_to_linear[(four[i] >> 20) & 1023];
      }
      r *= 0.25f; g *= 0.25f; bl *= 0.25f;
      // Display P3 to the video standard's primaries.
      float vr = 1.2249f * r - 0.2247f * g, vg = -0.0420f * r + 1.0419f * g, vb = -0.0197f * r - 0.0786f * g + 1.0979f * bl;
      random = random * 1664525u + 1013904223u;
      float n1 = (float)(random >> 24) / 255;
      random = random * 1664525u + 1013904223u;
      float noise = n1 + (float)(random >> 24) / 255 - 1;    // fine and even, so 8 bits do not band the dark gradients
      row[4 * x] = (uint8_t)(video_value(vb, noise) + 0.5f);
      row[4 * x + 1] = (uint8_t)(video_value(vg, noise) + 0.5f);
      row[4 * x + 2] = (uint8_t)(video_value(vr, noise) + 0.5f);
      row[4 * x + 3] = 255;
    }
  });
  CVPixelBufferUnlockBaseAddress(buffer, 0);
  [g_adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake(g_frames++, 60)];
  CVPixelBufferRelease(buffer);
}

void hs_video_close(void) {
  [g_input markAsFinished];
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  [g_writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  printf("video: %ld frames, %s\n", g_frames, g_writer.error ? g_writer.error.localizedDescription.UTF8String : "written");
}
