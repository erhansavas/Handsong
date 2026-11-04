#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# The disk image anyone can download: open it, drag Handsong onto Applications. Without a paid Developer ID macOS asks once on first
# opening (System Settings, Privacy & Security, Open Anyway). tools/sign_notarize_package.sh is the way that asks nothing.
set -e
cd "${0:A:h}/.."
./build.sh
codesign --verify --deep --strict build/Handsong.app
work=$(mktemp -d)
cp -R build/Handsong.app "$work/"
ln -s /Applications "$work/Applications"
hdiutil create -volname Handsong -srcfolder "$work" -ov -format ULFO build/Handsong.dmg >/dev/null
rm -rf "$work"
shasum -a 256 build/Handsong.dmg
du -h build/Handsong.dmg
