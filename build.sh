#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Builds build/Handsong.app. One machine, so one configuration. Pass a make target to build something else.
set -e
cd "${0:A:h}"
exec make -f tools/build.mk -j10 "$@"
