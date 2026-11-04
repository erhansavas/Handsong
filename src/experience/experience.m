// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "experience/experience.h"
#import "experience/director.h"
#include "experience/lights.h"
#import "experience/stage.h"
#import "experience/voice_out.h"
#import "hands/hands.h"
#import "listen/listen.h"
#import "listen/output_device.h"
#import "listen/speaker_chain.h"
#import "motion/motion.h"
#import "render/renderer.h"
#import "sound/output.h"
#import "sound/stems.h"
#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioWorkInterval.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#import "link/realtime.h"
#include <os/workgroup.h>
#include <stdatomic.h>

// The picture is sized so that the heaviest moment takes this long at full clock on this GPU, leaving room for a warm machine and a low clock.
static const double kFrameTargetMs = 12.5, kSmallestScale = 0.6, kFastFrameMs = 6.5;

@interface HSFullWindow : NSWindow
@property(nonatomic) uint32_t keyboardKeys;
@property(nonatomic) CGFloat dragX, dragY;
@end

@implementation HSFullWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
- (NSRect)constrainFrameRect:(NSRect)frame toScreen:(NSScreen *)screen { (void)screen; return frame; }    // up into the notch

// Without a controller the keyboard can still move through the story, and VoiceOver users can too.
static uint32_t key_for(unsigned short code) {
  switch (code) {
    case 124: return HS_KEY_SOUTH;      // right arrow: on
    case 123: return HS_KEY_EAST;       // left arrow: back
    case 49: case 36: return HS_KEY_SOUTH;     // space and return
    case 53: return HS_KEY_OPTIONS;     // escape
    default: return 0;
  }
}
- (void)keyDown:(NSEvent *)event {
  if (event.isARepeat) return;
  self.keyboardKeys |= key_for(event.keyCode);
}
- (void)keyUp:(NSEvent *)event { self.keyboardKeys &= ~key_for(event.keyCode); }
- (void)mouseDragged:(NSEvent *)event {
  self.dragX = fmax(-1, fmin(1, self.dragX + event.deltaX * 0.02));
  self.dragY = fmax(-1, fmin(1, self.dragY + event.deltaY * 0.02));
}
@end

@interface HSExperience : NSObject <CAMetalDisplayLinkDelegate>
- (bool)inFront;
- (void)tickWithoutPicture;
- (void)stepBack;
- (void)comeBack;
- (void)displaysChanged;
@end

static HSExperience *g_experience;
static hs_experience_script g_script;
enum { kIntervals = 72000 };
static float g_intervals[HS_CH_COUNT][kIntervals];
static long g_interval_count[HS_CH_COUNT], g_skipped[HS_CH_COUNT], g_drawable_size[2];
static double g_main_worst[HS_CH_COUNT], g_script_clock;
static hs_mesh_pack g_pack;
static _Atomic bool g_block_quit, g_let_go;

@implementation HSExperience {
  HSFullWindow *_window;
  CAMetalLayer *_layer;
  CAMetalDisplayLink *_link;
  HSRenderer *_renderer;
  id<MTLDevice> _device;
  pthread_t _blockThread;
  bool _blockRunning, _focusLost, _shown, _modeChanged, _steppedBack, _locked, _asleep, _everConnected;
  NSTimer *_background;
  CGDirectDisplayID _display;
  CGSize _pixels;
  double _renderScale, _heaviestMs;
  double _recentSkips[8];
  int _recentSkipAt;
  id _screenObserver, _sleepObserver, _wakeObserver, _lockObserver, _unlockObserver;
  hs_chapter _first;
  id _activity;
  double _lastTime, _focusLostAt, _openedAt;
  int _spokenChapter;
  long _frames;
  id _resignObserver, _activeObserver;
  CGDisplayModeRef _nativeMode;
  CGFloat _pointsWide, _pointsHigh;
  NSApplicationActivationPolicy _policyBefore;
}

static void *block_thread(void *arg) {
  (void)arg;
  pthread_setname_np("handsong-block");
  // Real time, with a promise of what it needs in each 21.33 ms block, and joined to the system's audio work group.
  hs_thread_promise(HS_STREAM_PERIOD_MS, 4);
  mach_timebase_info_data_t timebase;
  mach_timebase_info(&timebase);
  double ticks_per_ms = 1e6 * timebase.denom / timebase.numer;
  uint64_t period = (uint64_t)(HS_STREAM_PERIOD_MS * ticks_per_ms);
  os_workgroup_t group = AudioWorkIntervalCreate("handsong.block", OS_CLOCK_MACH_ABSOLUTE_TIME, NULL);
  os_workgroup_join_token_s token;
  bool joined = group && os_workgroup_join(group, &token) == 0;
  os_workgroup_interval_t interval = (os_workgroup_interval_t)group;

  static hs_speaker_chain chain;
  bool chain_ok = hs_speaker_init(&chain);
  if (chain_ok) {
    uint8_t silence[HS_SPEAKER_PACKET_BYTES];
    if (hs_speaker_silence_packet(silence)) hs_link_set_speaker_silence(silence);
  }
  static float speaker[HS_SPEAKER_PACKET_FRAMES * 2 * 2];
  bool speaker_on = false;
  uint64_t next = mach_absolute_time();
  while (!atomic_load(&g_block_quit)) {
    next += period;
    bool timed = group && os_workgroup_interval_start(interval, mach_absolute_time(), next, NULL) == 0;
    int8_t waveform[HS_WAVE_BYTES];
    double now = hs_now_ms();
    hs_live_block(now, waveform, NULL, NULL, NULL);
    bool voice = hs_voice_active();
    if (voice != speaker_on) {
      speaker_on = voice;
      if (chain_ok) hs_speaker_set_open(&chain, voice);
      if (voice) hs_link_set_speaker_volume(100);
    }
    // One wish per block. A game in front has already taken the link, and only its leaving gives it back.
    hs_mode wanted = atomic_load_explicit(&g_let_go, memory_order_relaxed) ? HS_MODE_IDLE : voice ? HS_MODE_SPEAKER : hs_link_wide() ? HS_MODE_WAVEFORM : HS_MODE_RUMBLE;
    if (hs_link_mode() != HS_MODE_YIELD && hs_link_mode() != wanted) hs_link_set_mode(wanted);
    if (voice && chain_ok) {
      hs_voice_block(speaker);
      uint8_t packets[HS_SPEAKER_PACKETS][HS_SPEAKER_PACKET_BYTES];
      if (hs_speaker_process(&chain, speaker, packets)) hs_link_push_speaker(packets);
    } else if (hs_link_mode() == HS_MODE_WAVEFORM) {
      hs_link_push_waveform(waveform);
    } else if (hs_link_mode() == HS_MODE_RUMBLE) {
      // The narrow link's fallback: the firmware's own rumble, from how hard each hand is being driven.
      float left, right;
      hs_hands_view(&left, &right);
      hs_link_set_rumble((uint8_t)fminf(255, left * 700), (uint8_t)fminf(255, right * 700));
    }
    if (timed && os_workgroup_interval_finish(interval, NULL) != 0) NSLog(@"the block missed its promise to the system");
    uint64_t now_ticks = mach_absolute_time();
    if (next > now_ticks) mach_wait_until(next);
    else next = now_ticks;       // never run to catch up: a late block is one block
  }
  if (joined) os_workgroup_leave(group, &token);
  if (chain_ok) hs_speaker_destroy(&chain);
  return NULL;
}

// The panel's own pixels, one to one, at twice-the-points where it has such a mode, and at its fastest refresh. Chosen for this process alone,
// so that a crash or a kill returns the desktop to how it was. Whatever the display, the scaled modes macOS uses for the desktop are skipped,
// because they make the panel resample every frame.
static CGDisplayModeRef find_native_mode(CGDirectDisplayID display) {
  NSDictionary *options = @{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes: @YES};
  NSArray *modes = CFBridgingRelease(CGDisplayCopyAllDisplayModes(display, (__bridge CFDictionaryRef)options));
  enum { kNativeFlag = 0x02000000 };
  size_t native_width = 0;
  for (id item in modes) {
    CGDisplayModeRef mode = (__bridge CGDisplayModeRef)item;
    size_t width = CGDisplayModeGetPixelWidth(mode);
    if ((CGDisplayModeGetIOFlags(mode) & kNativeFlag) && width > native_width) native_width = width;
  }
  CGDisplayModeRef best = NULL;
  double best_score = -1;
  for (id item in modes) {
    CGDisplayModeRef mode = (__bridge CGDisplayModeRef)item;
    size_t pixels = CGDisplayModeGetPixelWidth(mode);
    if (!CGDisplayModeIsUsableForDesktopGUI(mode) || (native_width && pixels != native_width)) continue;
    double hidpi = pixels == 2 * CGDisplayModeGetWidth(mode) ? 1e7 : 0;
    double score = hidpi + (double)CGDisplayModeGetPixelHeight(mode) * 1000 + fmin(CGDisplayModeGetRefreshRate(mode), 120);
    if (score > best_score) { best_score = score; best = mode; }
  }
  return best ? CGDisplayModeRetain(best) : NULL;
}

// A display that can reach true black is left alone. Mini-LED panels report a large potential range; backlit LCDs, this one included, do not.
static simd_float3 panel_black_lift(NSScreen *screen) {
  bool local_dimming = screen.maximumPotentialExtendedDynamicRangeColorComponentValue >= 4;
  return local_dimming ? simd_make_float3(0, 0, 0) : simd_make_float3(0.026f, 0.017f, 0.007f);
}

- (instancetype)initWithFirst:(hs_chapter)first {
  if ((self = [super init])) { _first = first; _openedAt = CACurrentMediaTime(); }
  return self;
}

- (void)load {
  // Everything heavy happens away from the main thread, and nothing is shown until the first image is ready.
  NSString *resources = NSBundle.mainBundle.resourcePath;
  NSString *pack_path = [resources stringByAppendingPathComponent:@"instrument.hsmesh"];
  _display = CGMainDisplayID();
  _nativeMode = find_native_mode(_display);
  NSScreen *screen = NSScreen.screens.firstObject;
  _pointsWide = _nativeMode ? (CGFloat)CGDisplayModeGetWidth(_nativeMode) : screen.frame.size.width;
  _pointsHigh = _nativeMode ? (CGFloat)CGDisplayModeGetHeight(_nativeMode) : screen.frame.size.height;
  _pixels = _nativeMode ? CGSizeMake(CGDisplayModeGetPixelWidth(_nativeMode), CGDisplayModeGetPixelHeight(_nativeMode))
                        : CGSizeMake(screen.frame.size.width * screen.backingScaleFactor, screen.frame.size.height * screen.backingScaleFactor);
  simd_float3 lift = panel_black_lift(screen);
  NSString *support = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Handsong/Model/model.hsmesh"];
  if ([NSFileManager.defaultManager fileExistsAtPath:support]) pack_path = support;     // another model following the same rig
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    if (!g_pack.part_count && !hs_mesh_pack_load(pack_path.fileSystemRepresentation, &g_pack)) { NSLog(@"no model"); return; }
    self->_device = MTLCreateSystemDefaultDevice();
    self->_renderer = [[HSRenderer alloc] initWithDevice:self->_device resources:resources meshPack:&g_pack];
    bool sound = hs_stems_table()[0].samples || hs_stems_load([resources stringByAppendingPathComponent:@"score"].fileSystemRepresentation);
    if (!self->_renderer || !sound) { NSLog(@"could not load the experience"); return; }
    for (int i = 0; i < HS_TEXT_COUNT; i++) [self->_renderer prepareText:@(hs_text_line(i)) key:i];
    self->_renderer.blackLift = lift;
    [self->_renderer warmUpAt:self->_pixels];
    [self measureThisMachine];
    hs_mic_prepare();
    dispatch_async(dispatch_get_main_queue(), ^{ [self startShowing]; });
  });
}

// Once for each GPU and display: how large a picture this machine can hold steady. The answer is kept, so later launches lose no time on it.
- (void)measureThisMachine {
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  NSString *key = [NSString stringWithFormat:@"heaviestFrame %@ %.0fx%.0f", _device.name, _pixels.width, _pixels.height];
  const char *pretend = getenv("HANDSONG_TEST_GPU_MS");
  double ms = pretend ? atof(pretend) : [defaults doubleForKey:key];
  if (ms <= 0) {
    ms = [_renderer measureHeaviestAt:_pixels];
    [defaults setDouble:ms forKey:key];
  }
  _heaviestMs = ms;
  _renderScale = ms <= kFrameTargetMs ? 1 : fmax(kSmallestScale, sqrt(kFrameTargetMs / ms));
}

- (CGSize)drawableSize {
  // Even numbers, so that the panel's scaler has nothing to split.
  return CGSizeMake(floor(_pixels.width * _renderScale / 2) * 2, floor(_pixels.height * _renderScale / 2) * 2);
}

- (bool)applyDisplayMode {
  if (!_nativeMode) return false;
  CGDisplayConfigRef config;
  if (CGBeginDisplayConfiguration(&config) != kCGErrorSuccess) return false;
  CGConfigureDisplayWithDisplayMode(config, _display, _nativeMode, NULL);
  return CGCompleteDisplayConfiguration(config, kCGConfigureForAppOnly) == kCGErrorSuccess;
}

// What the display and the window are while the picture is in front.
- (void)takeTheScreen {
  CGDisplayFadeReservationToken fade = kCGDisplayFadeReservationInvalidToken;
  bool will_change = _nativeMode && !_modeChanged;
  if (will_change && CGAcquireDisplayFadeReservation(2, &fade) == kCGErrorSuccess) CGDisplayFade(fade, 0.12f, kCGDisplayBlendNormal, kCGDisplayBlendSolidColor, 0, 0, 0, true);
  if (will_change) _modeChanged = [self applyDisplayMode];
  NSApp.presentationOptions = NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;
  [self fitWindow];
  _window.level = NSMainMenuWindowLevel + 1;
  [NSApp activateIgnoringOtherApps:YES];
  [_window makeKeyAndOrderFront:nil];
  [NSCursor setHiddenUntilMouseMoves:YES];
  if (fade != kCGDisplayFadeReservationInvalidToken) {
    CGDisplayFade(fade, 0.2f, kCGDisplayBlendSolidColor, kCGDisplayBlendNormal, 0, 0, 0, false);
    CGReleaseDisplayFadeReservation(fade);
  }
}

- (void)fitWindow {
  // The screen's own idea of its size catches up a moment later, so the mode's own numbers are used.
  NSRect frame = _modeChanged ? NSMakeRect(0, 0, _pointsWide, _pointsHigh) : NSScreen.screens.firstObject.frame;
  [_window setFrame:frame display:NO];
  _layer.contentsScale = _modeChanged ? _pixels.width / _pointsWide : NSScreen.screens.firstObject.backingScaleFactor;
  _layer.drawableSize = [self drawableSize];
}

// Someone went elsewhere: Command-Tab, Command-H, a click in the Dock, the lock screen, sleep. The desktop comes back exactly as it was, at once,
// the sound goes, and the story waits. The director keeps ticking without pictures, so its pause and the hands' silence still run their course.
- (void)stepBack {
  if (_steppedBack || !_window) return;
  _steppedBack = true;
  _focusLost = true;
  hs_sound_set_master(0, 0.25f);
  _window.keyboardKeys = 0;     // the key's release goes to whatever is in front now
  _link.paused = YES;
  hs_lights_stop();       // whatever is in front now may want them
  _window.level = NSNormalWindowLevel;
  [_window orderOut:nil];
  if (_modeChanged) {
    CGRestorePermanentDisplayConfiguration();
    _modeChanged = false;
  }
  NSApp.presentationOptions = NSApplicationPresentationDefault;
  __weak HSExperience *weak = self;
  _background = [NSTimer scheduledTimerWithTimeInterval:1.0 / 20 repeats:YES block:^(NSTimer *t) { (void)t; [weak tickWithoutPicture]; }];
}

- (void)comeBack {
  if (!_steppedBack || _locked || _asleep || !NSApp.active || !_window) return;
  _steppedBack = false;
  _focusLost = false;
  [_background invalidate];
  _background = nil;
  [self takeTheScreen];
  _lastTime = CACurrentMediaTime();
  _link.paused = NO;
  hs_sound_set_master(1, 0.3f);
}

- (void)tickWithoutPicture {
  double now = CACurrentMediaTime();
  float dt = (float)fmin(now - _lastTime, 0.05);
  _lastTime = now;
  hs_frame_in in = [self inputAt:now dt:dt];
  in.target_ms = in.now_ms;
  hs_scene *scene;
  hs_director_frame(&in, &scene);
  (void)scene;
}

- (hs_frame_in)inputAt:(double)now dt:(float)dt {
  hs_frame_in in = {0};
  hs_link_snapshot(&in.input);
  in.keyboard_keys = _window.keyboardKeys;
  in.input.keys |= _window.keyboardKeys;
  // On a cable there are no reports to read here: it must not pass for a controller with every stick pushed over.
  in.on_cable = hs_link_state() == HS_CONTROLLER_ON_CABLE;
  bool present = hs_link_state() != HS_CONTROLLER_NONE && !in.on_cable;
  _everConnected = _everConnected || present;
  // Nobody ever had a controller here: the story can still be watched and moved through. Losing one that was here is another matter.
  bool keyboard_only = !_everConnected && !g_script;
  hs_director_set_keyboard_only(keyboard_only);
  in.connected = present || (keyboard_only && !in.on_cable);
  if (keyboard_only) {
    in.input.left_x = (uint8_t)(127.5 + 127 * _window.dragX);
    in.input.left_y = (uint8_t)(127.5 + 127 * _window.dragY);
    in.input.right_x = in.input.right_y = 128;
    _window.dragX *= 0.92;
    _window.dragY *= 0.92;
  }
  if (g_script) {
    g_script_clock += dt;
    g_script(&in.input, g_script_clock);
    in.connected = true;
  }
  in.now_ms = hs_now_ms();
  in.dt = dt;
  in.focus_lost = _focusLost;
  // A second and a half after the focus goes, the controller is let go, so that whatever is in front can have it.
  if (_focusLost) {
    if (_focusLostAt == 0) _focusLostAt = now;
    atomic_store(&g_let_go, now - _focusLostAt > 1.5);
  } else {
    _focusLostAt = 0;
    atomic_store(&g_let_go, false);
  }
  return in;
}

- (void)startShowing {
  if (g_experience != self) return;
  _policyBefore = NSApp.activationPolicy;
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  NSRect frame = NSMakeRect(0, 0, _pointsWide, _pointsHigh);
  _window = [[HSFullWindow alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
  _window.backgroundColor = NSColor.blackColor;
  _window.opaque = YES;
  // On every Space: swiping between Spaces must not lose the picture while the app stays in front.
  _window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary;
  _window.acceptsMouseMovedEvents = NO;
  NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
  view.wantsLayer = YES;
  _layer = [CAMetalLayer layer];
  _layer.device = _device;
  _layer.pixelFormat = MTLPixelFormatBGR10A2Unorm;
  CGColorSpaceRef panel = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
  _layer.colorspace = panel;
  CGColorSpaceRelease(panel);
  _layer.opaque = YES;
  _layer.framebufferOnly = YES;
  _layer.maximumDrawableCount = 2;
  view.layer = _layer;
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  _window.contentView = view;

  hs_listen_step_aside(_first != HS_CH_LISTENING);
  hs_motion_start();
  if (getenv("HANDSONG_MOTION_LOG")) hs_motion_log_to(getenv("HANDSONG_MOTION_LOG"));
  if (getenv("HANDSONG_HANDS_LOG")) hs_hands_log_to(getenv("HANDSONG_HANDS_LOG"));
  hs_motion_set_keys_handler(hs_fx_on_input);
  hs_motion_set_report_handler(hs_gears_on_input);
  hs_fx_input_thread_clicks(true);
  hs_mic_get_state();
  hs_director_start(&g_pack, _first, true);
  hs_director_set_calm_motion(NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion);
  hs_sound_set_stems(hs_stems_table(), hs_stems_count());
  long voice_frames = 0;
  const int16_t *voice = hs_stems_voice_45k(&voice_frames);
  hs_voice_load(voice, voice_frames);
  hs_link_set_lights_quiet(true, false);
  hs_lights_set_steady(NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion);
  hs_lights_set_follow_music([NSUserDefaults.standardUserDefaults boolForKey:@"lightFollowsMusic"]);
  hs_link_set_mode(hs_link_wide() ? HS_MODE_WAVEFORM : HS_MODE_RUMBLE);
  if (_first != HS_CH_LISTENING) {
    atomic_store(&g_block_quit, false);
    _blockRunning = true;
    pthread_create(&_blockThread, NULL, block_thread, NULL);
    hs_sound_set_master(1, 0.05f);        // the last time here ended by fading out
    hs_sound_output_start();
  }
  _activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityLatencyCritical | NSActivityUserInitiated | NSActivityIdleDisplaySleepDisabled reason:@"The experience is on screen"];

  __weak HSExperience *weak = self;
  NSNotificationCenter *center = NSNotificationCenter.defaultCenter, *workspace = NSWorkspace.sharedWorkspace.notificationCenter;
  NSDistributedNotificationCenter *system = NSDistributedNotificationCenter.defaultCenter;
  _resignObserver = [center addObserverForName:NSApplicationWillResignActiveNotification object:nil queue:nil usingBlock:^(NSNotification *n) { (void)n; [weak stepBack]; }];
  _activeObserver = [center addObserverForName:NSApplicationDidBecomeActiveNotification object:nil queue:nil usingBlock:^(NSNotification *n) { (void)n; [weak comeBack]; }];
  _screenObserver = [center addObserverForName:NSApplicationDidChangeScreenParametersNotification object:nil queue:nil usingBlock:^(NSNotification *n) { (void)n; [weak displaysChanged]; }];
  void (^away)(NSNotification *) = ^(NSNotification *n) { (void)n; HSExperience *e = weak; if (e) { e->_locked = true; [e stepBack]; } };
  void (^back)(NSNotification *) = ^(NSNotification *n) { (void)n; HSExperience *e = weak; if (e) { e->_locked = false; [e comeBack]; } };
  // Waking is not unlocking: a Mac can wake straight into its lock screen, and the picture and sound wait for that as well.
  _sleepObserver = [workspace addObserverForName:NSWorkspaceWillSleepNotification object:nil queue:nil usingBlock:^(NSNotification *n) { (void)n; HSExperience *e = weak; if (e) { e->_asleep = true; [e stepBack]; } }];
  _wakeObserver = [workspace addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:nil usingBlock:^(NSNotification *n) { (void)n; HSExperience *e = weak; if (e) { e->_asleep = false; [e comeBack]; } }];
  _lockObserver = [system addObserverForName:@"com.apple.screenIsLocked" object:nil queue:nil usingBlock:away];
  _unlockObserver = [system addObserverForName:@"com.apple.screenIsUnlocked" object:nil queue:nil usingBlock:back];

  // One frame in hand before the window appears, so that nothing white or empty is ever seen.
  _lastTime = CACurrentMediaTime();
  _link = [[CAMetalDisplayLink alloc] initWithMetalLayer:_layer];
  _link.preferredFrameLatency = 1;
  // A faster panel runs faster only where this GPU has plenty of room; everything in the story moves by the clock, not by the frame.
  double most = (double)NSScreen.screens.firstObject.maximumFramesPerSecond;
  bool fast = most >= 100 && _heaviestMs > 0 && _heaviestMs <= kFastFrameMs;
  _link.preferredFrameRateRange = fast ? CAFrameRateRangeMake(60, 120, 120) : CAFrameRateRangeMake(60, 60, 60);
  _link.delegate = self;
  [self takeTheScreen];
  [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

// A display plugged in or out, or the main one changed: the picture moves to whichever is main now.
- (void)displaysChanged {
  if (!_window) return;
  CGDirectDisplayID now = CGMainDisplayID();
  if (now != _display) {
    if (_modeChanged) CGRestorePermanentDisplayConfiguration();
    _modeChanged = false;
    _display = now;
    if (_nativeMode) CGDisplayModeRelease(_nativeMode);
    _nativeMode = find_native_mode(now);
    NSScreen *screen = NSScreen.screens.firstObject;
    _pointsWide = _nativeMode ? (CGFloat)CGDisplayModeGetWidth(_nativeMode) : screen.frame.size.width;
    _pointsHigh = _nativeMode ? (CGFloat)CGDisplayModeGetHeight(_nativeMode) : screen.frame.size.height;
    _pixels = _nativeMode ? CGSizeMake(CGDisplayModeGetPixelWidth(_nativeMode), CGDisplayModeGetPixelHeight(_nativeMode))
                          : CGSizeMake(screen.frame.size.width * screen.backingScaleFactor, screen.frame.size.height * screen.backingScaleFactor);
    _renderer.blackLift = panel_black_lift(screen);
    if (!_steppedBack) [self takeTheScreen];
  } else if (!_steppedBack) {
    [self fitWindow];
  }
}

// If frames still go missing in earnest, the picture gets a little smaller rather than the motion any less smooth.
- (void)noteSkippedFrame:(double)now {
  _recentSkips[_recentSkipAt] = now;
  _recentSkipAt = (_recentSkipAt + 1) % 8;
  double oldest = _recentSkips[_recentSkipAt];
  // Only a GPU measured to be near its limit. On one with room, missed frames come from something else, and sharpness would not bring them back.
  if (oldest > 0 && now - oldest < 6 && _renderScale > kSmallestScale && _heaviestMs > kFrameTargetMs * 0.8) {
    _renderScale = fmax(kSmallestScale, _renderScale * 0.9);
    _layer.drawableSize = [self drawableSize];
    memset(_recentSkips, 0, sizeof _recentSkips);
  }
}

- (void)metalDisplayLink:(CAMetalDisplayLink *)link needsUpdate:(CAMetalDisplayLinkUpdate *)update {
  (void)link;
  id<CAMetalDrawable> drawable = update.drawable;
  double now = CACurrentMediaTime();
  if (_frames == 0 && getenv("HANDSONG_FRAME_TRACE")) fprintf(stderr, "from asking to the first image: %.0f ms\n", (now - _openedAt) * 1000);
  double interval = now - _lastTime;
  float dt = (float)fmin(interval, 0.05);
  _lastTime = now;
  int chapter_now = (int)hs_director_chapter();
  if (chapter_now != _spokenChapter) {
    // Someone listening with VoiceOver hears where the story has gone.
    _spokenChapter = chapter_now;
    static NSString *const names[HS_CH_COUNT] = {@"The opening", @"Coils", @"Gears", @"The lattice", @"Balance", @"Breath", @"Voice", @"The finale", @"Free play", @"Full Feel"};
    NSAccessibilityPostNotificationWithUserInfo(NSApp, NSAccessibilityAnnouncementRequestedNotification, @{NSAccessibilityAnnouncementKey: names[chapter_now], NSAccessibilityPriorityKey: @(NSAccessibilityPriorityHigh)});
  }
  // The first quarter second is under the fade from black that hides the display's mode change: nothing in it can be seen, or counted.
  enum { kUnderFade = 15 };
  if (_frames++ > kUnderFade && g_interval_count[chapter_now] < kIntervals) g_intervals[chapter_now][g_interval_count[chapter_now]++] = (float)(interval * 1000);
  if (_frames > kUnderFade + 1 && interval > 1.5 / 60) {
    g_skipped[chapter_now]++;
    [self noteSkippedFrame:now];
    if (getenv("HANDSONG_FRAME_TRACE")) fprintf(stderr, "skip at frame %ld chapter %d interval %.2f ms\n", _frames, chapter_now, interval * 1000);
  }
  g_drawable_size[0] = (long)drawable.texture.width;
  g_drawable_size[1] = (long)drawable.texture.height;
  hs_frame_in in = [self inputAt:now dt:dt];
  in.target_ms = in.now_ms + (update.targetPresentationTimestamp - now) * 1000;
  if (_activity && hs_director_idle_seconds() > 900) {
    [NSProcessInfo.processInfo endActivity:_activity];
    _activity = nil;
  } else if (!_activity && hs_director_idle_seconds() < 1) {
    _activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityLatencyCritical | NSActivityUserInitiated | NSActivityIdleDisplaySleepDisabled reason:@"The experience is on screen"];
  }
  // A warm machine or Low Power Mode gets fewer particles and the same sharpness.
  hs_director_set_modest(NSProcessInfo.processInfo.lowPowerModeEnabled || NSProcessInfo.processInfo.thermalState >= NSProcessInfoThermalStateSerious);
  hs_scene *scene;
  hs_director_frame(&in, &scene);
  hs_lights_frame(scene);
  [_renderer draw:scene into:drawable size:_layer.drawableSize];
  double spent = (CACurrentMediaTime() - now) * 1000;
  if (spent > g_main_worst[chapter_now]) g_main_worst[chapter_now] = spent;
  if (hs_director_wants_to_quit()) dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
  else if (hs_director_wants_to_close()) dispatch_async(dispatch_get_main_queue(), ^{ hs_experience_close(); });
}

- (bool)inFront { return !_steppedBack && _window; }

- (void)stop {
  [_background invalidate];
  _background = nil;
  hs_director_finish();
  [_link invalidate];
  _link = nil;
  if (_blockRunning) {
    usleep(300000);       // the fade asked for above is heard out, instead of the sound being cut
    atomic_store(&g_block_quit, true);
    pthread_join(_blockThread, NULL);
    _blockRunning = false;
    hs_sound_output_stop();
  }
  hs_voice_stop();
  hs_mic_stop();
  [_renderer waitUntilIdle];
  hs_motion_set_keys_handler(NULL);
  hs_motion_set_report_handler(NULL);
  hs_fx_input_thread_clicks(false);
  hs_motion_stop();
  hs_link_set_mode(HS_MODE_IDLE);
  hs_lights_stop();
  hs_link_set_lights_quiet([NSUserDefaults.standardUserDefaults boolForKey:@"calmLights"], false);     // back to what Settings says
  if (_resignObserver) [NSNotificationCenter.defaultCenter removeObserver:_resignObserver];
  if (_activeObserver) [NSNotificationCenter.defaultCenter removeObserver:_activeObserver];
  if (_screenObserver) [NSNotificationCenter.defaultCenter removeObserver:_screenObserver];
  if (_sleepObserver) [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:_sleepObserver];
  if (_wakeObserver) [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:_wakeObserver];
  if (_lockObserver) [NSDistributedNotificationCenter.defaultCenter removeObserver:_lockObserver];
  if (_unlockObserver) [NSDistributedNotificationCenter.defaultCenter removeObserver:_unlockObserver];
  if (_activity) [NSProcessInfo.processInfo endActivity:_activity];
  [_window orderOut:nil];
  _window = nil;
  if (_modeChanged) CGRestorePermanentDisplayConfiguration();
  _modeChanged = false;
  if (_nativeMode) CGDisplayModeRelease(_nativeMode);
  _nativeMode = NULL;
  [NSCursor setHiddenUntilMouseMoves:NO];
  NSApp.presentationOptions = NSApplicationPresentationDefault;
  // In the Dock and the app switcher only while there is a window to go to.
  bool window_left = false;
  for (NSWindow *w in NSApp.windows) window_left = window_left || (w.visible && (w.styleMask & NSWindowStyleMaskTitled));
  [NSApp setActivationPolicy:window_left ? NSApplicationActivationPolicyRegular : _policyBefore];
  hs_listen_step_aside(false);
  hs_world_destroy();
}
@end

void hs_experience_open(hs_chapter first) {
  if (g_experience) {       // it is only waiting behind whatever is in front: asking for it brings it back
    [NSApp activateIgnoringOtherApps:YES];
    return;
  }
  g_experience = [[HSExperience alloc] initWithFirst:first];
  [g_experience load];
}

void hs_experience_close(void) {
  if (!g_experience) return;
  HSExperience *e = g_experience;
  g_experience = nil;
  [e stop];
  hs_motion_start();        // Home's little instrument follows the hand again
}

bool hs_experience_is_open(void) { return g_experience != nil; }
bool hs_experience_in_front(void) { return g_experience && [g_experience inFront]; }
void hs_experience_ask_quit(void) { hs_director_ask_quit(); }
void hs_experience_set_script(hs_experience_script script) { g_script = script; }

static int compare_floats(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? -1 : x > y;
}

hs_frame_report hs_experience_frame_report(int chapter) {
  hs_frame_report r = {.frames = g_interval_count[chapter], .skipped = g_skipped[chapter], .drawable_width = g_drawable_size[0], .drawable_height = g_drawable_size[1], .main_worst_ms = g_main_worst[chapter]};
  if (r.frames == 0) return r;
  float *sorted = malloc(sizeof(float) * (size_t)r.frames);
  memcpy(sorted, g_intervals[chapter], sizeof(float) * (size_t)r.frames);
  qsort(sorted, (size_t)r.frames, sizeof(float), compare_floats);
  r.p50 = sorted[r.frames / 2];
  r.p95 = sorted[(long)((double)r.frames * 0.95)];
  r.p99 = sorted[(long)((double)r.frames * 0.99)];
  r.worst = sorted[r.frames - 1];
  free(sorted);
  return r;
}
