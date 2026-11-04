#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# The single command for every gate. test/run_gates.sh runs all; test/run_gates.sh G2 G3 runs some.
# A gate's result is its command and its output, kept in docs/evidence/<gate>.txt.
cd "${0:A:h}/.."
gates=("$@")
if [[ ${#gates} -eq 0 ]]; then
  for n in {1..13}; do [[ -x test/gates/g$n.sh ]] && gates+=(G$n); done
fi
mkdir -p docs/evidence
failed=0
for g in $gates; do
  script="test/gates/${g:l}.sh"
  log="docs/evidence/$g.txt"
  if [[ ! -x $script ]]; then print "$g NOT BUILT"; failed=1; continue; fi
  { print "\$ $script"; print "date: $(date -u +%FT%TZ)  commit: $(git rev-parse --short HEAD)"; } > $log
  if "$script" >> $log 2>&1; then print "$g PASS"; else print "$g FAIL (see $log)"; tail -5 $log; failed=1; fi
done
exit $failed
