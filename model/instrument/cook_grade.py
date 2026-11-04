# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Cooks the picture's colour table and its noise. The curve keeps hues: brass stays brass into its highlights instead of
turning lemon, and only the very brightest light drifts toward white, as it does on film."""
import struct
import sys
import numpy as np

SIZE = 33


def curve(x):
    """A sigmoid in log light: a soft toe for the dark field, mid grey in place, a long shoulder."""
    contrast, mid, mid_out = 1.28, 0.18, 0.17
    a = x ** contrast
    k = mid ** contrast * (1 - mid_out) / mid_out
    top = 32.0 ** contrast / (32.0 ** contrast + k)
    return a / (a + k) / top


def srgb_encode(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1 / 2.4) - 0.055)


def grade(linear):
    peak = np.maximum(linear.max(-1, keepdims=True), 1e-6)
    keep_hue = linear * curve(peak) / peak
    per_channel = curve(linear)
    highlight = np.clip((np.log2(peak) + 1) / 4, 0, 1)
    mixed = keep_hue * (1 - 0.55 * highlight) + per_channel * 0.55 * highlight
    return srgb_encode(mixed)


def blue_noise(size, sigma=1.9, seed=20261103):
    """Void and cluster (Ulichney): every threshold placed where the pattern is emptiest, so there is no low frequency at all."""
    rng = np.random.default_rng(seed)
    freq = np.fft.fftfreq(size)
    fx, fy = np.meshgrid(freq, freq)
    kernel = np.exp(-2 * (np.pi * sigma) ** 2 * (fx * fx + fy * fy))

    def energy(points):
        return np.fft.ifft2(np.fft.fft2(points) * kernel).real

    points = np.zeros((size, size))
    points.flat[rng.choice(size * size, size * size // 10, replace=False)] = 1
    while True:
        e = energy(points)
        tightest = np.unravel_index(np.argmax(np.where(points > 0, e, -np.inf)), points.shape)
        points[tightest] = 0
        e = energy(points)
        emptiest = np.unravel_index(np.argmin(np.where(points == 0, e, np.inf)), points.shape)
        if emptiest == tightest:
            points[tightest] = 1
            break
        points[emptiest] = 1
    start = points.copy()
    ones = int(points.sum())
    rank = np.zeros((size, size), dtype=np.int64)
    work = start.copy()
    for r in range(ones - 1, -1, -1):
        e = energy(work)
        spot = np.unravel_index(np.argmax(np.where(work > 0, e, -np.inf)), work.shape)
        work[spot] = 0
        rank[spot] = r
    work = start.copy()
    for r in range(ones, size * size):
        e = energy(work)
        spot = np.unravel_index(np.argmin(np.where(work == 0, e, np.inf)), work.shape)
        work[spot] = 1
        rank[spot] = r
    return ((rank + 0.5) / (size * size) * 256).astype(np.uint8)


def main():
    lut_path, noise_path = sys.argv[1], sys.argv[2]
    t = np.linspace(0, 1, SIZE)
    b, g, r = np.meshgrid(t, t, t, indexing="ij")
    shaped = np.stack([r, g, b], -1)
    linear = 2.0 ** (shaped * 16 - 11)
    encoded = grade(linear)
    rgba = np.concatenate([encoded, np.ones(encoded.shape[:3] + (1,))], -1)
    with open(lut_path, "wb") as f:
        f.write(b"LUT1" + struct.pack("<I", SIZE))
        f.write(rgba.astype("<f2").tobytes())
    noise = blue_noise(128)
    with open(noise_path, "wb") as f:
        f.write(b"BNS1" + struct.pack("<I", 128))
        f.write(noise.tobytes())
    print("grade and noise done", noise.min(), noise.max(), noise.mean())


if __name__ == "__main__":
    main()
