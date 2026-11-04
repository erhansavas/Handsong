#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
cd "${0:A:h}/../.."
test/gates/uvpy test/gates/g4.py build/Handsong.app/Contents/MacOS/Handsong || failed=1
# Zebra stripes on the shell, to be judged by eye: unbroken lines mean a continuous surface.
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python test/gates/zebra.py -- assets/instrument/instrument.hsmesh body "$PWD/docs/evidence/zebra" 2>&1 | grep -E "^zebra|Traceback"
exit ${failed:-0}
