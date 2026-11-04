# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""G2: every fixed view against the same frame at sixteen samples per pixel. Usage: g2.py <app binary> [view prefix]."""
import os
import subprocess
import sys
import tempfile
import numpy as np
from scipy import ndimage
sys.path.insert(0, os.path.dirname(__file__))
import metrics
import views as v


def render(binary, view, size, out):
    subprocess.run([binary, "--test", "render", "--out", out, "--size", size] + v.arguments(view), check=True, stderr=subprocess.DEVNULL)


def long_straight_edges(image, min_length=200, tolerance=0.5):
    """Strong, perfectly straight edges: a shape clipped by a line shows this, and FLIP cannot see it, as the reference has it too."""
    luma = image.mean(2)
    gx, gy = ndimage.sobel(luma, 1), ndimage.sobel(luma, 0)
    strong = np.hypot(gx, gy) > 0.5
    found = 0
    for axis_image in (strong, strong.T):
        for line in axis_image:
            run = np.flatnonzero(np.diff(np.r_[0, line.astype(int), 0]))
            for start, end in zip(run[::2], run[1::2]):
                if end - start >= min_length:
                    found += 1
    return found


def main():
    binary = sys.argv[1]
    prefix = sys.argv[2] if len(sys.argv) > 2 else ""
    failures = []
    with tempfile.TemporaryDirectory() as work:
        for name, view in v.VIEWS:
            if not name.startswith(prefix):
                continue
            a, b = os.path.join(work, "a.png"), os.path.join(work, "b.png")
            render(binary, view, "2560x1664", a)
            render(binary, view, "10240x6656", b)
            test = metrics.load_rgb(a)
            reference = metrics.downsample_linear(metrics.load_rgb(b), 4).astype(np.float32)
            report = metrics.flip_report(reference, test)
            straight = long_straight_edges(test)
            close = v.CLOSE.get(name)
            if report["shift"] != (0, 0):
                failures.append(f"{name}: registration {report['shift']}")
            if close:
                ok = report["mean"] <= close[0] and report["p99"] <= close[1] and report["edge_p99"] <= v.CLOSE_EDGE
            else:
                ok = report["object_mean"] <= v.ORBIT[0] and report["object_p99"] <= v.ORBIT[1] and report["object_p999"] <= v.ORBIT[2] and report["edge_p99"] <= v.ORBIT[3]
            print(f"{'pass' if ok else 'FAIL'} {name:22} mean {report['mean']:.4f} p99 {report['p99']:.3f} object {report['object_mean']:.4f} "
                  f"{report['object_p99']:.3f} {report['object_p999']:.3f} edges {report['edge_p99']:.3f} straight {straight}", flush=True)
            if not ok:
                failures.append(name)
    print(f"{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
