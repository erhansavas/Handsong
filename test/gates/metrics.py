# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""The measurements behind the gates: image error, flicker, dither, loudness, clicks and hand signals."""
import numpy as np
from PIL import Image
from scipy import ndimage, signal


# ---------------------------------------------------------------------------- Images
def load_rgb(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255


def downsample_linear(image, factor):
    """Averages in linear light, the way a supersampled reference has to be reduced."""
    h, w, c = image.shape
    linear = np.clip(image, 0, 1) ** 2.2
    return linear.reshape(h // factor, factor, w // factor, factor, c).mean((1, 3)) ** (1 / 2.2)


def registration(a, b):
    """Phase correlation. Anything but (0, 0) means the two frames do not show the same pose, so a comparison is void."""
    ya, yb = a.mean(2), b.mean(2)
    cross = np.fft.fft2(ya - ya.mean()) * np.conj(np.fft.fft2(yb - yb.mean()))
    corr = np.fft.ifft2(cross / (np.abs(cross) + 1e-6)).real
    peak = np.unravel_index(np.argmax(corr), corr.shape)
    return tuple(int(p if p < s / 2 else p - s) for p, s in zip(peak, corr.shape))


def object_mask(reference):
    luma = reference.mean(2)
    return luma > np.percentile(luma[:64, :], 99.5) + 0.01


def edge_mask(reference):
    luma = reference.mean(2)
    gradient = np.hypot(ndimage.sobel(luma, 0), ndimage.sobel(luma, 1))
    return ndimage.binary_dilation(gradient > 0.4, iterations=1)


def flip_report(reference, test):
    import flip_evaluator as flip
    # The raw error, not the colour-mapped picture of it that is the default.
    error, mean, _ = flip.evaluate(reference, test, "LDR", applyMagma=False)
    if error.ndim == 3:
        error = error[..., 0]
    mask, edges = object_mask(reference), edge_mask(reference)
    target = mask if mask.any() else np.ones_like(mask)
    return {
        "shift": registration(reference, test),
        "mean": float(mean),
        "p99": float(np.percentile(error, 99)),
        "object_mean": float(error[target].mean()),
        "object_p99": float(np.percentile(error[target], 99)),
        "object_p999": float(np.percentile(error[target], 99.9)),
        "edge_p99": float(np.percentile(error[edges], 99)) if edges.any() else 0.0,
    }


def linear_luma(frame):
    return (np.clip(frame, 1e-4, 1) ** 2.2) @ np.array([0.2126, 0.7152, 0.0722])


def flicker_map(frames):
    """Mean absolute second difference of each pixel over time: steady motion gives little, a flickering pixel a lot."""
    luma = np.stack([linear_luma(f) for f in frames])
    return np.abs(luma[:-2] - 2 * luma[1:-1] + luma[2:]).mean(0)


def sparkle(frames):
    """Share of pixels that jump above twice their own median for at most two frames."""
    luma = np.stack([linear_luma(f) for f in frames])
    median = np.median(luma, 0)
    runs = (luma > np.maximum(median * 2, median + 0.02)).sum(0)
    return float(((runs > 0) & (runs <= 2)).mean())


def flicker_report(test_frames, reference_frames):
    test, reference = flicker_map(test_frames), flicker_map(reference_frames)
    excess = np.maximum(test - reference, 0)
    return {
        "excess_mean": float(excess.mean()),
        "excess_p99": float(np.percentile(excess, 99)),
        "excess_p999": float(np.percentile(excess, 99.9)),
        "flickering_ratio": float((test > 0.01).mean() / max((reference > 0.01).mean(), 1e-9)),
        "sparkle": sparkle(test_frames),
        "sparkle_reference": sparkle(reference_frames),
    }


def lattice_peak(patch):
    """FFT peak over median of a detrended patch. A regular dither pattern gives an enormous peak, blue noise does not."""
    size = patch.shape[0]
    x, y = np.meshgrid(np.arange(size), np.arange(size))
    basis = np.c_[np.ones(size * size), x.ravel(), y.ravel(), (x ** 2).ravel(), (y ** 2).ravel(), (x * y).ravel()]
    coefficients = np.linalg.lstsq(basis, patch.ravel(), rcond=None)[0]
    residual = patch - (basis @ coefficients).reshape(size, size)
    window = np.outer(np.hanning(size), np.hanning(size))
    power = np.abs(np.fft.fft2(residual * window)) ** 2
    # What a slow gradient leaves near zero frequency is not a pattern anyone sees: a lattice lives higher up.
    freq = np.fft.fftfreq(size) * size
    fx, fy = np.meshgrid(freq, freq)
    power[np.hypot(fx, fy) < 8] = np.median(power)
    return float(power.max() / np.median(power))


# ---------------------------------------------------------------------------- Sound
RATE = 48000


def k_weight(x):
    b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1, -2, 1], [1, -1.99004745483398, 0.99007225036621]
    return signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)


def lufs(x, rate=RATE):
    """Integrated loudness after ITU-R BS.1770: K weighting, 400 ms blocks, -70 LUFS and -10 LU gates."""
    if len(x) < rate // 2:
        return float("-inf")
    weighted = k_weight(x)
    size, hop = int(0.4 * rate), int(0.1 * rate)
    power = np.array([(weighted[s:s + size] ** 2).mean(0).sum() for s in range(0, len(weighted) - size, hop)])
    level = -0.691 + 10 * np.log10(power + 1e-12)
    power = power[level > -70]
    if not len(power):
        return float("-inf")
    gate = -0.691 + 10 * np.log10(power.mean()) - 10
    power = power[-0.691 + 10 * np.log10(power) > gate]
    return float(-0.691 + 10 * np.log10(power.mean())) if len(power) else float("-inf")


def true_peak_db(x):
    return float(20 * np.log10(np.abs(signal.resample_poly(x, 4, 1, axis=0)).max() + 1e-9))


def clicks(x, order=24, threshold=6.0, frame=2048):
    """Sample indices where an autoregressive prediction fails badly: a discontinuity no smooth signal can make."""
    mono = x.mean(1) if x.ndim == 2 else x
    found = []
    for start in range(0, len(mono) - frame, frame // 2):
        block = mono[start:start + frame] * 1.0
        if np.abs(block).max() < 1e-5:
            continue
        autocorrelation = np.correlate(block, block, "full")[frame - 1:frame + order]
        autocorrelation[0] *= 1.0001
        try:
            coefficients = np.linalg.solve(_toeplitz(autocorrelation[:-1]), -autocorrelation[1:])
        except np.linalg.LinAlgError:
            continue
        residual = signal.lfilter(np.r_[1, coefficients], [1], block)[order:]
        sigma = np.median(np.abs(residual)) / 0.6745 + 1e-9
        hits = np.where((np.abs(residual) > threshold * 3 * sigma) & (np.abs(residual) > 0.01))[0]
        found.extend((start + order + hits).tolist())
    return sorted(set(found))


def _toeplitz(row):
    n = len(row)
    return np.array([[row[abs(i - j)] for j in range(n)] for i in range(n)])


def silent_gaps(x, rate=RATE, minimum=0.010):
    """Start and length, in seconds, of every stretch of digital silence longer than the minimum."""
    quiet = np.abs(x).max(1) < 1e-6 if x.ndim == 2 else np.abs(x) < 1e-6
    edges = np.diff(np.r_[0, quiet.astype(int), 0])
    starts, ends = np.flatnonzero(edges == 1), np.flatnonzero(edges == -1)
    return [(s / rate, (e - s) / rate) for s, e in zip(starts, ends) if (e - s) / rate > minimum]


def band_levels(x, rate=RATE):
    mono = x.mean(1) if x.ndim == 2 else x
    power = np.abs(np.fft.rfft(mono * np.hanning(len(mono)))) ** 2
    freq = np.fft.rfftfreq(len(mono), 1 / rate)
    bands = {"low": (20, 150), "mid": (150, 800), "high": (800, 4000), "air": (4000, 16000)}
    return {k: float(10 * np.log10(power[(freq >= a) & (freq < b)].sum() + 1e-9)) for k, (a, b) in bands.items()}


# ---------------------------------------------------------------------------- Hands
HAND_RATE = 3000


def hand_report(blocks):
    """blocks: signed bytes, two per frame. Doluluk, saturation, longest silence, contrast."""
    x = blocks.reshape(-1, 2).astype(np.float32)
    frames = np.abs(x).max(1)
    envelope = np.convolve(frames, np.ones(150) / 150, "same")
    silent, longest = 0, 0
    for quiet in frames <= 3:
        silent = silent + 1 if quiet else 0
        longest = max(longest, silent)
    return {
        "seconds": len(x) / HAND_RATE,
        "peak": float(frames.max()) if len(frames) else 0.0,
        "saturated_share": float((frames >= 124).mean()) if len(frames) else 0.0,
        "active_share": float((frames > 3).mean()) if len(frames) else 0.0,
        "longest_silence": longest / HAND_RATE,
        "contrast": float((np.percentile(envelope, 95) + 1) / (np.percentile(envelope, 50) + 1)) if len(frames) else 1.0,
    }
