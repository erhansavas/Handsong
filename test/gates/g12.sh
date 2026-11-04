#!/bin/zsh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
# The first impression, as far as a script can see it: how fast the first image is ready, and that the icon is really there.
cd "${0:A:h}/../.."
APP=build/Handsong.app
failed=0
# The very first launch compiles the pipelines once; every launch after that is what is measured.
$APP/Contents/MacOS/Handsong --test render --size 640x416 --out /tmp/handsong_first.png
report=$(HANDSONG_REPORT_LOAD=1 $APP/Contents/MacOS/Handsong --test render --size 2560x1664 --out /tmp/handsong_first.png)
print -r -- "$report"
ms=$(print -r -- "$report" | sed -nE 's/first frame finished ([0-9]+) ms.*/\1/p')
(( ms <= 300 )) || { print "FAIL first frame over 300 ms"; failed=1; }
opened=$(HANDSONG_FRAME_TRACE=1 $APP/Contents/MacOS/Handsong --test full-screen --seconds 4 2>&1 | sed -nE 's/from asking to the first image: ([0-9]+) ms/\1/p')
print "from opening the experience to its first image on screen: $opened ms (at most 1000)"
(( opened <= 1000 )) || { print "FAIL over a second"; failed=1; }
# The words must really be on the picture: they once were drawn in black on black and no check noticed.
$APP/Contents/MacOS/Handsong --test experience --script tour --seconds 61 --shot /tmp/handsong_words_ --shot-at 60 --size 1280x832 >/dev/null 2>&1
test/gates/uvpy -c "
from PIL import Image; import numpy as np
a = np.asarray(Image.open('/tmp/handsong_words_60.0.png').convert('L')).astype(float)
bright = (a[720:800, 80:1200] > 150).mean()
print(f'guidance on the picture: {bright:.3f} of its strip is lettering')
raise SystemExit(0 if bright > 0.01 else 1)" || { print "FAIL no words on the picture"; failed=1; }
plutil -extract CFBundleIconName raw $APP/Contents/Info.plist || failed=1
[[ -f $APP/Contents/Resources/Assets.car ]] && print "Assets.car present" || { print "FAIL no Assets.car"; failed=1; }
xcrun assetutil --info $APP/Contents/Resources/Assets.car 2>/dev/null | grep -c Handsong | sed 's/^/icon entries named Handsong: /'
exit $failed
