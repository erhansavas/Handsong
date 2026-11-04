#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Everything generated, from nothing but this tree: the model with its bake, the score and the icon. About a minute. The same input
# gives the same output. Needs uv and Blender 5.2 for the bake; neither is needed to run the app.
set -e
cd "${0:A:h}/.."
BLENDER=${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}
mkdir -p assets/instrument
test/gates/uvpy model/instrument/cook_env.py assets/instrument/studio.hsenv
test/gates/uvpy model/instrument/cook_grade.py assets/instrument/grade.hslut assets/instrument/blue_noise.hsnoise
test/gates/uvpy model/instrument/export.py assets/instrument/instrument.hsmesh --cell 0.6 2>/dev/null | head -2
"$BLENDER" -b --factory-startup --python model/instrument/bake.py -- assets/instrument/instrument.hsmesh 128 2>&1 | grep -E "baking|occlusion"
test/gates/uvpy sound/compose.py assets/instrument/score
test/gates/uvpy tools/icon/make_icon.py
