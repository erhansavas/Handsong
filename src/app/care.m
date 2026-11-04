// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "app/care.h"
#import "experience/experience.h"
#import "link/link.h"
#import "listen/listen.h"
#import "listen/output_device.h"
#import "motion/motion.h"
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>

static NSString *const kGyroZero = @"gyroZero", *const kFit = @"accelFit";

void hs_care_at_launch(void) {
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  NSArray *zero = [defaults arrayForKey:kGyroZero];
  if (zero.count == 3) hs_motion_restore_zero(simd_make_float3([zero[0] floatValue], [zero[1] floatValue], [zero[2] floatValue]));
}

void hs_care_at_quit(void) {
  simd_float3 zero = hs_motion_gyro_zero();
  if (simd_length(zero) > 0) [NSUserDefaults.standardUserDefaults setObject:@[@(zero.x), @(zero.y), @(zero.z)] forKey:kGyroZero];
}

bool hs_care_login_item(void) { return SMAppService.mainAppService.status == SMAppServiceStatusEnabled; }

void hs_care_set_login_item(bool on) {
  NSError *error = nil;
  if (on) [SMAppService.mainAppService registerAndReturnError:&error];
  else [SMAppService.mainAppService unregisterAndReturnError:&error];
  if (error) NSLog(@"login item: %@", error);
}

bool hs_care_in_applications(void) {
  NSString *path = NSBundle.mainBundle.bundlePath;
  return [path hasPrefix:@"/Applications/"] || [path hasPrefix:[NSHomeDirectory() stringByAppendingPathComponent:@"Applications/"]];
}

void hs_care_remove_everything(void) {
  if (hs_experience_is_open()) hs_experience_close();
  hs_listen_stop();
  hs_output_select(false);      // hands the Mac's sound back first if it was ours
  hs_output_remove();
  hs_link_stop();
  if (hs_care_login_item()) hs_care_set_login_item(false);
  NSFileManager *files = NSFileManager.defaultManager;
  NSString *support = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Handsong"];
  [files removeItemAtPath:support error:NULL];
  NSString *identifier = NSBundle.mainBundle.bundleIdentifier;
  [NSUserDefaults.standardUserDefaults removePersistentDomainForName:identifier];
  NSArray *leftovers = @[[NSHomeDirectory() stringByAppendingFormat:@"/Library/Preferences/%@.plist", identifier], [NSHomeDirectory() stringByAppendingFormat:@"/Library/Caches/%@", identifier],
                         [NSHomeDirectory() stringByAppendingFormat:@"/Library/Saved Application State/%@.savedState", identifier]];
  for (NSString *path in leftovers) [files removeItemAtPath:path error:NULL];
  // What macOS remembers about permissions belongs to it; asking it to forget is the one honest thing to do.
  NSTask *reset = [NSTask new];
  reset.launchPath = @"/usr/bin/tccutil";
  reset.arguments = @[@"reset", @"All", identifier];
  reset.standardError = NSFileHandle.fileHandleWithNullDevice;
  reset.standardOutput = NSFileHandle.fileHandleWithNullDevice;
  @try { [reset launch]; [reset waitUntilExit]; } @catch (NSException *e) { (void)e; }
  [NSWorkspace.sharedWorkspace recycleURLs:@[NSBundle.mainBundle.bundleURL] completionHandler:^(NSDictionary<NSURL *, NSURL *> *moved, NSError *error) {
    (void)moved; (void)error;
    dispatch_async(dispatch_get_main_queue(), ^{ exit(0); });
  }];
}
