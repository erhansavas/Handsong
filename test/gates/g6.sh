#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Frame pacing in the real window, native mode, silent. A tour, with the GPU's own cost per chapter at full clock measured separately.
cd "${0:A:h}/../.."
APP=build/Handsong.app/Contents/MacOS/Handsong
failed=0
$APP --test experience --script tour --seconds 300 --gpu-every 3 --size 2560x1664 2>&1 | tail -9 > /tmp/handsong_gpu.txt
cat /tmp/handsong_gpu.txt
ceilings=(6.4 6.3 7.9 8.3 8.3 8.2 8.1 16 8.6)     # the finale's own ceiling is the pressed case's, below
ceilings[8]=8.7
while read -r _ c _ _ _ _ worst _; do
  limit=${ceilings[$((c + 1))]}
  if (( worst > limit )); then print "FAIL chapter $c worst $worst over $limit"; failed=1; fi
done < <(grep "^chapter" /tmp/handsong_gpu.txt)
SECONDS_RUN=${1:-320}
$APP --test full-screen --seconds $SECONDS_RUN > /tmp/handsong_full.txt 2>&1 &
pid=$!
sleep 25
print "game mode: $(xcrun gamepolicyctl game-mode status 2>/dev/null | head -1 | sed 's/\x1b\[[0-9;]*m//g')"
wait $pid
grep -E "^chapter|^total" /tmp/handsong_full.txt
grep -q "drawable 2560x1664" /tmp/handsong_full.txt || { print "FAIL not the native mode"; failed=1; }
while read -r line; do
  p95=$(print $line | sed -E 's/.*p95 ([0-9.]+).*/\1/'); p99=$(print $line | sed -E 's/.*p99 ([0-9.]+).*/\1/'); worst=$(print $line | sed -E 's/.*worst ([0-9.]+) main.*/\1/'); skipped=$(print $line | sed -E 's/.*skipped ([0-9]+).*/\1/')
  if (( p95 > 16.9 || p99 > 17.3 || worst > 33.4 || skipped > 0 )); then print "FAIL $line"; failed=1; fi
done < <(grep "^chapter" /tmp/handsong_full.txt)
total=$(grep "^total" /tmp/handsong_full.txt)
thermal=$(print $total | sed -E 's/.*thermal ([0-9]+).*/\1/'); resident=$(print $total | sed -E 's/.*resident ([0-9]+).*/\1/')
(( thermal <= 1 )) || { print "FAIL thermal state $thermal"; failed=1; }
(( resident <= 900 )) || { print "FAIL resident memory $resident MB"; failed=1; }
# Going elsewhere and coming back, in the real window: Command-Tab, hiding, the question before quitting, the desktop's own mode.
$APP --test focus || failed=1
exit $failed
