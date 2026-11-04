#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# A stranger's Mac, as far as a script can go: hardened runtime, exactly the entitlements needed, no dependence on Xcode or a private
# framework, and it starts from any folder. A new user account's first run remains a [user] item.
cd "${0:A:h}/../.."
APP=${1:-build/Handsong.app}
failed=0
codesign --verify --deep --strict $APP && print "signature: valid" || { print "FAIL signature"; failed=1; }
flags=$(codesign -dv $APP 2>&1 | grep -E "flags=")
print "$flags"
[[ $flags == *runtime* ]] || { print "FAIL no hardened runtime"; failed=1; }
codesign -d --entitlements - --xml $APP 2>/dev/null | plutil -p - 2>/dev/null
libs=$(otool -arch arm64 -L $APP/Contents/MacOS/Handsong | tail -n +2 | grep -vE "/System/Library/|/usr/lib/" || true)
[[ -z $libs ]] && print "links only to the system" || { print "FAIL links to: $libs"; failed=1; }
if strings $APP/Contents/MacOS/Handsong | grep -q gamepolicyctl; then print "FAIL names gamepolicyctl"; failed=1; else print "does not use gamepolicyctl"; fi
if strings $APP/Contents/MacOS/Handsong | grep -qE "/Applications/Xcode"; then print "FAIL names Xcode"; failed=1; else print "does not name Xcode"; fi
work=$(mktemp -d)
cp -R $APP $work/Moved.app
$work/Moved.app/Contents/MacOS/Handsong --test render --size 640x416 --out $work/x.png && print "runs from another folder" || { print "FAIL from another folder"; failed=1; }
HANDSONG_NO_ALERT=1 arch -x86_64 $work/Moved.app/Contents/MacOS/Handsong --test nothing >/dev/null 2>&1; print "on another processor it says so and leaves: exit code $?"
lipo -info $work/Moved.app/Contents/MacOS/Handsong
rm -rf $work
exit $failed
