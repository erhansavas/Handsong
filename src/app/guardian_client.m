// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
#import "app/guardian_client.h"
#import <Foundation/Foundation.h>
#include <spawn.h>
#include <unistd.h>

extern char **environ;
static int g_pipe = -1;

const char *hs_guardian_output_file(void) {
  static char path[1024];
  if (!path[0]) {
    NSString *folder = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Handsong"];
    [NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:NULL];
    strlcpy(path, [folder stringByAppendingPathComponent:@"output-to-restore"].fileSystemRepresentation, sizeof path);
  }
  return path;
}

void hs_guardian_start(void) {
  NSString *helper = [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Contents/Helpers/handsong-guardian"];
  if (![NSFileManager.defaultManager isExecutableFileAtPath:helper]) return;
  int ends[2];
  if (pipe(ends)) return;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, ends[0], STDIN_FILENO);
  posix_spawn_file_actions_addclose(&actions, ends[1]);
  char *const arguments[] = {(char *)helper.fileSystemRepresentation, (char *)hs_guardian_output_file(), NULL};
  pid_t child;
  if (posix_spawn(&child, helper.fileSystemRepresentation, &actions, NULL, arguments, environ) == 0) g_pipe = ends[1];
  else close(ends[1]);
  close(ends[0]);
  posix_spawn_file_actions_destroy(&actions);
}

void hs_guardian_goodbye(void) {
  if (g_pipe < 0) return;
  (void)!write(g_pipe, "q", 1);
  close(g_pipe);
  g_pipe = -1;
}
