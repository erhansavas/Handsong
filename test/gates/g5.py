# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""G5, the part a script can judge: the dark backdrop's gradient, from the finished 10-bit values. No lattice and no bands."""
import os
import subprocess
import sys
import tempfile
import numpy as np
from scipy import ndimage
sys.path.insert(0, os.path.dirname(__file__))
import metrics


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory() as work:
        out = os.path.join(work, "frame.raw10")
        subprocess.run([binary, "--test", "render", "--out", out, "--hold", "0,0,0", "--eye", "0,0.24,0.26,0,0,0,34"], check=True, stderr=subprocess.DEVNULL)
        raw = np.fromfile(out, dtype=np.uint32).reshape(1664, 2560)
    green = ((raw >> 10) & 1023).astype(np.float64)
    failures = 0
    for name, (y, x) in {"top left": (40, 40), "top": (40, 1150), "bottom": (1500, 1150), "left": (700, 60)}.items():
        patch = green[y:y + 128, x:x + 128]
        peak = metrics.lattice_peak(patch)
        smooth = ndimage.uniform_filter(patch, 15)[10:-10, 10:-10]
        step = max(np.abs(np.diff(smooth, axis=0)).max(), np.abs(np.diff(smooth, axis=1)).max())
        ok = peak <= 25 and step <= 1.0
        print(f"{'pass' if ok else 'FAIL'} {name:9} level {patch.mean():6.1f} fft peak/median {peak:6.1f} step {step:.3f} LSB")
        failures += not ok
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
