#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Writes the publishable tree from an allow-list, with a fresh Git history of one commit. Nothing outside the list can get in, so
# working notes and anything from earlier versions stay behind. Usage: tools/export_release.sh [destination]
set -e
cd "${0:A:h}/.."
to=${1:-$HOME/demo_release}
rm -rf "$to"
mkdir -p "$to"
allow=(LICENSE NOTICE README.md build.sh .gitignore.release .github src shaders model/instrument sound test/*.c test/*.m test/*.sh test/fixtures test/gates test/signals tools third_party/jolt third_party/opus/PROVENANCE third_party/opus/opus-1.5.2 third_party/GamepadMotionHelpers docs/ASSETS.md docs/PROTOCOL_NOTES.md docs/media assets/instrument/studio.hsenv assets/instrument/grade.hslut assets/instrument/blue_noise.hsnoise)
for item in $allow; do
  for f in ${~item}(N); do
    [[ -e $f ]] || continue
    mkdir -p "$to/${f:h}"
    cp -R "$f" "$to/$f"
  done
done
rm -rf "$to/third_party/opus/opus-1.5.2/dnn"     # optional neural models, 17 MB, which this build does not use
rm -f "$to/test/hand_session.sh"      # a personal checklist
rm -f "$to/test/gates/g11.sh" "$to/test/gates/g11_scan.py" "$to/test/gates/forbidden_hashes.txt"     # the release check itself stays with whoever releases
find "$to" \( -name __pycache__ -o -name .DS_Store -o -name '.git' \) -prune -exec rm -rf {} + 2>/dev/null || true
[[ -f "$to/.gitignore.release" ]] && mv "$to/.gitignore.release" "$to/.gitignore"
( cd "$to" && git init -q && git add -A && git -c user.name="$(git -C "$OLDPWD" config user.name)" -c user.email="$(git -C "$OLDPWD" config user.email)" commit -q -m "Handsong" )
print "exported to $to"
