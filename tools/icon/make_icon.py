# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""The mark: a struck tuning fork and the sound leaving it, a song held in one hand. Drawn here from nothing.
Writes the layers of the Liquid Glass icon, a fallback .icns and the menu bar's template. Usage: make_icon.py (from the project root)."""
import json
import os
import subprocess
import tempfile
import numpy as np
from PIL import Image

SIZE = 1024


def fork_mask(size, scale=4):
    """Antialiased from a distance field: two round-ended tines, a bridge with soft inner corners, and a stem."""
    n = size * scale
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    x = (x + 0.5) / n * 2 - 1
    y = (y + 0.5) / n * 2 - 1

    def capsule(px, py, ax, ay, bx, by, r):
        vx, vy = bx - ax, by - ay
        t = np.clip(((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy), 0, 1)
        return np.hypot(px - ax - vx * t, py - ay - vy * t) - r

    def smooth_min(a, b, k):
        h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
        return b + (a - b) * h - k * h * (1 - h)

    tine_w = 0.105
    left = capsule(x, y, -0.27, -0.56, -0.27, 0.17, tine_w)
    right = capsule(x, y, 0.27, -0.56, 0.27, 0.17, tine_w)
    # The bridge is a shallow U, as the crotch of a fork is.
    bridge = capsule(x, y, -0.27, 0.17, 0.0, 0.30, tine_w * 0.92)
    bridge = np.minimum(bridge, capsule(x, y, 0.27, 0.17, 0.0, 0.30, tine_w * 0.92))
    stem = capsule(x, y, 0.0, 0.30, 0.0, 0.64, tine_w * 0.75)
    d = smooth_min(smooth_min(left, right, 0.001), smooth_min(bridge, stem, 0.05), 0.06)
    mask = np.clip(0.5 - d * n / 2, 0, 1)
    return np.array(Image.fromarray((mask * 255).astype(np.uint8)).resize((size, size), Image.LANCZOS)) / 255.0


def brass_layer(size):
    """Polished brass, lit from above: bright all the way to the tips, so it reads at any size and in any light."""
    mask = fork_mask(size)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    light = 1.0 - 0.34 * y + 0.10 * np.cos((x * 0.8 - y * 0.5) * 9.0)
    rgb = np.stack([1.00 * light, 0.78 * light, 0.42 * light], -1)
    return Image.fromarray((np.dstack([np.clip(rgb, 0, 1), mask]) * 255).astype(np.uint8), "RGBA")


def glow_layer(size):
    """What a struck fork does to the air: two waves leaving each side of it, in the filament's one warm light."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size * 2 - 1
    cy = y + 0.10
    r = np.hypot(x, cy)
    # How far round from straight out to the side, softly ended.
    spread = np.abs(np.arctan2(cy, np.abs(x) + 1e-6))
    ring = np.zeros_like(x)
    halo = np.zeros_like(x)
    for radius, reach, strength in ((0.58, 0.62, 1.0), (0.78, 0.50, 0.62)):
        fade = np.clip((reach - spread) / 0.10, 0, 1) * strength
        ring = np.maximum(ring, np.exp(-((r - radius) / 0.026) ** 2) * fade)
        halo = np.maximum(halo, np.exp(-((r - radius) / 0.10) ** 2) * 0.30 * fade)
    alpha = np.clip(ring + halo, 0, 1)
    rgb = np.stack([np.ones_like(x), 0.50 + 0.25 * ring, 0.18 + 0.28 * ring], -1)
    return Image.fromarray((np.dstack([rgb, alpha]) * 255).astype(np.uint8), "RGBA")


def flat_icon(size):
    """The same mark on its dark ground, for what cannot read the layered icon."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size * 2 - 1
    ground = np.stack([0.075 - 0.03 * y, 0.055 - 0.025 * y, 0.060 - 0.025 * y], -1)
    rounded = np.hypot(np.maximum(np.abs(x) - 0.60, 0), np.maximum(np.abs(y) - 0.60, 0)) - 0.20
    shape = np.clip(0.5 - rounded * size / 2, 0, 1)
    out = ground
    glow = np.asarray(glow_layer(size)).astype(np.float32) / 255
    out = out * (1 - glow[..., 3:]) + glow[..., :3] * glow[..., 3:]
    fork = np.asarray(brass_layer(size)).astype(np.float32) / 255
    out = out * (1 - fork[..., 3:]) + fork[..., :3] * fork[..., 3:]
    return Image.fromarray((np.dstack([np.clip(out, 0, 1), shape]) * 255).astype(np.uint8), "RGBA")


def main():
    base = "tools/icon"
    icon = os.path.join(base, "Handsong.icon")
    assets = os.path.join(icon, "Assets")
    os.makedirs(assets, exist_ok=True)
    brass_layer(SIZE).save(os.path.join(assets, "fork.png"))
    glow_layer(SIZE).save(os.path.join(assets, "waves.png"))
    description = {
        "fill": {"linear-gradient": ["display-p3:0.098,0.075,0.075,1.0", "display-p3:0.035,0.027,0.031,1.0"]},
        "groups": [
            {"layers": [{"image-name": "fork.png", "name": "fork", "glass": True}], "shadow": {"kind": "layer-color", "opacity": 0.5}, "translucency": {"enabled": True, "value": 0.2}},
            {"layers": [{"image-name": "waves.png", "name": "waves", "glass": False}], "shadow": {"kind": "none", "opacity": 0}, "translucency": {"enabled": False, "value": 0}},
        ],
        "supported-platforms": {"squares": "shared"},
    }
    with open(os.path.join(icon, "icon.json"), "w") as f:
        json.dump(description, f, indent=2)
    with tempfile.TemporaryDirectory() as work:
        folder = os.path.join(work, "Handsong.iconset")
        os.makedirs(folder)
        for size in (16, 32, 128, 256, 512):
            flat_icon(size).save(os.path.join(folder, f"icon_{size}x{size}.png"))
            flat_icon(size * 2).save(os.path.join(folder, f"icon_{size}x{size}@2x.png"))
        subprocess.run(["/usr/bin/iconutil", "-c", "icns", folder, "-o", os.path.join(base, "Handsong.icns")], check=True)
    template = (fork_mask(36) * 255).astype(np.uint8)
    Image.fromarray(np.dstack([np.zeros_like(template)] * 3 + [template]), "RGBA").save(os.path.join(base, "menubar.png"))


if __name__ == "__main__":
    main()
