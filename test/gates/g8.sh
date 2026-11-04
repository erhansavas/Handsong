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
rm -rf $work
exit $(( r1 | r2 | r3 | r4 ))
