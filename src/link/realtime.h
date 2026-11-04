// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 erhansavas
// Tells the system what a thread with a deadline needs: how often it runs, and for how long at most.
#pragma once
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include "link/protocol.h"

static inline void hs_thread_promise(double every_ms, double work_ms) {
  mach_timebase_info_data_t ticks;
  mach_timebase_info(&ticks);
  double per_ms = 1e6 * (double)ticks.denom / (double)ticks.numer;
  thread_time_constraint_policy_data_t promise;
  promise.period = (uint32_t)(every_ms * per_ms);
  promise.computation = (uint32_t)(work_ms * per_ms);
  promise.constraint = promise.period;
  promise.preemptible = 1;
  thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&promise, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
}

// The next time a fixed-rate loop should wake. Falling a long way behind means starting again from now, not running to catch up, which
// on the controller's link would mean a burst of reports.
static inline double hs_next_tick(double planned, double now) {
  double next = planned + HS_STREAM_PERIOD_MS;
  return now - next > 8 * HS_STREAM_PERIOD_MS ? now : next;
}
