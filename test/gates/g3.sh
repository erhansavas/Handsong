#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
cd "${0:A:h}/../.."
exec test/gates/uvpy test/gates/g3.py build/Handsong.app/Contents/MacOS/Handsong
