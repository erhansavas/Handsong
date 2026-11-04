#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Sound and hands over a whole scripted tour, written to files and measured. Nothing is heard.
cd "${0:A:h}/../.."
work=$(mktemp -d)
APP=build/Handsong.app/Contents/MacOS/Handsong
HANDSONG_SOUND_EVENTS=$work/e.txt $APP --test experience --script tour --seconds 300 --sound $work/s.f32 --hands $work/h.s8 --marks $work/m.txt | tail -1
test/gates/uvpy test/gates/tour_report.py $work/s.f32 $work/h.s8 $work/m.txt
r1=$?
test/gates/uvpy test/gates/mix_checks.py $work/s.f32 $work/m.txt $work/e.txt
r2=$?
$APP --test press-latency
r3=$?
$APP --test trigger-prediction
r4=$?
# Listening: all eleven kinds of material through the real feel chain.
make -f tools/build.mk test-tools >/dev/null
test/gates/uvpy test/signals/generate.py $work/signals >/dev/null 2>&1
test/gates/uvpy test/gates/feel_gate.py $work/signals build/h/test/feel_tool 2>/dev/null
r5=$?
rm -rf $work
exit $(( r1 | r2 | r3 | r4 | r5 ))
