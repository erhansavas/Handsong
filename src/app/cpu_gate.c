// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// What an Intel Mac runs instead of the app, which is built for Apple silicon alone: it says so plainly and leaves.
#include <CoreFoundation/CoreFoundation.h>
#include <stdlib.h>

int main(void) {
  if (getenv("HANDSONG_NO_ALERT")) return 0;      // a test that this slice runs at all, with nobody there to answer
  CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL, CFSTR("Handsong needs Apple silicon"),
                                 CFSTR("This app is built for Macs with Apple silicon, and it cannot run on this Intel Mac."),
                                 CFSTR("Quit"), NULL, NULL, NULL);
  return 0;
}
