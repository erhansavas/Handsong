#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# The seeded fuzzer, 30 minutes of play per seed at full speed, then the scenarios a script can reach.
cd "${0:A:h}/../.."
APP=build/Handsong.app/Contents/MacOS/Handsong
failed=0
for seed in 1 2 3 4 5 6 7 8; do
  $APP --test experience --script fuzz:$seed --seconds 1800 2>&1 | grep -E "fuzz seed|frames|invalid numbers" | head -4 || failed=1
  [[ ${pipestatus[1]} -eq 0 ]] || { print "FAIL seed $seed"; failed=1; }
done
$APP --test scenarios || failed=1
exit $failed
