#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Motion: a recorded resting session and scripted cases. Real hand sessions join test/fixtures when the owner has recorded them.
cd "${0:A:h}/../.."
for f in test/fixtures/motion_*.txt; do build/Handsong.app/Contents/MacOS/Handsong --test motion $f || exit 1; done
