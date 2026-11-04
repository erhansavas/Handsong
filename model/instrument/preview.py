# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Quick software preview of meshes, to judge form before anything is exported. Usage: preview(meshes, path)."""
import numpy as np
from PIL import Image


def render(parts, path, eye=(0, 190, 170), target=(0, 0, 0), size=(1400, 900), fov=30, up=(0, 1, 0)):
    """parts: list of (mesh, base colour, glossiness). A plain z-buffer rasteriser with a studio-like lighting, numpy only."""
    W, H = size
    eye, target = np.array(eye, dtype=np.float64), np.array(target, dtype=np.float64)
    forward = target - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, up)
    right /= np.linalg.norm(right)
    true_up = np.cross(right, forward)
    focal = 0.5 * H / np.tan(np.radians(fov) / 2)
    color = np.tile(np.array([0.05, 0.045, 0.05]), (H, W, 1)).astype(np.float32)
    depth = np.full((H, W), np.inf, dtype=np.float32)
    light = np.array([0.4, 0.8, 0.45])
    light /= np.linalg.norm(light)
    for mesh, base, gloss in parts:
        p = mesh.positions.astype(np.float64) - eye
        x, y, z = p @ right, p @ true_up, p @ forward
        sx, sy = W / 2 + focal * x / z, H / 2 - focal * y / z
        tri = mesh.indices.reshape(-1, 3)
        n = mesh.normals
        view = -_unit(mesh.positions - eye)
        half = _unit(view + light)
        diffuse = np.clip(n @ light, 0, 1)
        spec = np.clip((n * half).sum(-1), 0, 1) ** (20 + 300 * gloss)
        rim = (1 - np.clip((n * view).sum(-1), 0, 1)) ** 3
        shade = np.asarray(base)[None] * (0.18 + 0.75 * diffuse)[:, None] + (0.8 * gloss * spec + 0.25 * rim)[:, None]
        area = (sx[tri[:, 1]] - sx[tri[:, 0]]) * (sy[tri[:, 2]] - sy[tri[:, 0]]) - (sx[tri[:, 2]] - sx[tri[:, 0]]) * (sy[tri[:, 1]] - sy[tri[:, 0]])
        tri = tri[area < 0]
        for a, b, c in tri:
            xs, ys = sx[[a, b, c]], sy[[a, b, c]]
            x0, x1, y0, y1 = int(max(xs.min(), 0)), int(min(np.ceil(xs.max()), W - 1)), int(max(ys.min(), 0)), int(min(np.ceil(ys.max()), H - 1))
            if x1 < x0 or y1 < y0:
                continue
            px, py = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
            d = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
            if abs(d) < 1e-9:
                continue
            w1 = ((px - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (py - ys[0])) / d
            w2 = ((xs[1] - xs[0]) * (py - ys[0]) - (px - xs[0]) * (ys[1] - ys[0])) / d
            w0 = 1 - w1 - w2
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            zz = w0 * z[a] + w1 * z[b] + w2 * z[c]
            sub = depth[y0:y1 + 1, x0:x1 + 1]
            mask = inside & (zz < sub)
            sub[mask] = zz[mask]
            color[y0:y1 + 1, x0:x1 + 1][mask] = (w0[..., None] * shade[a] + w1[..., None] * shade[b] + w2[..., None] * shade[c])[mask]
    Image.fromarray((np.clip(color, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).save(path)


def _unit(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)
