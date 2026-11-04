// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// The small live instrument in the Home window: the same picture at 24 frames a second, and only while it can be seen.
#import "app/home/bridge.h"
#import "link/link.h"
#import "motion/motion.h"
#import "render/renderer.h"
#include "world/crew.h"
#import <QuartzCore/QuartzCore.h>

@interface HSPreviewView : NSView
@end

@implementation HSPreviewView {
  CAMetalLayer *_layer;
  HSRenderer *_renderer;
  hs_mesh_pack _pack;
  NSTimer *_timer;
  hs_scene _scene;
  uint8_t _hidden[HS_MAX_PART_POSES];
  double _start;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.wantsLayer = YES;
    _layer = [CAMetalLayer layer];
    _layer.pixelFormat = MTLPixelFormatBGR10A2Unorm;
    CGColorSpaceRef panel = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
    _layer.colorspace = panel;
    CGColorSpaceRelease(panel);
    _layer.opaque = YES;
    self.layer = _layer;
    _start = CACurrentMediaTime();
  }
  return self;
}

- (BOOL)wantsUpdateLayer { return YES; }

- (void)prepare {
  if (_renderer) return;
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  NSString *resources = NSBundle.mainBundle.resourcePath;
  NSString *pack_path = [resources stringByAppendingPathComponent:@"instrument.hsmesh"];
  NSString *support = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Handsong/Model/model.hsmesh"];
  if ([NSFileManager.defaultManager fileExistsAtPath:support]) pack_path = support;
  if (!hs_mesh_pack_load(pack_path.fileSystemRepresentation, &_pack)) return;
  _layer.device = device;
  _renderer = [[HSRenderer alloc] initWithDevice:device resources:resources meshPack:&_pack];
  _scene = hs_scene_default();
  for (const char *const *name = (const char *const[]){"lattice", "lattice_nodes", NULL}; *name; name++) {
    int at = hs_mesh_pack_find(&_pack, *name);
    if (at >= 0) _hidden[at] = 1;
  }
  _scene.part_hidden = _hidden;
  _scene.filament = 1.0f;
  _scene.eye = simd_make_float3(0.0f, 0.24f, 0.27f);
  _scene.target = simd_make_float3(0, -0.022f, 0.016f);
  _scene.fov_degrees = 30;
  hs_crew_rest(&_scene, simd_make_float3(0, 0.24f, 0.27f));
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [_timer invalidate];
  _timer = nil;
  if (!self.window) return;
  [self prepare];
  hs_motion_start();
  __weak HSPreviewView *weak = self;
  _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 24 repeats:YES block:^(NSTimer *t) { (void)t; [weak tick]; }];
  [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)tick {
  if (!_renderer || !self.window.isVisible || self.window.occlusionState == 0 || self.bounds.size.width < 10) return;
  if (!(self.window.occlusionState & NSWindowOcclusionStateVisible)) return;
  if (!NSApp.active) return;      // behind whatever you are using, it draws nothing and costs nothing: listening does not depend on this window
  CGFloat scale = self.window.backingScaleFactor;
  CGSize size = CGSizeMake(self.bounds.size.width * scale, self.bounds.size.height * scale);
  _layer.drawableSize = size;
  double t = CACurrentMediaTime() - _start;
  hs_input in;
  hs_link_snapshot(&in);
  simd_quatf pose = simd_quaternion(sinf((float)t * 0.25f) * 0.12f, simd_make_float3(0, 1, 0));
  bool live = hs_link_state() != HS_CONTROLLER_NONE;
  if (live) {
    hs_motion_view view = hs_motion_at(hs_now_ms() + 30, hs_now_ms(), 1.0f / 24);
    pose = simd_mul(simd_quaternion(-hs_motion_hold_pitch(view.pose), simd_make_float3(1, 0, 0)), view.pose);
  }
  _scene.object = simd_matrix4x4(pose);
  _scene.seconds = t;
  if (live) {
    _scene.sticks[0][0] = ((float)in.left_x - 127.5f) / 127.5f;
    _scene.sticks[0][1] = ((float)in.left_y - 127.5f) / 127.5f;
    _scene.sticks[1][0] = ((float)in.right_x - 127.5f) / 127.5f;
    _scene.sticks[1][1] = ((float)in.right_y - 127.5f) / 127.5f;
    _scene.triggers[0] = (float)in.left_trigger / 255;
    _scene.triggers[1] = (float)in.right_trigger / 255;
    _scene.keys[0] = (in.keys & HS_KEY_WEST) ? 1 : 0;
    _scene.keys[1] = (in.keys & HS_KEY_SOUTH) ? 1 : 0;
    _scene.keys[2] = (in.keys & HS_KEY_EAST) ? 1 : 0;
    _scene.keys[3] = (in.keys & HS_KEY_NORTH) ? 1 : 0;
    _scene.cross[0] = ((in.keys & HS_KEY_RIGHT) ? 1.0f : 0.0f) - ((in.keys & HS_KEY_LEFT) ? 1.0f : 0.0f);
    _scene.cross[1] = ((in.keys & HS_KEY_DOWN) ? 1.0f : 0.0f) - ((in.keys & HS_KEY_UP) ? 1.0f : 0.0f);
    _scene.shoulders[0] = (in.keys & HS_KEY_L1) ? 1 : 0;
    _scene.shoulders[1] = (in.keys & HS_KEY_R1) ? 1 : 0;
    _scene.pills[0] = (in.keys & HS_KEY_CREATE) ? 1 : 0;
    _scene.pills[1] = (in.keys & HS_KEY_OPTIONS) ? 1 : 0;
    _scene.stick_press[0] = (in.keys & HS_KEY_L3) ? 1 : 0;
    _scene.stick_press[1] = (in.keys & HS_KEY_R3) ? 1 : 0;
    _scene.dome_press = (in.keys & HS_KEY_TOUCH) ? 1 : 0;
    _scene.home = (in.keys & HS_KEY_HOME) ? 1 : 0;
    _scene.mute = (in.keys & HS_KEY_MUTE) ? 1 : 0;
    _scene.gear_turn[0] = _scene.triggers[0];
    _scene.gear_turn[1] = _scene.triggers[1];
    _scene.bellows[0] = _scene.triggers[0];
    _scene.bellows[1] = _scene.triggers[1];
  }
  id<CAMetalDrawable> drawable = _layer.nextDrawable;
  if (drawable) [_renderer draw:&_scene into:drawable size:size];
}
@end

NSView *hs_home_preview_view(void) { return [[HSPreviewView alloc] initWithFrame:NSMakeRect(0, 0, 640, 600)]; }
