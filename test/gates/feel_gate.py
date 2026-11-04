# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Feel across the synthetic listening set. Usage: feel_gate.py <signals folder> <tool>. G9, the listening part."""
import glob
import os
import subprocess
import sys
import tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import metrics

RATE_BLOCKS = 3000 / 64
failures = []


def check(condition, text):
    print(("pass " if condition else "FAIL ") + text)
    if not condition:
        failures.append(text)


def analyse(raw, tool, work):
    name = os.path.basename(raw)[:-4]
    hands, view = os.path.join(work, name + ".hands"), os.path.join(work, name + ".view")
    subprocess.run([tool, raw, hands, view], check=True)
    blocks = np.fromfile(hands, dtype=np.int8)
    table = np.loadtxt(view)
    report = metrics.hand_report(blocks)
    frames = blocks.reshape(-1, 2).astype(np.float32)
    body = (frames[:, 0] + frames[:, 1]) / 2
    music_part = body[:int(30 * 3000)]
    report["rms"] = float(np.sqrt((music_part ** 2).mean()))
    report["speech"] = float(table[: int(28 * RATE_BLOCKS), 4].mean())
    gain = table[int(5 * RATE_BLOCKS): int(28 * RATE_BLOCKS), 5]
    window = int(10 * RATE_BLOCKS)
    drift = 20 * np.log10((np.maximum.accumulate(gain)[window:] + 1e-6) / (gain[window:] + 1e-6))
    report["gain_swing_10s"] = float(max(abs(20 * np.log10(gain[i + window] / gain[i])) for i in range(0, len(gain) - window, 5)))
    report["after_silence"] = float(np.abs(frames[int(30.5 * 3000):]).max()) if len(frames) > 30.5 * 3000 else 0.0
    return name, report


def main():
    folder, tool = sys.argv[1], sys.argv[2]
    results = {}
    with tempfile.TemporaryDirectory() as work:
        for raw in sorted(glob.glob(os.path.join(folder, "*.raw"))):
            name, report = analyse(raw, tool, work)
            results[name] = report
            print(f"{name:14} rms {report['rms']:6.1f} peak {report['peak']:4.0f} active {report['active_share'] * 100:5.1f}% "
                  f"contrast {report['contrast']:5.1f} saturated {report['saturated_share'] * 100:6.3f}% speech {report['speech']:.2f} "
                  f"swing {report['gain_swing_10s']:4.1f} dB after silence {report['after_silence']:.0f}")
    music = ["four_on_floor", "rock", "sub_bass", "acoustic"]
    reference = np.mean([results[m]["rms"] for m in music])
    for name, r in results.items():
        check(r["saturated_share"] <= 0.001, f"{name}: limiter touches at most 0.1% ({r['saturated_share'] * 100:.3f}%)")
        check(r["after_silence"] == 0, f"{name}: still in true silence ({r['after_silence']:.0f})")
    check(results["speech"]["rms"] <= 0.20 * reference, f"speech body {results['speech']['rms']:.1f} at most 20% of music {reference:.1f}")
    check(results["speech"]["speech"] > 0.7, f"speech recognised ({results['speech']['speech']:.2f})")
    for name in music + ["orchestra", "ambient", "piano", "very_loud", "very_quiet"]:
        check(results[name]["speech"] < 0.35, f"{name}: not mistaken for speech ({results[name]['speech']:.2f})")
    for name in ("four_on_floor", "rock", "sub_bass"):
        check(results[name]["gain_swing_10s"] <= 3, f"{name}: the level holds, swing at most 3 dB in 10 s ({results[name]['gain_swing_10s']:.1f})")
    quiet, loud = results["very_quiet"]["rms"], results["four_on_floor"]["rms"]
    check(abs(20 * np.log10(quiet / loud)) <= 3, f"quiet master feels like a normal one ({20 * np.log10(quiet / loud):+.1f} dB)")
    check(results["film"]["peak"] > 90, f"a blow in a film still gets through ({results['film']['peak']:.0f})")
    check(results["ambient"]["rms"] < results["four_on_floor"]["rms"] * 0.8 or results["ambient"]["contrast"] >= 1, "ambient stays calm")
    print(f"{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
