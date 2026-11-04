// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "render/text.h"
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

enum { kEm = 96, kPad = 18, kMaxKeys = 32 };

// The distance to the nearest edge in one pass each way, after Felzenszwalb and Huttenlocher.
static void distance_1d(const float *f, int n, float *d, int *v, float *z) {
  if (n < 2) return;
  int k = 0;
  v[0] = 0;
  z[0] = -1e20f;
  z[1] = 1e20f;
  for (int q = 1; q < n; q++) {
    float s = 0;
    while (k >= 0) {
      int p = v[k];
      s = ((f[q] + (float)(q * q)) - (f[p] + (float)(p * p))) / (float)(2 * q - 2 * p);
      if (s > z[k]) break;
      k--;
    }
    if (k < 0) { k = 0; v[0] = q; z[0] = -1e20f; z[1] = 1e20f; continue; }
    k++;
    v[k] = q;
    z[k] = s;
    z[k + 1] = 1e20f;
  }
  k = 0;
  for (int q = 0; q < n; q++) {
    while (k < n - 1 && z[k + 1] < (float)q) k++;
    d[q] = (float)((q - v[k]) * (q - v[k])) + f[v[k]];
  }
}

static void distance_2d(float *grid, int w, int h) {
  int most = MAX(w, h);
  float *f = calloc((size_t)most, sizeof(float)), *d = calloc((size_t)most, sizeof(float)), *z = calloc((size_t)most + 2, sizeof(float));
  int *v = calloc((size_t)most, sizeof(int));
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) f[y] = grid[y * w + x];
    distance_1d(f, h, d, v, z);
    for (int y = 0; y < h; y++) grid[y * w + x] = d[y];
  }
  for (int y = 0; y < h; y++) {
    distance_1d(grid + y * w, w, d, v, z);
    memcpy(grid + y * w, d, sizeof(float) * (size_t)w);
  }
  free(f);
  free(d);
  free(z);
  free(v);
}

// Pictures of the controls, in the same ink as the words. Nothing here copies any controller's own marks: the four face keys are four plain
// places, with the one that is meant filled in.
static CGFloat icon_width(NSString *name) {
  CGFloat em = kEm * 1.25;
  if ([name isEqualToString:@"touch"]) return em * 1.15;
  if ([name isEqualToString:@"sticks"] || [name isEqualToString:@"triggers"]) return em * 1.2;
  if ([name isEqualToString:@"options"]) return em * 0.55;
  return em * 0.95;
}

static void circle(CGContextRef c, CGFloat x, CGFloat y, CGFloat r, bool filled) {
  CGRect box = CGRectMake(x - r, y - r, r * 2, r * 2);
  if (filled) CGContextFillEllipseInRect(c, box);
  else CGContextStrokeEllipseInRect(c, box);
}

static void draw_stick(CGContextRef c, CGFloat x, CGFloat y, CGFloat em, bool lit) {
  circle(c, x, y, em * 0.22, false);
  if (lit) circle(c, x, y, em * 0.10, true);
}

static void draw_icon(CGContextRef c, NSString *name, CGFloat left, CGFloat baseline) {
  CGFloat em = kEm * 1.25, y = baseline + kEm * 0.30, x = left + icon_width(name) / 2;
  CGContextSetLineWidth(c, em * 0.05);
  NSUInteger key = [@[@"north", @"east", @"south", @"west"] indexOfObject:name];
  if (key != NSNotFound) {
    const CGFloat off = em * 0.30, dx[4] = {0, off, 0, -off}, dy[4] = {off, 0, -off, 0};
    for (NSUInteger i = 0; i < 4; i++) circle(c, x + dx[i], y + dy[i], i == key ? em * 0.19 : em * 0.08, i == key);
  } else if ([name isEqualToString:@"sticks"]) {
    draw_stick(c, x - em * 0.29, y, em, true);
    draw_stick(c, x + em * 0.29, y, em, true);
  } else if ([name isEqualToString:@"left-stick"]) {
    draw_stick(c, x - em * 0.2, y, em, true);
    circle(c, x + em * 0.25, y, em * 0.12, false);
  } else if ([name isEqualToString:@"right-stick"]) {
    circle(c, x - em * 0.25, y, em * 0.12, false);
    draw_stick(c, x + em * 0.2, y, em, true);
  } else if ([name isEqualToString:@"press-stick"]) {
    draw_stick(c, x, y - em * 0.08, em, true);
    CGContextMoveToPoint(c, x - em * 0.13, y + em * 0.40);
    CGContextAddLineToPoint(c, x + em * 0.13, y + em * 0.40);
    CGContextAddLineToPoint(c, x, y + em * 0.22);
    CGContextClosePath(c);
    CGContextFillPath(c);
  } else if ([name isEqualToString:@"triggers"]) {
    for (int side = -1; side <= 1; side += 2) {
      CGRect box = CGRectMake(x + side * em * 0.28 - em * 0.17, y - em * 0.27, em * 0.34, em * 0.54);
      CGPathRef path = CGPathCreateWithRoundedRect(box, em * 0.12, em * 0.12, NULL);
      CGContextAddPath(c, path);
      CGContextFillPath(c);
      CGPathRelease(path);
    }
  } else if ([name isEqualToString:@"touch"]) {
    CGRect box = CGRectMake(x - em * 0.48, y - em * 0.28, em * 0.96, em * 0.56);
    CGPathRef path = CGPathCreateWithRoundedRect(box, em * 0.1, em * 0.1, NULL);
    CGContextAddPath(c, path);
    CGContextStrokePath(c);
    CGPathRelease(path);
    circle(c, x, y, em * 0.09, true);
  } else if ([name isEqualToString:@"options"]) {
    CGRect box = CGRectMake(x - em * 0.09, y - em * 0.26, em * 0.18, em * 0.52);
    CGPathRef path = CGPathCreateWithRoundedRect(box, em * 0.09, em * 0.09, NULL);
    CGContextAddPath(c, path);
    CGContextFillPath(c);
    CGPathRelease(path);
  }
}

@implementation HSTextCache {
  id<MTLDevice> _device;
  id<MTLTexture> _textures[kMaxKeys];
  dispatch_queue_t _queue;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device {
  if ((self = [super init])) {
    _device = device;
    _queue = dispatch_queue_create("handsong.text", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
  }
  return self;
}

- (id<MTLTexture>)textureForKey:(int)key {
  if (key < 0 || key >= kMaxKeys) return nil;
  @synchronized(self) { return _textures[key]; }
}

- (void)waitUntilReady { dispatch_sync(_queue, ^{}); }

- (void)prepare:(NSString *)line key:(int)key {
  if (key < 0 || key >= kMaxKeys) return;
  dispatch_async(_queue, ^{
    NSFontDescriptor *serif = [[NSFont systemFontOfSize:kEm].fontDescriptor fontDescriptorWithDesign:NSFontDescriptorSystemDesignSerif];
    NSFont *font = [NSFont fontWithDescriptor:serif size:kEm] ?: [NSFont systemFontOfSize:kEm];
    NSDictionary *look = @{NSFontAttributeName: font, NSKernAttributeName: @0.6, (__bridge NSString *)kCTForegroundColorFromContextAttributeName: @YES};
    // A line is words and {pictures} of the controls, laid one after another.
    NSMutableArray *pieces = [NSMutableArray array];
    NSScanner *scanner = [NSScanner scannerWithString:line];
    scanner.charactersToBeSkipped = nil;
    while (!scanner.isAtEnd) {
      NSString *words = nil, *icon = nil;
      if ([scanner scanUpToString:@"{" intoString:&words]) [pieces addObject:words];
      if ([scanner scanString:@"{" intoString:NULL] && [scanner scanUpToString:@"}" intoString:&icon]) {
        [scanner scanString:@"}" intoString:NULL];
        [pieces addObject:@[icon]];
      }
    }
    CGFloat ascent = font.ascender, descent = -font.descender, width = 0;
    for (id piece in pieces) {
      if ([piece isKindOfClass:NSArray.class]) { width += icon_width(((NSArray *)piece)[0]); continue; }
      CTLineRef ct = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)[[NSAttributedString alloc] initWithString:piece attributes:look]);
      width += CTLineGetTypographicBounds(ct, NULL, NULL, NULL);
      CFRelease(ct);
    }
    int w = (int)ceil(width) + kPad * 2, h = (int)ceil(ascent + descent) + kPad * 2;
    uint8_t *ink = calloc((size_t)(w * h), 1);
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef context = CGBitmapContextCreate(ink, (size_t)w, (size_t)h, 8, (size_t)w, gray, (CGBitmapInfo)kCGImageAlphaNone);
    CGContextSetGrayFillColor(context, 1, 1);
    CGContextSetGrayStrokeColor(context, 1, 1);
    CGFloat x = kPad, baseline = kPad + descent;
    for (id piece in pieces) {
      if ([piece isKindOfClass:NSArray.class]) {
        NSString *icon = ((NSArray *)piece)[0];
        draw_icon(context, icon, x, baseline);
        x += icon_width(icon);
        continue;
      }
      CTLineRef ct = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)[[NSAttributedString alloc] initWithString:piece attributes:look]);
      CGContextSetTextPosition(context, x, baseline);
      CTLineDraw(ct, context);
      x += CTLineGetTypographicBounds(ct, NULL, NULL, NULL);
      CFRelease(ct);
    }
    CGContextRelease(context);
    CGColorSpaceRelease(gray);
    float *outside = calloc((size_t)(w * h), sizeof(float)), *inside = calloc((size_t)(w * h), sizeof(float));
    for (int i = 0; i < w * h; i++) {
      bool in = ink[i] > 127;
      outside[i] = in ? 0 : 1e10f;
      inside[i] = in ? 1e10f : 0;
    }
    distance_2d(outside, w, h);
    distance_2d(inside, w, h);
    uint8_t *field = malloc((size_t)(w * h));
    for (int i = 0; i < w * h; i++) {
      float d = sqrtf(outside[i]) - sqrtf(inside[i]);
      field[i] = (uint8_t)fmaxf(0, fminf(255, 127.5f - d * 127.5f / (float)kPad));
    }
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [self->_device newTextureWithDescriptor:td];
    [texture replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h) mipmapLevel:0 withBytes:field bytesPerRow:(NSUInteger)w];
    free(ink);
    free(outside);
    free(inside);
    free(field);
    @synchronized(self) { self->_textures[key] = texture; }
  });
}
@end
