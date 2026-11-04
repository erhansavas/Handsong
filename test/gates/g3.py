# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""G3: slow turn, against the same turn at sixteen samples per pixel: flicker and sparkle beyond what the true image has."""
import glob
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, os.path.dirname(__file__))
import metrics


def sequence(binary, work, tag, size, factor, hold, step, count):
    out = os.path.join(work, tag + ".png")
    subprocess.run([binary, "--test", "render", "--out", out, "--size", size, "--hold", hold, "--sequence", str(step), "--frames", str(count),
                    "--eye", "0,0.24,0.26,0,0,0,34"], check=True, stderr=subprocess.DEVNULL)
    frames = []
    for i in range(count):
        image = metrics.load_rgb(os.path.join(work, f"{tag}_{i}.png"))
        frames.append(metrics.downsample_linear(image, factor) if factor > 1 else image)
    return frames


def main():
    binary = sys.argv[1]
    failures = 0
    for hold in ("20,0,0", "130,60,0", "-35,30,0"):
        with tempfile.TemporaryDirectory() as work:
            test = sequence(binary, work, "t", "2560x1664", 1, hold, 0.05, 15)
            reference = sequence(binary, work, "r", "10240x6656", 4, hold, 0.05, 15)
            r = metrics.flicker_report(test, reference)
            ok = r["excess_mean"] <= 0.0004 and r["excess_p99"] <= 0.010 and r["excess_p999"] <= 0.05 and r["flickering_ratio"] <= 1.3 and r["sparkle"] <= 0.006 and r["sparkle"] <= max(r["sparkle_reference"] * 1.4, 1e-4)
            print(f"{'pass' if ok else 'FAIL'} {hold:10} excess {r['excess_mean']:.6f} {r['excess_p99']:.5f} {r['excess_p999']:.5f} ratio {r['flickering_ratio']:.2f} "
                  f"sparkle {r['sparkle']:.5f} reference {r['sparkle_reference']:.5f}", flush=True)
            failures += not ok
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
