# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""What a scripted tour sounded and felt like, chapter by chapter. Usage: tour_report.py sound.f32 hands.s8 marks.txt. Exit 1 on a failed threshold."""
import sys
import numpy as np
import metrics

RATE = 48000
NAMES = ["Assembly", "Coils", "Gears", "Lattice", "Balance", "Breath", "Voice", "Finale", "Workshop", "Listening"]
LUFS_TARGET = {0: -20, 1: -24, 2: -18, 3: -26, 4: -22, 5: -28, 7: -16, 8: -20}


def main():
    sound = np.fromfile(sys.argv[1], dtype="<f4").reshape(-1, 2).astype(np.float64)
    hands = np.fromfile(sys.argv[2], dtype=np.int8).reshape(-1, 2).astype(np.float64)
    marks = [line.split() for line in open(sys.argv[3])]
    marks = [(float(a), int(b), int(c)) for a, b, c in marks]
    spans, plays = {}, {}
    for i, (t, chapter, phase) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(sound) / RATE
        spans.setdefault(chapter, [t, end])[1] = end
        if phase == 1:
            plays[chapter] = (t, end)
    failed = []
    whole = metrics.lufs(sound, RATE)
    peak = metrics.true_peak_db(sound)
    print(f"whole tour: {whole:.1f} LUFS, true peak {peak:.2f} dBTP, invalid {int(np.sum(~np.isfinite(sound)))}")
    if not -21.5 <= whole <= -18.5:
        failed.append("whole tour loudness")
    if peak > -2:
        failed.append("true peak")
    print(f"{'chapter':10} {'seconds':>7} {'LUFS':>6} {'peak':>7} {'hands fill':>10} {'top':>4} {'>=124':>6} {'contrast':>8} {'longest quiet':>13}")
    for chapter, (a, b) in sorted(spans.items()):
        x = sound[int(a * RATE):int(b * RATE)]
        h = hands[int(a * 3000):int(b * 3000)]
        loud = metrics.lufs(x, RATE) if len(x) > RATE else -99
        p = metrics.true_peak_db(x) if len(x) else -99
        if len(h):
            mono = np.max(np.abs(h), axis=1)
            fill = float(np.mean(mono > 3))
            top = int(mono.max())
            over = float(np.mean(mono >= 124))
            window = 150
            env = np.convolve(mono, np.ones(window) / window, mode="same")
            contrast = (np.percentile(env, 95) + 1) / (np.percentile(env, 50) + 1)
            run = longest = 0
            for v in mono < 1:
                run = run + 1 if v else 0
                longest = max(longest, run)
            quiet = longest / 3000
        else:
            fill, top, over, contrast, quiet = 0, 0, 0, 0, 0
        print(f"{NAMES[chapter]:10} {b - a:7.1f} {loud:6.1f} {p:7.2f} {fill * 100:9.1f}% {top:4d} {over * 100:5.1f}% {contrast:8.1f} {quiet:12.2f}s")
        if chapter in LUFS_TARGET and abs(loud - LUFS_TARGET[chapter]) > 2:
            failed.append(f"{NAMES[chapter]} loudness {loud:.1f}, wanted {LUFS_TARGET[chapter]}")
        if chapter == 6:
            pa, pb = plays[6]
            active = sound[int((pa + 0.5) * RATE):int((pb - 0.6) * RATE)]
            mac = metrics.lufs(active, RATE) if len(active) > RATE else -99
            print(f"           Mac during the speaker: {mac:.1f} LUFS")
            if mac > -60:
                failed.append("the Mac is not silent during the speaker chapter")
    # The designed silences, and each chapter's vocabulary in play.
    table = {1: (0.40, 1.0, 3), 2: (0.50, 1.0, 3), 3: (0.60, 1.0, 2), 4: (0.10, 0.60, 3), 8: (0.0, 1.0, 3)}
    for chapter, (low, high, wanted_contrast) in table.items():
        if chapter not in plays:
            continue
        a, b = plays[chapter]
        mono = np.max(np.abs(hands[int(a * 3000):int(b * 3000)]), axis=1)
        fill = float(np.mean(mono > 3))
        window = 150
        env = np.convolve(mono, np.ones(window) / window, mode="same")
        contrast = (np.percentile(env, 95) + 1) / (np.percentile(env, 50) + 1)
        print(f"{NAMES[chapter]} in play: fill {fill * 100:.1f}% (wanted {low * 100:.0f} to {high * 100:.0f}), contrast {contrast:.1f} (wanted {wanted_contrast})")
        if not low <= fill <= high:
            failed.append(f"{NAMES[chapter]} hands fill {fill:.2f}")
        if contrast < wanted_contrast:
            failed.append(f"{NAMES[chapter]} hands contrast {contrast:.1f}")
    assembly = np.max(np.abs(hands[:int(39 * 3000)]), axis=1)
    held = float(np.max(assembly[int(33.02 * 3000):int(33.36 * 3000)])) if len(assembly) > 34 * 3000 else 0
    first = float(np.max(assembly[:int(1.15 * 3000)]))
    print(f"Assembly: first second peak {first:.0f}, the held breath peak {held:.0f} (both wanted 0)")
    if first > 0 or held > 0:
        failed.append("the opening's designed silences")
    if failed:
        print("FAILED:", "; ".join(failed))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
