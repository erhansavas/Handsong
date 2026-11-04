# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Writes the listening test set: 11 kinds of material, all synthesised here, so no commercial recording is involved.
Usage: generate.py <folder>. Each file is raw 32-bit float stereo at 48 kHz, ending in two seconds of digital silence."""
import os
import sys
import numpy as np

RATE = 48000
rng = np.random.default_rng(20261103)


def seconds(n):
    return int(n * RATE)


def envelope(t, tau):
    return np.exp(-t / tau)


def kick(length=0.28, f0=52):
    t = np.arange(seconds(length)) / RATE
    return np.sin(2 * np.pi * (f0 * t + 30 * (1 - np.exp(-t / 0.02)) * 0.02)) * envelope(t, 0.09)


def snare(length=0.2):
    t = np.arange(seconds(length)) / RATE
    return (rng.standard_normal(len(t)) * envelope(t, 0.05) + np.sin(2 * np.pi * 190 * t) * envelope(t, 0.04)) * 0.5


def hat(length=0.05):
    t = np.arange(seconds(length)) / RATE
    return np.diff(rng.standard_normal(len(t) + 1)) * envelope(t, 0.012) * 0.25


def place(track, sound, at, gain=1.0):
    start = seconds(at)
    end = min(len(track), start + len(sound))
    if start < len(track):
        track[start:end] += sound[:end - start] * gain


def note(hertz, length, partials=(1, 0.5, 0.25), tau=None):
    t = np.arange(seconds(length)) / RATE
    wave = sum(a * np.sin(2 * np.pi * hertz * (i + 1) * t) for i, a in enumerate(partials))
    return wave * (envelope(t, tau) if tau else np.minimum(1, t / 0.01) * np.minimum(1, (length - t) / 0.02))


def stereo(mono, width=None):
    if width is None:
        return np.c_[mono, mono]
    return np.c_[mono + width, mono - width]


def four_on_floor(duration=30):
    track = np.zeros(seconds(duration))
    beat = 60 / 125
    for i in range(int(duration / beat)):
        place(track, kick(), i * beat, 0.9)
        place(track, hat(), (i + 0.5) * beat, 0.5)
        place(track, note(55 * (1 if i % 8 < 6 else 1.2), beat * 0.45, (1, 0.3)), (i + 0.5) * beat, 0.4)
    return stereo(track)


def acoustic(duration=30):
    track, side = np.zeros(seconds(duration)), np.zeros(seconds(duration))
    for i in range(int(duration / 0.3)):
        pitch = [196, 247, 294, 392, 330, 247][i % 6]
        place(side, note(pitch, 0.8, (1, 0.6, 0.3, 0.15), tau=0.25), i * 0.3, 0.25 * (1 if i % 2 else -1))
        if i % 4 == 0:
            place(track, note([82, 98, 73, 110][(i // 4) % 4], 1.2, (1, 0.4), tau=0.5), i * 0.3, 0.5)
    return stereo(track, side)


def orchestra(duration=30):
    t = np.arange(seconds(duration)) / RATE
    swell = 0.1 + 0.9 * (0.5 - 0.5 * np.cos(2 * np.pi * t / 20))
    ensemble = sum(np.sin(2 * np.pi * f * t + i) for i, f in enumerate([110, 165, 220, 277, 330, 440])) / 6
    track = ensemble * swell * 0.5
    for at in (9.5, 10.5, 19.5, 20.5):
        place(track, note(65, 0.7, (1, 0.3), tau=0.2), at, 0.9)
    return stereo(track, np.sin(2 * np.pi * 277.5 * t) * swell * 0.1)


def piano(duration=30):
    track = np.zeros(seconds(duration))
    for i in range(int(duration / 1.4)):
        pitch = [55, 330, 262, 165, 392, 73, 196][i % 7]
        place(track, note(pitch, 2.5, (1, 0.5, 0.25, 0.12), tau=0.7), i * 1.4, 0.5)
    return stereo(track)


def rock(duration=30):
    t = np.arange(seconds(duration)) / RATE
    guitar = np.tanh(6 * (np.sin(2 * np.pi * 82.4 * t) + 0.5 * np.sin(2 * np.pi * 123.5 * t))) * 0.22
    track = guitar.copy()
    beat = 60 / 120
    for i in range(int(duration / beat)):
        place(track, kick(), i * beat if i % 2 == 0 else (i + 0.5) * beat, 0.8)
        if i % 2:
            place(track, snare(), i * beat, 0.9)
    return stereo(track, np.sin(2 * np.pi * 247 * t) * 0.05)


def ambient(duration=30):
    t = np.arange(seconds(duration)) / RATE
    track = sum(np.sin(2 * np.pi * f * t) * (0.5 + 0.5 * np.sin(2 * np.pi * t / p)) for f, p in
                [(98, 13), (147, 17), (196, 11), (294, 19)]) * 0.15
    return stereo(track, np.sin(2 * np.pi * 148 * t) * 0.05)


def sub_bass(duration=30):
    track = np.zeros(seconds(duration))
    beat = 60 / 70
    for i in range(int(duration / beat / 2)):
        length = beat * 1.8
        t = np.arange(seconds(length)) / RATE
        track_note = np.sin(2 * np.pi * (45 + 15 * np.exp(-t / 0.03)) * t) * np.minimum(1, (length - t) / 0.05)
        place(track, track_note, i * beat * 2, 0.85)
        place(track, snare(), (i + 0.5) * beat * 2, 0.5)
    return stereo(track)


def formant(hertz, length, formants):
    t = np.arange(seconds(length)) / RATE
    wave = np.zeros(len(t))
    for harmonic in range(1, 40):
        f = hertz * harmonic
        gain = sum(np.exp(-((f - c) / 120) ** 2) * a for c, a in formants) + 0.02
        wave += gain * np.sin(2 * np.pi * f * t + harmonic) / harmonic ** 0.5
    return wave * np.minimum(1, t / 0.015) * np.minimum(1, (length - t) / 0.03)


def speech(duration=30):
    """A voice in outline: pitch around 120 Hz, formants changing per syllable, about four syllables a second, with pauses."""
    track = np.zeros(seconds(duration))
    vowels = [[(700, 1), (1200, 0.5), (2600, 0.2)], [(300, 1), (2300, 0.5), (3000, 0.2)], [(500, 1), (1000, 0.5), (2500, 0.2)],
              [(400, 1), (1900, 0.5), (2500, 0.2)]]
    at = 0.0
    while at < duration - 1:
        words = int(rng.integers(3, 9))
        for _ in range(words):
            length = float(rng.uniform(0.14, 0.26))
            place(track, formant(float(rng.uniform(105, 140)), length, vowels[int(rng.integers(0, 4))]), at, 0.5)
            at += length + float(rng.uniform(0.02, 0.08))
        at += float(rng.uniform(0.25, 0.7))
    return stereo(track)


def film(duration=30):
    voice = speech(duration)[:, 0]
    rumble = np.zeros(len(voice))
    for at in (7.0, 16.0, 24.0):
        length = 1.2
        n = seconds(length)
        noise = rng.standard_normal(n)
        low = np.convolve(noise, np.ones(600) / 600, "same") * 40
        t = np.arange(n) / RATE
        place(rumble, low * np.exp(-t / 0.35) * np.minimum(1, t / 0.006), at, 0.8)
    return stereo(voice * 0.8 + rumble)


def scaled(signal, peak):
    return signal / np.abs(signal).max() * peak


def very_quiet():
    return scaled(four_on_floor(), 0.05)


def very_loud():
    loud = np.tanh(rock() * 5)
    return scaled(loud, 0.99)


SET = {"four_on_floor": four_on_floor, "acoustic": acoustic, "orchestra": orchestra, "piano": piano, "rock": rock, "ambient": ambient,
       "sub_bass": sub_bass, "speech": speech, "film": film, "very_quiet": very_quiet, "very_loud": very_loud}


def main():
    folder = sys.argv[1]
    os.makedirs(folder, exist_ok=True)
    for name, make in SET.items():
        audio = scaled(make(), 0.85) if name not in ("very_quiet", "very_loud") else make()
        audio = np.vstack([audio, np.zeros((seconds(2), 2))]).astype(np.float32)
        audio.tofile(os.path.join(folder, name + ".raw"))
        print(name, f"{len(audio) / RATE:.1f} s, peak {np.abs(audio).max():.3f}")


if __name__ == "__main__":
    main()
