#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# Signs, notarises and packages the built app. Run this yourself: it uses your Developer ID and your notary profile, and nothing here
# ever sees them. First, once: xcrun notarytool store-credentials handsong-notary
# Usage: tools/sign_notarize_package.sh "Developer ID Application: Your Name (TEAMID)" [notary profile]
set -e
cd "${0:A:h}/.."
identity=${1:?give your Developer ID Application identity}
profile=${2:-handsong-notary}
app=build/Handsong.app
./build.sh
codesign --force --options runtime --timestamp --sign "$identity" --identifier io.github.erhansavas.handsong.guardian $app/Contents/Helpers/handsong-guardian
codesign --force --options runtime --timestamp --sign "$identity" --identifier io.github.erhansavas.handsong --entitlements tools/entitlements.plist $app
codesign --verify --deep --strict $app
ditto -c -k --keepParent $app build/Handsong.zip
xcrun notarytool submit build/Handsong.zip --keychain-profile "$profile" --wait
xcrun stapler staple $app
work=$(mktemp -d)
cp -R $app $work/
ln -s /Applications $work/Applications
hdiutil create -volname Handsong -srcfolder $work -ov -format ULFO build/Handsong.dmg
codesign --sign "$identity" --timestamp build/Handsong.dmg
xcrun notarytool submit build/Handsong.dmg --keychain-profile "$profile" --wait
xcrun stapler staple build/Handsong.dmg
rm -rf $work
print "ready: build/Handsong.dmg"
