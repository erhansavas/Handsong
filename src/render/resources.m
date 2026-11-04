// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "render/resources.h"
#import <AppKit/AppKit.h>

// Materials in linear Display P3, chosen for how they read under the studio's dark-field light. a is metalness.
const hs_material *hs_material_table(void) {
  static const hs_material table[HS_MATERIALS] = {
    [HS_MAT_BRASS_BRUSHED] = {{0.910f, 0.690f, 0.360f, 1}, 0.36f, 0.45f, HS_PATTERN_BRUSHED, 0},
    [HS_MAT_BRASS_POLISHED] = {{0.950f, 0.740f, 0.420f, 1}, 0.14f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_STEEL_BLUE] = {{0.080f, 0.130f, 0.340f, 1}, 0.24f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_STEEL_BRUSHED] = {{0.560f, 0.570f, 0.590f, 1}, 0.26f, 0.55f, HS_PATTERN_BRUSHED, 0},
    [HS_MAT_RUBY] = {{0.420f, 0.012f, 0.040f, 0}, 0.06f, 0, HS_PATTERN_NONE, 0.05f},
    [HS_MAT_CERAMIC] = {{0.500f, 0.480f, 0.445f, 0}, 0.24f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_COPPER] = {{0.880f, 0.440f, 0.260f, 1}, 0.28f, 0.2f, HS_PATTERN_COPPER_WIRE, 0},
    [HS_MAT_BRASS_PERLAGE] = {{0.830f, 0.650f, 0.380f, 1}, 0.36f, 0, HS_PATTERN_PERLAGE, 0},
    [HS_MAT_BRONZE_DARK] = {{0.240f, 0.170f, 0.110f, 1}, 0.42f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_RUBBER] = {{0.030f, 0.028f, 0.028f, 0}, 0.62f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_PAPER] = {{0.380f, 0.330f, 0.250f, 0}, 0.80f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_FILAMENT] = {{1.000f, 0.460f, 0.180f, 0}, 0.40f, 0, HS_PATTERN_NONE, 40.0f},
    [HS_MAT_ACRYLIC] = {{1, 1, 1, 0}, 0.05f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_LENS] = {{0.014f, 0.016f, 0.022f, 0}, 0.035f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_KNURLED] = {{0.950f, 0.740f, 0.420f, 1}, 0.18f, 0, HS_PATTERN_KNURL, 0},
    [HS_MAT_WATER] = {{0.006f, 0.016f, 0.020f, 0}, 0.03f, 0, HS_PATTERN_WATER, 0},
    [HS_MAT_WOOD] = {{0.360f, 0.190f, 0.085f, 0}, 0.46f, 0, HS_PATTERN_WOOD, 0},
    [HS_MAT_GRAVEL] = {{0.150f, 0.135f, 0.120f, 0}, 0.62f, 0, HS_PATTERN_GRAVEL, 0},
    [HS_MAT_LATTICE] = {{0.300f, 0.150f, 0.090f, 1}, 0.46f, 0, HS_PATTERN_LATTICE, 0},
    [HS_MAT_LATTICE_NODE] = {{0.420f, 0.280f, 0.170f, 1}, 0.34f, 0, HS_PATTERN_LATTICE, 1.0f},
    [HS_MAT_PINS] = {{0.920f, 0.710f, 0.390f, 1}, 0.24f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_STEEL_SATIN] = {{0.620f, 0.625f, 0.640f, 1}, 0.42f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_STEEL_POLISHED] = {{0.700f, 0.705f, 0.720f, 1}, 0.07f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_MATTE_DARK] = {{0.016f, 0.016f, 0.019f, 1}, 0.50f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_GLOSS_DARK] = {{0.014f, 0.014f, 0.016f, 1}, 0.16f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_SMOKED_KEY] = {{0.024f, 0.025f, 0.030f, 1}, 0.24f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_SLATE] = {{0.030f, 0.038f, 0.052f, 1}, 0.34f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_PRINT] = {{0.620f, 0.620f, 0.640f, 0}, 0.48f, 0, HS_PATTERN_NONE, 0},
    [HS_MAT_COPPER_PLAIN] = {{0.880f, 0.440f, 0.260f, 1}, 0.20f, 0, HS_PATTERN_NONE, 0},
  };
  return table;
}

@implementation HSResources {
  simd_float4 _sh[9];
}

- (const simd_float4 *)irradiance { return _sh; }

- (id<MTLTexture>)privateCopy:(id<MTLTexture>)staging queue:(id<MTLCommandQueue>)queue device:(id<MTLDevice>)device {
  MTLTextureDescriptor *d = [MTLTextureDescriptor new];
  d.textureType = staging.textureType;
  d.pixelFormat = staging.pixelFormat;
  d.width = staging.width;
  d.height = staging.height;
  d.depth = staging.depth;
  d.mipmapLevelCount = staging.mipmapLevelCount;
  d.storageMode = MTLStorageModePrivate;
  d.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> out = [device newTextureWithDescriptor:d];
  id<MTLCommandBuffer> c = [queue commandBuffer];
  id<MTLBlitCommandEncoder> b = [c blitCommandEncoder];
  [b copyFromTexture:staging toTexture:out];
  [b endEncoding];
  [c commit];
  [c waitUntilCompleted];
  return out;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device queue:(id<MTLCommandQueue>)queue folder:(NSString *)folder {
  if (!(self = [super init])) return nil;
  // The light: "ENV2", size, mips, nine harmonics in three floats each, then half floats in RGBA, largest mip first.
  NSData *env = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:@"studio.hsenv"] options:NSDataReadingMappedIfSafe error:NULL];
  if (env.length < 120 || memcmp(env.bytes, "ENV2", 4) != 0) { NSLog(@"no studio light"); return nil; }
  const uint8_t *p = env.bytes;
  uint32_t size, mips;
  memcpy(&size, p + 4, 4);
  memcpy(&mips, p + 8, 4);
  float sh[27];
  memcpy(sh, p + 12, sizeof sh);
  for (int i = 0; i < 9; i++) _sh[i] = simd_make_float4(sh[3 * i], sh[3 * i + 1], sh[3 * i + 2], 0);
  MTLTextureDescriptor *d = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float size:size mipmapped:YES];
  d.mipmapLevelCount = mips;
  d.storageMode = MTLStorageModeShared;
  id<MTLTexture> staging = [device newTextureWithDescriptor:d];
  size_t at = 120;
  for (uint32_t mip = 0; mip < mips; mip++) {
    NSUInteger n = MAX(size >> mip, 1u);
    for (NSUInteger face = 0; face < 6; face++) {
      if (at + n * n * 8 > env.length) return nil;
      [staging replaceRegion:MTLRegionMake2D(0, 0, n, n) mipmapLevel:mip slice:face withBytes:p + at bytesPerRow:n * 8 bytesPerImage:0];
      at += n * n * 8;
    }
  }
  _environment = [self privateCopy:staging queue:queue device:device];

  // The colour table: "LUT1", size, then half floats in RGBA.
  NSData *table = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:@"grade.hslut"] options:NSDataReadingMappedIfSafe error:NULL];
  if (table.length < 8 || memcmp(table.bytes, "LUT1", 4) != 0) { NSLog(@"no colour table"); return nil; }
  uint32_t n;
  memcpy(&n, (const uint8_t *)table.bytes + 4, 4);
  MTLTextureDescriptor *t = [MTLTextureDescriptor new];
  t.textureType = MTLTextureType3D;
  t.pixelFormat = MTLPixelFormatRGBA16Float;
  t.width = t.height = t.depth = n;
  t.storageMode = MTLStorageModeShared;
  id<MTLTexture> lut = [device newTextureWithDescriptor:t];
  [lut replaceRegion:MTLRegionMake3D(0, 0, 0, n, n, n) mipmapLevel:0 slice:0 withBytes:(const uint8_t *)table.bytes + 8 bytesPerRow:n * 8 bytesPerImage:n * n * 8];
  _grade = [self privateCopy:lut queue:queue device:device];

  // Blue noise: "BNS1", size, then one byte per texel.
  NSData *noise = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:@"blue_noise.hsnoise"] options:NSDataReadingMappedIfSafe error:NULL];
  if (noise.length < 8 || memcmp(noise.bytes, "BNS1", 4) != 0) { NSLog(@"no noise"); return nil; }
  uint32_t m;
  memcpy(&m, (const uint8_t *)noise.bytes + 4, 4);
  MTLTextureDescriptor *nd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:m height:m mipmapped:NO];
  nd.storageMode = MTLStorageModeShared;
  id<MTLTexture> blue = [device newTextureWithDescriptor:nd];
  [blue replaceRegion:MTLRegionMake2D(0, 0, m, m) mipmapLevel:0 withBytes:(const uint8_t *)noise.bytes + 8 bytesPerRow:m];
  _blueNoise = [self privateCopy:blue queue:queue device:device];
  return self;
}
@end

void hs_save_png(id<MTLTexture> texture, NSString *path) {
  NSUInteger w = texture.width, h = texture.height;
  NSMutableData *raw = [NSMutableData dataWithLength:w * h * 4];
  [texture getBytes:raw.mutableBytes bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
  if ([path hasSuffix:@".raw10"]) {     // the finished 10-bit values, for the banding and dither gates
    [raw writeToFile:path atomically:NO];
    return;
  }
  NSMutableData *rgb = [NSMutableData dataWithLength:w * h * 8];
  const uint32_t *in = raw.bytes;
  uint16_t *out = rgb.mutableBytes;
  for (NSUInteger i = 0; i < w * h; i++) {     // BGR10A2 in little-endian order: blue in the low bits
    uint32_t v = in[i];
    uint32_t b = v & 1023, g = (v >> 10) & 1023, r = (v >> 20) & 1023;
    out[4 * i] = (uint16_t)(r * 65535 / 1023);
    out[4 * i + 1] = (uint16_t)(g * 65535 / 1023);
    out[4 * i + 2] = (uint16_t)(b * 65535 / 1023);
    out[4 * i + 3] = 65535;
  }
  unsigned char *planes[1] = {rgb.mutableBytes};
  NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:(NSInteger)w pixelsHigh:(NSInteger)h bitsPerSample:16 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:(NSInteger)w * 8 bitsPerPixel:64];
  [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:NO];
}
