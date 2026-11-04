# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""The speaker chain and codec across the listening set: headroom kept, nothing clipped, the useless bass removed."""
import glob
import os
import subprocess
import sys
import tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import metrics

RATE = 48000
failures = []


def check(condition, text):
    print(("pass " if condition else "FAIL ") + text)
    if not condition:
        failures.append(text)


def main():
    folder, tool = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory() as work:
        for raw in sorted(glob.glob(os.path.join(folder, "*.raw"))):
            name = os.path.basename(raw)[:-4]
            before, after = os.path.join(work, "before"), os.path.join(work, "after")
            subprocess.run([tool, raw, before, after], check=True)
            highest = float(np.fromfile(before, dtype=np.float32).max())
            decoded = np.fromfile(after, dtype=np.float32)
            levels = metrics.band_levels(decoded, RATE)
            print(f"{name:14} sent peak {20 * np.log10(highest + 1e-9):6.2f} dBFS, decoded peak {np.abs(decoded).max():.3f}, "
                  f"low {levels['low']:.0f} mid {levels['mid']:.0f} high {levels['high']:.0f} air {levels['air']:.0f}")
            check(highest <= 0.631 * 1.001, f"{name}: never above -4 dBFS into the codec")
            check(np.abs(decoded).max() <= 1.0, f"{name}: never clips after the codec")
            check(levels["low"] < levels["mid"] - 6, f"{name}: the bass it cannot play is gone")
    print(f"{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
