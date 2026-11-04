#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# What a clean machine can prove with no GPU, audio device, display or controller: the code builds with no warnings, and the logic holds.
set -e
setopt pipefail
cd "${0:A:h}/.."
work=$(mktemp -d)
trap 'rm -rf $work' EXIT
./build.sh > $work/build.log 2>&1 || { tail -30 $work/build.log; exit 1; }
for t in test/*_test.c; do
  bin=$work/${t:t:r}
  clang -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -I. $t -lm -o $bin
  $bin
done
build/Handsong.app/Contents/MacOS/Handsong --test mic-gate
make -f tools/build.mk test-tools > /dev/null
test/gates/uvpy test/signals/generate.py $work/signals > /dev/null
test/gates/uvpy test/gates/feel_gate.py $work/signals build/h/test/feel_tool | tail -1
print "all checks passed"
