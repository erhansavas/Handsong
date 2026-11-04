# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Cooks the stage's light: a dark field. A clear object shows only what it bends and reflects, so what draws it here is a
black room, two tall cards at the sides, a soft panel above, and a low warm line behind. Prefiltered for every roughness."""
import os
import struct
import sys
import numpy as np

FACE = 256
MIPS = 8


def face_directions(face, size):
    t = (np.arange(size) + 0.5) / size * 2 - 1
    u, v = np.meshgrid(t, t)
    d = {0: (np.ones_like(u), -v, -u), 1: (-np.ones_like(u), -v, u), 2: (u, np.ones_like(u), v),
         3: (u, -np.ones_like(u), -v), 4: (u, -v, np.ones_like(u)), 5: (-u, -v, -np.ones_like(u))}[face]
    d = np.stack(d, -1)
    return d / np.linalg.norm(d, axis=-1, keepdims=True)


def soft_box(azimuth, elevation, az0, az1, el0, el1, soft):
    a = np.clip((azimuth - az0) / soft, 0, 1) * np.clip((az1 - azimuth) / soft, 0, 1)
    b = np.clip((elevation - el0) / soft, 0, 1) * np.clip((el1 - elevation) / soft, 0, 1)
    return a * a * (3 - 2 * a) * b * b * (3 - 2 * b)


def radiance(d):
    """Azimuth 0 looks away from the player, along -z. Colours are linear Display P3."""
    azimuth = np.degrees(np.arctan2(d[..., 0], -d[..., 2]))
    elevation = np.degrees(np.arcsin(np.clip(d[..., 1], -1, 1)))
    color = np.zeros(d.shape, dtype=np.float64)
    color += np.array([0.0030, 0.0026, 0.0030])

    def add(mask, rgb):
        color[...] += mask[..., None] * np.array(rgb)

    add(soft_box(azimuth, elevation, -48, 48, 50, 86, 10), (1.10, 1.05, 1.00))        # the soft panel above
    add(soft_box(azimuth, elevation, -112, -70, 6, 58, 4), (5.2, 4.9, 4.6))           # tall card, left
    add(soft_box(azimuth, elevation, 70, 112, 6, 58, 4), (5.2, 4.9, 4.6))             # tall card, right
    add(soft_box(azimuth, elevation, -30, -26, 12, 60, 1.5), (3.4, 3.2, 3.0))         # thin strip, inner left
    add(soft_box(azimuth, elevation, 26, 30, 12, 60, 1.5), (3.4, 3.2, 3.0))           # thin strip, inner right
    add(soft_box(azimuth, elevation, -70, 70, -4, 7, 3), (2.6, 1.15, 0.62))           # the warm line low behind
    add(soft_box(azimuth, elevation, 120, 240, 22, 44, 8), (0.30, 0.28, 0.26))        # a faint fill from where you sit
    add(soft_box(azimuth - 360, elevation, 120 - 360, 240 - 360, 22, 44, 8), (0.30, 0.28, 0.26))
    add(soft_box(azimuth, elevation, -180, 180, -90, -50, 12), (0.035, 0.022, 0.018))   # the dark floor's warmth
    return color


def radical_inverse(bits):
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)
    return bits * 2.3283064365386963e-10


def prefilter(directions, roughness, samples):
    """Importance samples GGX around each direction, with n = v = r."""
    alpha = max(roughness * roughness, 1e-4)
    ids = np.arange(samples, dtype=np.uint64)
    xi1 = ids / samples
    xi2 = np.array([radical_inverse(int(i)) for i in ids])
    phi = 2 * np.pi * xi1
    cos_theta = np.sqrt((1 - xi2) / (1 + (alpha * alpha - 1) * xi2))
    sin_theta = np.sqrt(1 - cos_theta * cos_theta)
    h_local = np.stack([sin_theta * np.cos(phi), sin_theta * np.sin(phi), cos_theta], -1)
    n = directions.reshape(-1, 3)
    up = np.where(np.abs(n[:, 2, None]) < 0.999, np.array([0.0, 0.0, 1.0]), np.array([1.0, 0.0, 0.0]))
    t = np.cross(up, n)
    t /= np.linalg.norm(t, axis=-1, keepdims=True)
    b = np.cross(n, t)
    total = np.zeros_like(n)
    weight = np.zeros(len(n))
    for h in h_local:
        half = t * h[0] + b * h[1] + n * h[2]
        vh = (n * half).sum(-1)
        l = 2 * vh[:, None] * half - n
        nl = np.maximum((n * l).sum(-1), 0)
        total += radiance(l) * nl[:, None]
        weight += nl
    return (total / np.maximum(weight, 1e-6)[:, None]).reshape(directions.shape)


def spherical_harmonics(size=64):
    coefficients = np.zeros((9, 3))
    t = (np.arange(size) + 0.5) / size * 2 - 1
    u, v = np.meshgrid(t, t)
    solid = 4 / ((1 + u * u + v * v) ** 1.5) * (2 / size) ** 2
    for face in range(6):
        d = face_directions(face, size)
        x, y, z = d[..., 0], d[..., 1], d[..., 2]
        c = radiance(d)
        basis = [np.ones_like(x), y, z, x, x * y, y * z, 3 * z * z - 1, x * z, x * x - y * y]
        for i, b in enumerate(basis):
            coefficients[i] += ((c * (b * solid)[..., None]).sum((0, 1)))
    scale = np.array([0.282095, 0.488603, 0.488603, 0.488603, 1.092548, 1.092548, 0.315392, 1.092548, 0.546274])
    return coefficients * scale[:, None]


def main():
    out = sys.argv[1]
    harmonics = spherical_harmonics()
    with open(out, "wb") as f:
        f.write(b"ENV2" + struct.pack("<II", FACE, MIPS))
        f.write(harmonics.astype("<f4").tobytes())
        for mip in range(MIPS):
            size = max(FACE >> mip, 1)
            roughness = mip / (MIPS - 1)
            for face in range(6):
                d = face_directions(face, size)
                color = radiance(d) if mip == 0 else prefilter(d, roughness, 96 if size > 64 else 256)
                rgba = np.concatenate([color, np.ones(color.shape[:2] + (1,))], -1)
                f.write(rgba.astype("<f2").tobytes())
            print("mip", mip, size, flush=True)
    print("env", out, os.path.getsize(out))


if __name__ == "__main__":
    main()
