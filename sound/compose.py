# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""The score of Handsong, composed here from nothing: every note placed by this file, every timbre computed from partials and
noise. It reads no audio and no reference from any other work, and the same run always gives the same samples.
Usage: compose.py <output folder>. Each stem is normalised to its chapter's loudness target with test/gates/metrics.py."""
import os
import subprocess
import sys
import wave
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test", "gates"))
import metrics

RATE = 48000
RNG = np.random.default_rng(20261103)
A4 = 440.0


def midi(n):
    return A4 * 2.0 ** ((n - 69) / 12.0)


NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note(name):
    """'D4', 'Bb3', 'F#5' to a MIDI number."""
    n = NAMES[name[0]]
    i = 1
    while name[i] in "#b":
        n += 1 if name[i] == "#" else -1
        i += 1
    return n + 12 * (int(name[i:]) + 1)


# ---------------------------------------------------------------------------- Timbres
def partials(freqs, levels, decays, seconds, attack=0.002):
    """A sum of decaying sines, each with its own rate. The rate at which highs die is what tells the ear the material."""
    n = int(seconds * RATE)
    t = np.arange(n, dtype=np.float64) / RATE
    out = np.zeros(n, dtype=np.float64)
    for f, a, d in zip(freqs, levels, decays):
        if f > RATE * 0.45:
            continue
        out += a * np.sin(2 * np.pi * f * t + RNG.uniform(0, 0.4)) * np.exp(-t / d)
    env = np.minimum(t / max(attack, 1e-5), 1.0)
    fade = np.minimum((n - np.arange(n)) / (RATE * 0.01), 1.0)
    return (out * env * fade).astype(np.float32)


def noise_burst(seconds, low, high, decay):
    n = int(seconds * RATE)
    spectrum = np.fft.rfft(RNG.standard_normal(n))
    freq = np.fft.rfftfreq(n, 1 / RATE)
    spectrum *= (freq >= low) & (freq <= high)
    out = np.fft.irfft(spectrum, n)
    return (out / max(np.max(np.abs(out)), 1e-6) * np.exp(-np.arange(n) / RATE / decay)).astype(np.float32)


def kalimba(f, seconds=2.4, level=1.0):
    """A steel tine on a wooden box: an inharmonic second mode that dies fast, and a soft thump from the box."""
    tone = partials([f, f * 2.0, f * 5.93, f * 13.2], [1.0, 0.16, 0.34, 0.10], [seconds * 0.42, seconds * 0.22, 0.09, 0.03], seconds, 0.0012)
    box = partials([f * 0.5 if f > 300 else f, 310.0], [0.10, 0.08], [0.05, 0.03], seconds, 0.001)
    return (tone + box) * level


def voice_tine(f, seconds=1.9, level=1.0):
    """A small thumb-piano note for the controller's own driver: round, a soft start, one quiet upper mode and no high ping."""
    return partials([f, f * 2.0, f * 3.9], [1.0, 0.12, 0.06], [seconds * 0.40, seconds * 0.14, seconds * 0.05], seconds, 0.006) * level


def felt(f, seconds=3.5, level=1.0):
    """A felted hammer on a string: warm, the top gone almost at once, the partials very slightly stretched."""
    freqs = [f * k * np.sqrt(1 + 0.00035 * k * k) for k in range(1, 9)]
    amp = [1.0, 0.55, 0.26, 0.16, 0.08, 0.05, 0.03, 0.02]
    dec = [seconds * 0.46 / (1 + 0.5 * k) for k in range(8)]
    return partials(freqs, amp, dec, seconds, 0.004) * level


def bell(f, seconds=4.0, level=1.0):
    return partials([f * 0.5, f, f * 1.19, f * 1.5, f * 2.0, f * 2.51, f * 3.01], [0.5, 1.0, 0.45, 0.35, 0.3, 0.14, 0.08],
                    [seconds * 0.6, seconds * 0.4, seconds * 0.3, seconds * 0.2, seconds * 0.14, seconds * 0.08, seconds * 0.05], seconds, 0.001) * level


def swell(seconds, attack, release):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    up = 0.5 - 0.5 * np.cos(np.pi * np.minimum(t / attack, 1.0))
    down = 0.5 - 0.5 * np.cos(np.pi * np.minimum((seconds - t) / release, 1.0))
    return (up * down).astype(np.float32)


def glass_pad(f, seconds, level=1.0, attack=2.0, release=2.0):
    """Wet fingers on glass: a few pure partials beating slowly against slightly detuned twins."""
    n = int(seconds * RATE)
    t = np.arange(n, dtype=np.float64) / RATE
    out = np.zeros(n, dtype=np.float64)
    for k, a in ((1, 1.0), (2, 0.3), (3, 0.12), (4.02, 0.06)):
        d = RNG.uniform(0.15, 0.5)
        out += a * (np.sin(2 * np.pi * f * k * t + RNG.uniform(0, 6)) + np.sin(2 * np.pi * (f * k + d) * t + RNG.uniform(0, 6))) * 0.5
    return (out.astype(np.float32) * swell(seconds, attack, release)) * level


def brass(f, seconds, level=1.0, attack=0.5, release=0.8, bright=1.0):
    """A swelling reed: many harmonics, opening as the note grows."""
    n = int(seconds * RATE)
    t = np.arange(n, dtype=np.float64) / RATE
    env = swell(seconds, attack, release).astype(np.float64)
    out = np.zeros(n, dtype=np.float64)
    vibrato = 0.0015 * np.sin(2 * np.pi * 4.7 * t) * np.minimum(t / 1.2, 1)
    for k in range(1, 15):
        if f * k > 16000:
            break
        open_up = env ** (0.5 + k * 0.32 / bright)
        out += np.sin(2 * np.pi * f * k * (t + vibrato / (2 * np.pi * 4.7)) + RNG.uniform(0, 6)) * open_up / (k ** 1.15)
    return (out * env).astype(np.float32) * level * 0.5


def bass(f, seconds=1.2, level=1.0):
    return partials([f, f * 2, f * 3], [1.0, 0.32, 0.1], [seconds * 0.5, seconds * 0.25, seconds * 0.12], seconds, 0.006) * level


def tick(f=2400.0, level=1.0):
    return (partials([f, f * 1.73, f * 2.6], [1.0, 0.5, 0.25], [0.012, 0.008, 0.004], 0.08, 0.0002) + noise_burst(0.08, 2000, 12000, 0.004) * 0.3).astype(np.float32) * level


def ratchet(count, spacing, level=1.0):
    out = np.zeros(int((count * spacing + 0.1) * RATE), dtype=np.float32)
    for i in range(count):
        hit = tick(1900 + 40 * (i % 3), 0.7 + 0.3 * ((i % 4) == 0))
        at = int(i * spacing * RATE)
        out[at:at + len(hit)] += hit
    return out * level


def air(seconds, low, high, level=1.0, attack=1.0, release=1.0):
    return noise_burst(seconds, low, high, 1e6) * swell(seconds, attack, release) * level


# ---------------------------------------------------------------------------- Laying things out
class Stem:
    def __init__(self, seconds, loop=True):
        self.n = int(seconds * RATE)
        self.loop = loop
        self.lr = np.zeros((self.n, 2), dtype=np.float32)

    def put(self, at_seconds, sound, pan=0.0, gain=1.0):
        at = int(at_seconds * RATE)
        angle = (pan + 1) * np.pi / 4
        gl, gr = np.cos(angle) * gain, np.sin(angle) * gain
        count = len(sound)
        if self.loop:          # whatever runs past the end comes round to the beginning, so the loop has no seam
            idx = (at + np.arange(count)) % self.n
            np.add.at(self.lr[:, 0], idx, sound * gl)
            np.add.at(self.lr[:, 1], idx, sound * gr)
        else:
            end = min(self.n, at + count)
            if at < end:
                self.lr[at:end, 0] += sound[:end - at] * gl
                self.lr[at:end, 1] += sound[:end - at] * gr

    def room(self, seconds=0.9, wet=0.25, brightness=5000.0):
        """A small hall from decaying filtered noise, different in each ear. Circular for loops, so tails wrap too."""
        n = int(seconds * RATE)
        ir = np.zeros((n, 2), dtype=np.float64)
        for c in range(2):
            x = RNG.standard_normal(n) * np.exp(-np.arange(n) / RATE / (seconds * 0.24))
            spectrum = np.fft.rfft(x)
            f = np.fft.rfftfreq(n, 1 / RATE)
            spectrum *= 1 / (1 + (f / brightness) ** 2)
            ir[:, c] = np.fft.irfft(spectrum, n)
            ir[:, c] /= np.sqrt(np.sum(ir[:, c] ** 2))
        size = self.n if self.loop else self.n + n
        for c in range(2):
            wet_c = np.fft.irfft(np.fft.rfft(self.lr[:, c], size) * np.fft.rfft(ir[:, c], size), size)
            self.lr[:, c] = (self.lr[:, c] + wet * wet_c[:self.n]).astype(np.float32)
        return self


def chord_notes(names):
    return [midi(note(x)) for x in names.split()]


# Everything lives in D dorian, with Bb lent from the minor whenever the music wants to darken.
PROGRESSION = ["D3 A3 C4 E4 F4", "Bb2 F3 A3 C4 D4", "G2 D3 F3 A3 C4", "A2 E3 G3 C4 D4"]
MOTIF = ["A4", "D5", "E5", "F5", "E5", "C5", "D5"]        # the theme: a reach upward, a turn, and home


def coils_bed():
    bar = 60 / 72 * 4
    s = Stem(bar * 8)
    for i in range(8):
        chord = chord_notes(PROGRESSION[(i // 2) % 4])
        for f in chord[:3]:
            s.put(i * bar, glass_pad(f, bar * 1.3, 0.16, 0.9, 0.9), RNG.uniform(-0.4, 0.4))
        for k in range(8):
            f = chord[[0, 2, 1, 3, 4, 3, 2, 1][k]]
            s.put(i * bar + k * bar / 8, felt(f, 2.6, 0.28 + 0.1 * (k == 0)), -0.25 + 0.5 * (k % 2))
        s.put(i * bar, bass(chord[0] / 2, 2.4, 0.42))
    return s.room(1.1, 0.30, 4200)


def coils_top():
    beat = 60 / 72
    s = Stem(beat * 32)
    line = [(0, "A4"), (1.5, "D5"), (2.5, "E5"), (3, "F5"), (8, "E5"), (9.5, "C5"), (10.5, "D5"), (16, "A4"), (17, "C5"), (18.5, "D5"), (24, "F5"), (25, "E5"), (26.5, "D5"), (28, "A4")]
    for at, name in line:
        s.put(at * beat, kalimba(midi(note(name)), 2.6, 0.6), RNG.uniform(-0.35, 0.35))
    return s.room(1.1, 0.32, 5200)


def gears_bed():
    beat = 60 / 96
    s = Stem(beat * 32)
    chords = ["D3 A3 D4 F4", "F3 C4 F4 A4", "C3 G3 C4 E4", "Bb2 F3 Bb3 D4"]
    for i, c in enumerate(chords * 2):
        for f in chord_notes(c):
            s.put(i * beat * 4, brass(f, beat * 4.4, 0.34, 0.5, 0.7, 1.1), RNG.uniform(-0.45, 0.45))
    return s.room(0.9, 0.22, 4500)


def gears_motion():
    beat = 60 / 96
    s = Stem(beat * 32)
    roots = ["D2", "D2", "F2", "F2", "C2", "C2", "Bb1", "A1"]
    for b in range(32):
        root = midi(note(roots[b // 4]))
        s.put(b * beat, bass(root, 0.8, 0.85 if b % 4 == 0 else 0.6))
        s.put((b + 0.5) * beat, bass(root * (1.5 if b % 8 == 7 else 1), 0.42, 0.34))
        s.put(b * beat, tick(2100, 0.55 if b % 2 == 0 else 0.36), -0.2)
        s.put((b + 0.5) * beat, tick(2800, 0.22), 0.25)
        if b % 8 == 6:
            s.put((b + 0.75) * beat, ratchet(5, beat / 10, 0.4), 0.35)
        if b % 4 == 2:
            s.put(b * beat, noise_burst(0.16, 180, 900, 0.05) * 0.5, 0.0)
    return s.room(0.5, 0.14, 6000)


def lattice_bed():
    beat = 60 / 54
    s = Stem(beat * 24)
    chords = ["D4 A4 E5", "F4 C5 G5", "A3 E4 B4", "C4 G4 D5"]
    for i, c in enumerate(chords):
        for f in chord_notes(c):
            s.put(i * beat * 6, glass_pad(f, beat * 7.5, 0.13, 3.0, 3.0), RNG.uniform(-0.7, 0.7))
    s.put(0, air(beat * 24, 3000, 9000, 0.012, 3, 3))
    return s.room(1.6, 0.42, 7000)


def lattice_top():
    beat = 60 / 54
    s = Stem(beat * 20)
    for b, name in [(0, "E6"), (3, "A5"), (4.5, "D6"), (8, "G6"), (9, "E6"), (13, "C6"), (15, "A5"), (17.5, "B5")]:
        s.put(b * beat, bell(midi(note(name)), 3.4, 0.2), RNG.uniform(-0.7, 0.7))
    return s.room(1.6, 0.5, 8000)


def balance_bed():
    beat = 60 / 108
    s = Stem(beat * 48)
    chords = ["D3 A3 C4 F4", "G2 D3 F3 B3", "C3 G3 Bb3 E4", "A2 E3 A3 C4"]
    for i, c in enumerate(chords * 2):
        for f in chord_notes(c):
            s.put(i * beat * 6, glass_pad(f, beat * 7, 0.15, 1.6, 1.6), RNG.uniform(-0.5, 0.5))
    return s.room(0.9, 0.28, 5200)


def balance_motion():
    beat = 60 / 108
    s = Stem(beat * 48)
    roots = ["D2", "G1", "C2", "A1"]
    tops = [["D4", "F4", "A4"], ["D4", "F4", "B4"], ["E4", "G4", "C5"], ["E4", "A4", "C5"]]
    for bar in range(16):
        which = (bar // 2) % 4
        s.put(bar * 3 * beat, bass(midi(note(roots[which])), 0.9, 0.7), -0.1)
        for k in (1, 2):
            s.put((bar * 3 + k) * beat, kalimba(midi(note(tops[which][k])), 1.0, 0.34), 0.25 if k == 1 else -0.25)
        if bar % 4 == 3:
            s.put((bar * 3 + 2.5) * beat, kalimba(midi(note(tops[which][0])) * 2, 0.8, 0.28), 0.4)
        s.put(bar * 3 * beat, tick(1500, 0.22))
    return s.room(0.8, 0.2, 5200)


def breath_bed():
    s = Stem(36.0)
    for f, at, length in [(midi(note("D2")), 0, 24), (midi(note("A2")), 6, 24), (midi(note("F3")), 16, 22), (midi(note("D3")), 26, 20)]:
        s.put(at, glass_pad(f, length, 0.5, 8, 8), RNG.uniform(-0.3, 0.3))
    return s.room(1.8, 0.35, 2600)


def workshop_bed():
    beat = 60 / 84
    s = Stem(beat * 32)
    chords = ["D3 A3 F4", "Bb2 F3 D4", "F3 C4 A4", "C3 G3 E4"]
    for i, c in enumerate(chords * 2):
        notes = chord_notes(c)
        for f in notes:
            s.put(i * beat * 4, felt(f, beat * 4.5, 0.5), RNG.uniform(-0.3, 0.3))
            s.put(i * beat * 4, glass_pad(f, beat * 4.6, 0.09, 1.2, 1.2), RNG.uniform(-0.5, 0.5))
        s.put(i * beat * 4, bass(notes[0] / 2, 2.4, 0.5))
        for b in range(4):
            s.put((i * 4 + b) * beat, tick(1300, 0.16 + 0.1 * (b == 0)))
    return s.room(1.0, 0.25, 4600)


def opening_rise():
    """From the moment the shells close to the lock: eleven seconds that only know how to rise, and stop dead."""
    s = Stem(11.0, loop=False)
    for f in chord_notes("D3 A3 D4 E4 A4"):
        s.put(0, glass_pad(f, 11.0, 0.2, 7.0, 0.05), RNG.uniform(-0.6, 0.6))
    for f in chord_notes("D2 A2"):
        s.put(3, brass(f, 8.0, 0.5, 6.0, 0.03), 0)
    s.put(0, air(11.0, 400, 5000, 0.05, 9.0, 0.03))
    return s


def opening_open():
    """After the silence: the chord opens, the theme's first reach, and then it hands on to the first chapter."""
    s = Stem(12.0, loop=False)
    for f in chord_notes("D3 A3 C4 E4 F4 A4"):
        s.put(0, glass_pad(f, 12.0, 0.17, 0.9, 5.0), RNG.uniform(-0.6, 0.6))
        s.put(0, felt(f, 5.0, 0.5), RNG.uniform(-0.3, 0.3))
    s.put(0, bass(midi(note("D2")), 6.0, 0.8))
    for k, name in enumerate(MOTIF):
        s.put(0.6 + k * 0.62, kalimba(midi(note(name)), 3.2, 0.62), -0.3 + 0.1 * k)
    return s.room(1.4, 0.32, 5200)


def finale():
    """The whole instrument at once. It builds for fourteen seconds to the last chord, and leaves the last two in silence."""
    s = Stem(20.0, loop=False)
    beat = 60 / 96
    chords = ["D3 A3 D4 F4", "Bb2 F3 Bb3 D4", "F3 C4 F4 A4", "C3 G3 C4 E4 G4", "D3 A3 D4 F4", "Bb2 F3 Bb3 D4", "C3 G3 C4 E4"]
    for i, c in enumerate(chords):
        at = i * beat * 3.5
        grow = 0.55 + 0.45 * i / 6
        for f in chord_notes(c):
            s.put(at, brass(f, beat * 3.8, 0.42 * grow, 0.3, 0.5, 1.0 + 0.5 * grow), RNG.uniform(-0.5, 0.5))
            s.put(at, glass_pad(f * 2, beat * 3.8, 0.12 * grow, 0.3, 0.4), RNG.uniform(-0.7, 0.7))
        root = chord_notes(c)[0] / 2
        for b in range(7):
            s.put(at + b * beat * 0.5, bass(root, 0.5, 0.62 * grow))
            s.put(at + b * beat * 0.5, tick(2300, 0.4 * grow), RNG.uniform(-0.3, 0.3))
    for k, name in enumerate(MOTIF * 2):
        s.put(3.0 + k * beat * 0.75, kalimba(midi(note(name)) * 2, 2.6, 0.5), -0.5 + 0.08 * k)
        s.put(6.0 + k * beat * 0.75, bell(midi(note(name)), 2.6, 0.22), 0.5 - 0.08 * k)
    last = chord_notes("D2 A2 D3 A3 D4 F4 A4 C5 E5")
    for f in last:
        s.put(14.0, brass(f, 4.0, 0.42, 0.04, 3.0, 1.6), RNG.uniform(-0.6, 0.6))
        s.put(14.0, bell(f * 2, 4.0, 0.14), RNG.uniform(-0.6, 0.6))
    s.put(14.0, bass(midi(note("D1")), 3.8, 1.0))
    s.room(1.0, 0.25, 6000)
    # Nothing at all after the eighteenth second, however the room rang.
    end = int(18.0 * RATE)
    fade = int(0.35 * RATE)
    s.lr[end - fade:end] *= np.cos(np.linspace(0, np.pi / 2, fade))[:, None].astype(np.float32)
    s.lr[end:] = 0
    return s


def org_stop(up=True):
    """Pulling a stop on an organ: wood on wood, a breath of air, and a short chord that opens or closes."""
    s = Stem(1.8, loop=False)
    s.put(0.0, partials([210, 390, 640, 950], [1.0, 0.7, 0.4, 0.2], [0.03, 0.022, 0.015, 0.01], 0.2, 0.001), 0, 0.9)
    s.put(0.0, air(1.6, 500, 3000, 0.1, 0.18, 1.0))
    notes = "D4 A4 D5" if up else "A3 D4 F4"
    for f in chord_notes(notes):
        s.put(0.05, brass(f, 1.6, 0.34, 0.16, 1.1, 0.8), RNG.uniform(-0.4, 0.4))
    return s.room(0.8, 0.24, 5000)


def bridge():
    """Under the handover from the Mac to the controller's own speaker, and back: never a hole in the sound."""
    s = Stem(4.0, loop=False)
    for f in chord_notes("D4 A4 E5"):
        s.put(0, glass_pad(f, 4.0, 0.18, 0.2, 2.8), RNG.uniform(-0.4, 0.4))
    return s.room(1.0, 0.3, 5000)


def voice_piece():
    """Written for a driver the size of a coin, on its own, in the hands. Nothing below 520 Hz, and the harmony is only ever
    implied by a low tine's shape, as on a real thumb piano. At 45 kHz, as the controller wants it."""
    beat = 60 / 88
    rate = 45000
    s = Stem(beat * 50, loop=False)
    upper = ["A5", "D6", "E6", "F6", "E6", "C6", "D6", "A5", "C6", "D6", "F6", "G6", "F6", "E6", "D6", "C6", "A5", "G5", "A5", "C6", "D6"]
    lower = ["D5", "A5", "D5", "A5", "Bb4", "F5", "Bb4", "F5", "G5", "D6", "G5", "D6", "A4", "E5", "A4", "E5"]
    # Room between the notes: on a driver this small, density is what turns into a buzz.
    for k in range(int(beat * 48 / (beat * 2))):
        s.put(k * beat * 2, voice_tine(midi(note(lower[k * 2 % len(lower)])), 1.6, 0.30), -0.05)
    at = 4.0
    for bar in range(3):
        for i, name in enumerate(upper):
            s.put(at * beat, voice_tine(midi(note(name)), 1.9, 0.62), 0.05)
            at += [2, 1, 1, 3, 1, 2, 4][i % 7]
            if at > 46:
                break
    s.room(0.20, 0.08, 3500)
    out = s.lr.mean(axis=1)
    # This driver is hot from 4 kHz up and hard transients crackle through its codec: everything is rounded off well below that.
    for _ in range(2):
        out = np.array(out, dtype=np.float64)
        a = 1 - np.exp(-2 * np.pi * 3600 / RATE)
        y = 0.0
        for i in range(len(out)):
            y += a * (out[i] - y)
            out[i] = y
    # To the controller's own clock.
    t = np.arange(int(len(out) * rate / RATE)) * RATE / rate
    return np.interp(t, np.arange(len(out)), out).astype(np.float32), rate


# ---------------------------------------------------------------------------- Writing
STEMS = [
    ("open_rise", opening_rise, -22.0), ("open_open", opening_open, -22.0),
    ("coils_bed", coils_bed, -25.5), ("coils_top", coils_top, -32.0),
    ("gears_bed", gears_bed, -23.5), ("gears_motion", gears_motion, -24.0),
    ("lattice_bed", lattice_bed, -28.0), ("lattice_top", lattice_top, -35.0),
    ("balance_bed", balance_bed, -27.0), ("balance_motion", balance_motion, -24.5),
    ("breath_bed", breath_bed, -30.0),
    ("finale", finale, -15.5), ("workshop_bed", workshop_bed, -21.0),
    ("stop_up", lambda: org_stop(True), -24.0), ("stop_down", lambda: org_stop(False), -24.0), ("bridge", bridge, -26.0),
]


def write_caf(path, data, rate):
    """16-bit with fine triangular dither, then Apple Lossless through the system's own converter."""
    channels = 1 if data.ndim == 1 else data.shape[1]
    tpdf = (RNG.random(data.shape) - RNG.random(data.shape)) / 32768.0
    pcm = np.clip(np.rint((data + tpdf) * 32767), -32767, 32767).astype("<i2")
    temporary = path + ".wav"
    with wave.open(temporary, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())
    subprocess.run(["/usr/bin/afconvert", "-f", "caff", "-d", "alac", temporary, path], check=True)
    os.remove(temporary)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, make, target in STEMS:
        s = make()
        now = metrics.lufs(s.lr.astype(np.float64), RATE)
        gain = 10 ** ((target - now) / 20)
        data = s.lr * gain
        peak = float(np.max(np.abs(data)))
        if peak > 0.70:
            data *= 0.70 / peak
        print(f"{name:16} {len(data) / RATE:6.1f} s  {metrics.lufs(data.astype(np.float64), RATE):6.1f} LUFS  peak {max(peak, 0):.3f}")
        write_caf(os.path.join(out, name + ".caf"), data, RATE)
    voice, rate = voice_piece()
    voice *= 0.20 / np.max(np.abs(voice))     # -14 dBFS: the level of a small object in the hands, far below where the driver strains
    print(f"voice            {len(voice) / rate:6.1f} s  at {rate} Hz")
    write_caf(os.path.join(out, "voice.caf"), voice, rate)


if __name__ == "__main__":
    main()
