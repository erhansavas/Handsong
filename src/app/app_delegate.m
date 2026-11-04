// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "app/app_delegate.h"
#import "app/care.h"
#import "app/guardian_client.h"
#import "app/home/bridge.h"
#import "experience/director.h"
#import "experience/experience.h"
#import "link/link.h"
#import "listen/listen.h"
#import "listen/output_device.h"
#import "motion/motion.h"

static NSString *const kFeelKey = @"feelTheMusic";
static NSString *const kSpeakerKey = @"playOnController";

// True for apps the system lists as games, or whose bundle says so. They speak to the controller themselves.
static bool is_game(NSRunningApplication *app) {
  NSArray *games = CFBridgingRelease(CFPreferencesCopyAppValue(CFSTR("games"), CFSTR("com.apple.GameController")));
  if ([games isKindOfClass:NSDictionary.class]) games = ((NSDictionary *)games)[@"data"];
  if ([games isKindOfClass:NSArray.class]) {
    for (NSDictionary *game in games) {
      if (![game isKindOfClass:NSDictionary.class]) continue;
      NSString *bundle = game[@"bundleIdentifier"];
      if (![bundle isEqualToString:@"com.apple.default"] && [bundle isEqualToString:app.bundleIdentifier]) return true;
    }
  }
  NSDictionary *info = app.bundleURL ? [NSBundle bundleWithURL:app.bundleURL].infoDictionary : nil;
  NSString *category = info[@"LSApplicationCategoryType"];
  return ([category isKindOfClass:NSString.class] && [category containsString:@"games"]) || [info[@"GCSupportsControllerUserInteraction"] boolValue];
}

// Apps where a picture has to match the sound. Browsers are here because so much video plays in them.
static bool shows_video(NSRunningApplication *app) {
  static NSSet *players;
  if (!players) players = [NSSet setWithArray:@[@"com.apple.QuickTimePlayerX", @"com.apple.TV", @"org.videolan.vlc", @"com.colliderli.iina", @"io.mpv",
                                                @"com.apple.Safari", @"com.google.Chrome", @"org.mozilla.firefox", @"company.thebrowser.Browser", @"com.microsoft.edgemac", @"com.apple.FaceTime", @"us.zoom.xos"]];
  return app.bundleIdentifier && [players containsObject:app.bundleIdentifier];
}

// The real menu: what makes Command-Q, Command-H and Command-W work anywhere in the app, whether the bar is on screen or not.
void hs_app_install_menu(void) {
  NSMenu *bar = [NSMenu new];
  NSMenu *app = [NSMenu new];
  [app addItemWithTitle:@"About Handsong" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
  [app addItem:NSMenuItem.separatorItem];
  [app addItemWithTitle:@"Hide Handsong" action:@selector(hide:) keyEquivalent:@"h"];
  [app addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"].keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  [app addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
  [app addItem:NSMenuItem.separatorItem];
  [app addItemWithTitle:@"Quit Handsong" action:@selector(terminate:) keyEquivalent:@"q"];
  NSMenuItem *app_item = [bar addItemWithTitle:@"Handsong" action:nil keyEquivalent:@""];
  app_item.submenu = app;
  NSMenu *window = [[NSMenu alloc] initWithTitle:@"Window"];
  [window addItemWithTitle:@"Close" action:@selector(hsCloseFront:) keyEquivalent:@"w"];
  [window addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
  NSMenuItem *window_item = [bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""];
  window_item.submenu = window;
  NSApp.mainMenu = bar;
  NSApp.windowsMenu = window;
}

// True when the system itself is asking: log out, restart, shut down. Nothing may hold those up.
static bool system_is_quitting_us(void) {
  NSAppleEventDescriptor *event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
  return event && [event attributeDescriptorForKeyword:'why?'] != nil;
}

static void on_output_changed(void *context) {
  (void)context;
  hs_listen_output_changed();
}

@implementation AppDelegate {
  NSStatusItem *_item;
  NSMenuItem *_statusLine, *_feelItem, *_speakerItem;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
  (void)note;
  // One process owns the controller. A second launch hands over to the first and leaves.
  NSArray *running = [NSRunningApplication runningApplicationsWithBundleIdentifier:NSBundle.mainBundle.bundleIdentifier];
  for (NSRunningApplication *other in running) {
    if (other.processIdentifier == NSProcessInfo.processInfo.processIdentifier) continue;
    [other activateWithOptions:0];
    [NSApp terminate:nil];
    return;
  }
  hs_app_install_menu();
  hs_care_at_launch();
  hs_guardian_start();
  hs_link_start();
  hs_output_ensure();
  hs_listen_start();
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  [defaults registerDefaults:@{kFeelKey: @NO, kSpeakerKey: @NO}];
  if ([defaults boolForKey:kFeelKey] && [defaults boolForKey:kSpeakerKey]) [defaults setBool:NO forKey:kSpeakerKey];   // from before they were one choice
  hs_listen_set_feel([defaults boolForKey:kFeelKey]);
  hs_listen_set_speaker([defaults boolForKey:kSpeakerKey]);
  hs_home_apply_calm_lights();
  hs_output_watch(on_output_changed, NULL);
  [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(frontAppChanged) name:NSWorkspaceDidActivateApplicationNotification object:nil];
  [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(macWoke) name:NSWorkspaceDidWakeNotification object:nil];
  [self frontAppChanged];
  [self buildMenu];
  [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(refresh) userInfo:nil repeats:YES];
  [self refresh];
  // A person who has just dragged it in sees the app at once; afterwards it lives quietly in the menu bar until it is asked for.
  bool opened_by_login = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent && [NSAppleEventManager.sharedAppleEventManager.currentAppleEvent paramDescriptorForKeyword:keyAEPropData].enumCodeValue == keyAELaunchedAsLogInItem;
  if (!opened_by_login) hs_home_show();
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag {
  (void)sender; (void)flag;
  if (!hs_experience_is_open()) hs_home_show();    // with the picture open, coming to the front is already the answer
  return NO;
}

// Command-Q over the picture is asked on the picture. From anywhere else (the menu bar, the Dock, the app switcher) it just quits.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  (void)sender;
  if (hs_experience_in_front() && NSApp.active && !hs_director_wants_to_quit() && !system_is_quitting_us()) {
    hs_experience_ask_quit();
    return NSTerminateCancel;
  }
  return NSTerminateNow;
}

- (void)hsCloseFront:(id)sender {
  (void)sender;
  if (hs_experience_in_front()) hs_experience_close();
  else [NSApp.keyWindow performClose:nil];
}

- (void)buildMenu {
  _item = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
  NSImage *mark = [[NSImage alloc] initWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"menubar" ofType:@"png"]];
  mark.size = NSMakeSize(18, 18);
  mark.template = YES;
  mark.accessibilityDescription = @"Handsong";
  _item.button.image = mark;
  NSMenu *menu = [NSMenu new];
  menu.autoenablesItems = NO;
  _statusLine = [menu addItemWithTitle:@"" action:nil keyEquivalent:@""];
  _statusLine.enabled = NO;
  [menu addItem:NSMenuItem.separatorItem];
  _feelItem = [menu addItemWithTitle:@"Feel It in Your Hands" action:@selector(toggleFeel) keyEquivalent:@""];
  _feelItem.target = self;
  _speakerItem = [menu addItemWithTitle:@"Play on the Controller (Experimental)" action:@selector(toggleSpeaker) keyEquivalent:@""];
  _speakerItem.target = self;
  [menu addItem:NSMenuItem.separatorItem];
  [menu addItemWithTitle:@"Open Full Feel" action:@selector(openListening) keyEquivalent:@""].target = self;
  [menu addItemWithTitle:@"Start the Tour" action:@selector(openExperience) keyEquivalent:@""].target = self;
  [menu addItemWithTitle:@"Open Handsong" action:@selector(openHome) keyEquivalent:@""].target = self;
  [menu addItem:NSMenuItem.separatorItem];
  [menu addItemWithTitle:@"Quit Handsong" action:@selector(terminate:) keyEquivalent:@"q"];
  _item.menu = menu;
}

- (void)toggleFeel {
  hs_home_set_feel(![NSUserDefaults.standardUserDefaults boolForKey:kFeelKey]);
  [self refresh];
}

- (void)toggleSpeaker {
  hs_home_set_speaker(![NSUserDefaults.standardUserDefaults boolForKey:kSpeakerKey]);
  [self refresh];
}

- (void)openHome { hs_home_show(); }
- (void)openListening { hs_home_open_listening_room(); }
- (void)openExperience {
  if (hs_home_flash_warning_needed()) { hs_home_show(); return; }
  hs_experience_open(HS_CH_ASSEMBLY);
}

- (void)frontAppChanged {
  NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
  bool game = front && is_game(front);
  if (hs_experience_is_open()) {     // the experience keeps its own hold, but never against a game
    hs_listen_step_aside(game || hs_director_chapter() != HS_CH_LISTENING);
    if (game) hs_link_set_mode(HS_MODE_YIELD);
    else if (hs_link_mode() == HS_MODE_YIELD) hs_link_set_mode(HS_MODE_IDLE);
    return;
  }
  hs_listen_step_aside(game);
  hs_link_set_mode(game ? HS_MODE_YIELD : hs_link_mode() == HS_MODE_YIELD ? HS_MODE_IDLE : hs_link_mode());
  hs_listen_set_video_in_front(front && shows_video(front));
}

- (void)macWoke {
  hs_listen_output_changed();
}

- (void)refresh {
  hs_input input;
  hs_link_snapshot(&input);
  switch (hs_link_state()) {
    case HS_CONTROLLER_NONE: _statusLine.title = @"Turn on your controller"; break;
    case HS_CONTROLLER_ON_CABLE: _statusLine.title = @"Unplug the cable to use Bluetooth"; break;
    case HS_CONTROLLER_UNTESTED: _statusLine.title = @"This controller model is untested"; break;
    case HS_CONTROLLER_READY:
      _statusLine.title = [NSString stringWithFormat:@"Controller connected, %d%%%@", input.battery_percent, input.charging ? @", charging" : @""];
      break;
  }
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  _feelItem.state = [defaults boolForKey:kFeelKey] ? NSControlStateValueOn : NSControlStateValueOff;
  _speakerItem.state = [defaults boolForKey:kSpeakerKey] ? NSControlStateValueOn : NSControlStateValueOff;
}

// Only for the owner's hand session, which asks for it. What happened on the link and in the sensors, never anything heard.
- (void)writeSessionLog {
  const char *path = getenv("HANDSONG_SESSION_LOG");
  if (!path) return;
  FILE *f = fopen(path, "w");
  if (!f) return;
  hs_link_stats link = hs_link_get_stats();
  fprintf(f, "link: stream reports %ld, control reports %ld, send failures %ld, held back %ld, waveform underruns %ld, speaker underruns %ld, narrow drops %ld, worst stream gap %.1f ms, input rate %.0f per second\n",
          link.stream_sent, link.control_sent, link.send_failures, link.skipped_in_flight, link.waveform_underruns, link.speaker_underruns, link.dropped_narrow, link.worst_stream_gap_ms, hs_link_input_rate());
  fprintf(f, "motion: reports %ld, gravity error %.3f degrees\n", hs_motion_reports(), hs_motion_gravity_error());
  for (int c = 0; c < HS_CH_COUNT; c++) {
    hs_frame_report r = hs_experience_frame_report(c);
    if (r.frames) fprintf(f, "chapter %d: frames %ld, skipped %ld, interval p50 %.2f p99 %.2f worst %.2f ms, drawable %ldx%ld\n", c, r.frames, r.skipped, r.p50, r.p99, r.worst, r.drawable_width, r.drawable_height);
  }
  fclose(f);
}

- (void)applicationWillTerminate:(NSNotification *)note {
  (void)note;
  [self writeSessionLog];
  if (hs_experience_is_open()) hs_experience_close();
  hs_care_at_quit();
  hs_listen_stop();
  hs_output_remove();
  hs_link_stop();
  hs_guardian_goodbye();
}
@end
