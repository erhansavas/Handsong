# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""What a mix must never contain: clicks, undesigned gaps, a limiter working hard. Usage: mix_checks.py sound.f32 marks.txt events.txt."""
import sys
import numpy as np
import metrics

RATE = 48000


def main():
    sound = np.fromfile(sys.argv[1], dtype="<f4").reshape(-1, 2).astype(np.float64)
    marks = [line.split() for line in open(sys.argv[2])]
    marks = [(float(a), int(b), int(c)) for a, b, c in marks]
    failed = []
    # Silence at the very start, the held breath, the speaker chapter and the finale's end are designed. Any other gap over 10 ms is not.
    designed = [(0, 2.05), (33.0, 33.45), (18.0, 20.1)]
    for t, chapter, phase in marks:
        if chapter == 6:
            designed.append((t - 0.1, t + 40))
        if chapter == 7 and phase == 0:
            designed.append((t + 18, t + 20.2))
    gaps = metrics.silent_gaps(sound.mean(1), RATE, 0.010)
    undesigned = [(a, b) for a, b in gaps if not any(x - 0.05 <= a and b <= y + 0.05 for x, y in designed)]
    print(f"gaps over 10 ms: {len(gaps)}, undesigned {len(undesigned)} {undesigned[:5]}")
    if undesigned:
        failed.append("undesigned gaps")
    # Clicks anywhere in the tour. A sound that was meant to begin at that moment is not a click: each one logged its first sample.
    events = np.sort(np.loadtxt(sys.argv[3], ndmin=1))
    found = np.array(metrics.clicks(sound), dtype=np.int64)
    if len(found):
        at = np.searchsorted(events, found)
        before = events[np.clip(at - 1, 0, len(events) - 1)]
        after = events[np.clip(at, 0, len(events) - 1)]
        meant = ((found - before) < 0.040 * RATE) & ((found - before) >= 0) | ((after - found) < 0.002 * RATE) & ((after - found) >= 0)
        stray = found[~meant]
        # A note inside a piece of music also starts sharply and logs nothing. A click is a jump that stands out from the samples around it.
        mono = sound.mean(1)
        step = np.abs(np.diff(mono, prepend=mono[0]))
        def stands_out(i):
            near = step[max(0, i - 240):i + 240]
            return step[i - 2:i + 3].max() > 6 * np.median(near) and step[i - 2:i + 3].max() > 0.08
        stray = np.array([i for i in stray if stands_out(int(i))], dtype=np.int64)
    else:
        stray = found
    print(f"sharp edges in the tour: {len(found)}, not at a sound's own start: {len(stray)} {np.round(stray[:10] / RATE, 3).tolist()}")
    if len(stray):
        failed.append("clicks")
    # Reduction by the limiter over 1 dB, as a share of time.
    ceiling = 0.70
    over = float(np.mean(np.max(np.abs(sound), axis=1) >= ceiling * 0.999))
    print(f"at the limiter's ceiling: {over * 100:.2f}% of samples (at most 1)")
    if over > 0.01:
        failed.append("limiter working too hard")
    bands = metrics.band_levels(sound)
    print("bands:", {k: round(float(v), 1) for k, v in bands.items()} if isinstance(bands, dict) else np.round(bands, 1))
    if failed:
        print("FAILED:", failed)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
