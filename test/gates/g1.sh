#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Build and analysis: warning-free build, analyzer, file sizes, no discard, unit tests.
cd "${0:A:h}/../.."
set -e
set -o pipefail
touch tools/build.mk
./build.sh > /tmp/handsong_build.log 2>&1 || { tail -20 /tmp/handsong_build.log; exit 1; }
if grep -E "warning:" /tmp/handsong_build.log | grep -v -E "third_party"; then print "warnings found"; exit 1; fi
print "build: no warnings"
make -f tools/build.mk analyze
print "analyzer: clean"
long=0
for f in $(find src shaders test model sound tools -type f \( -name '*.c' -o -name '*.h' -o -name '*.m' -o -name '*.cpp' -o -name '*.swift' -o -name '*.metal' -o -name '*.py' \) 2>/dev/null); do
  n=$(wc -l < $f)
  if (( n > 1000 )); then print "$f has $n lines"; long=1; fi
done
(( long == 0 )) || exit 1
print "file sizes: all at most 1000 lines"
if grep -l discard_fragment shaders/* 2>/dev/null; then print "discard_fragment found"; exit 1; fi
print "discard_fragment: none"
for t in test/*_test.c; do
  bin="/tmp/handsong_$(basename $t .c)"
  clang -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Isrc -I. $t -lm -o $bin
  $bin
done
print "unit tests: pass"
build/Handsong.app/Contents/MacOS/Handsong --test realtime-audit
print "realtime paths: clean"
# The sanitizers over a whole scripted tour and a fuzz run: every first-party line that can run without a window.
tour() { "$1" --test experience --script tour --seconds 300 2>&1 | tail -1; "$1" --test experience --script fuzz:3 --seconds 120 2>&1 | tail -1; }
ASAN=build/asan/Handsong.app/Contents/MacOS/Handsong
make -f tools/build.mk -j10 MODE=asan > /tmp/handsong_asan.log 2>&1 || { tail -20 /tmp/handsong_asan.log; exit 1; }
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 tour $ASAN
print "address and undefined-behaviour sanitizers: clean over the tour and the fuzzer"
TSAN=build/tsan/Handsong.app/Contents/MacOS/Handsong
make -f tools/build.mk -j10 MODE=tsan > /tmp/handsong_tsan.log 2>&1 || { tail -20 /tmp/handsong_tsan.log; exit 1; }
# Any report fails the gate, whatever the exit code of the run says.
TSAN_OPTIONS=halt_on_error=0 $TSAN --test full-screen --seconds 60 > /tmp/handsong_tsan_run.log 2>&1 || true
tail -1 /tmp/handsong_tsan_run.log
if grep -q "ThreadSanitizer" /tmp/handsong_tsan_run.log; then grep "SUMMARY: ThreadSanitizer" /tmp/handsong_tsan_run.log | sort -u; print "thread sanitizer: FAILED"; exit 1; fi
grep -q "^total frames" /tmp/handsong_tsan_run.log || { print "thread sanitizer: the run did not finish"; exit 1; }
print "thread sanitizer: clean over a live window"
# Metal's API and shader validation, in the real pipelines: a frame of each chapter's load.
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER_ERROR_MODE=assert build/Handsong.app/Contents/MacOS/Handsong --test experience --script tour --seconds 300 --gpu-every 20 --size 1280x832 2>&1 | tail -3 || { print "Metal validation: the run did not finish"; exit 1; }
print "Metal validation: clean"
