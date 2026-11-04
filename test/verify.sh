#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Checks the speaker end to end, with the owner present: plays a 1 kHz tone and analyses it with the Mac's microphone.
# Audio stays in memory. Keep the controller near the Mac. A healthy link shows purity above 80 percent, about 0 phase
# jumps per second, and a single line at 1000 Hz. The tone needs Game Mode's wider link, held by Xcode's tool for this test only.
cd "${0:A:h}/.." || exit 1
seconds="${1:-5}"
result="$(mktemp)"
clang -fobjc-arc -O2 -Wall -o /tmp/handsong_micmeasure test/micmeasure.m -framework Foundation -framework AudioToolbox -framework Accelerate || exit 1
trap 'xcrun gamepolicyctl game-mode set auto >/dev/null 2>&1' EXIT
xcrun gamepolicyctl game-mode set on >/dev/null 2>&1
sleep 1
/tmp/handsong_micmeasure "$seconds" 1000 --skip 1 > "$result" 2>&1 &
mic=$!
sleep 0.1
build/Handsong.app/Contents/MacOS/Handsong --test tone 1000 "$seconds"
rc=$?
wait $mic
grep -E 'purity|carrier|lines' "$result" | cut -c1-140
command rm -f "$result"
exit $rc
