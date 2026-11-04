#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# If a controller is awake, checks the live link: reads only, nothing audible, no vibration.
cd "${0:A:h}/.."
exec build/Handsong.app/Contents/MacOS/Handsong --test hw-smoke "${1:-3}"
