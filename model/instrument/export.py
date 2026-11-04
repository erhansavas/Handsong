# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Builds every part of the instrument and writes the model pack. Usage: export.py <out.hsmesh> [--cell mm]."""
import os
import struct
import sys
import time
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from instrument import body, controls, crew, mechanism
from instrument.controls import part

MATERIALS = ["BRASS_BRUSHED", "BRASS_POLISHED", "STEEL_BLUE", "STEEL_BRUSHED", "RUBY", "CERAMIC", "COPPER", "BRASS_PERLAGE", "BRONZE_DARK",
             "RUBBER", "PAPER", "FILAMENT", "ACRYLIC", "LENS", "KNURLED",
             "WATER", "WOOD", "GRAVEL", "LATTICE", "LATTICE_NODE", "PINS", "COPPER_PLAIN", "STEEL_SATIN", "STEEL_POLISHED",
             "MATTE_DARK", "GLOSS_DARK", "SMOKED_KEY", "SLATE", "PRINT"]
PASSES = {"solid": 0, "clear": 1, "clear_back": 2, "dome": 3, "dome_back": 4}
MM = 0.001


def pack_snorm(v):
    c = np.clip(np.rint(v * 511), -511, 511).astype(np.int64) & 1023
    return (c[:, 0] | (c[:, 1] << 10) | (c[:, 2] << 20)).astype(np.uint32)


def vertices(mesh):
    n = len(mesh.positions)
    out = np.zeros(n, dtype=[("p", "<f4", 3), ("n", "<u4"), ("t", "<u4"), ("uv", "<f4", 2), ("bake", "<u4")])
    out["p"] = mesh.positions * MM
    out["n"] = pack_snorm(mesh.normals)
    out["t"] = pack_snorm(mesh.tangents)
    out["uv"] = mesh.uvs
    b = np.clip(np.rint(mesh.bake * 255), 0, 255).astype(np.uint32)
    out["bake"] = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16) | (b[:, 3] << 24)
    return out


def wound_outward(mesh):
    """Every builder leaves its own winding. What counts is the normal each vertex carries, so each face is wound to agree with it."""
    tri = mesh.indices.reshape(-1, 3).copy()
    p = mesh.positions.astype(np.float64)
    n = mesh.normals.astype(np.float64)
    facing = np.cross(p[tri[:, 1]] - p[tri[:, 0]], p[tri[:, 2]] - p[tri[:, 0]])
    wrong = (facing * (n[tri[:, 0]] + n[tri[:, 1]] + n[tri[:, 2]])).sum(-1) < 0
    tri[wrong] = tri[wrong][:, ::-1]
    mesh.indices = tri.ravel().astype(np.uint32)
    return mesh


def write(parts, path):
    blob_v, blob_i, header = [], [], []
    v_at, i_at = 0, 0
    for p in parts:
        mesh = wound_outward(p["mesh"])
        v = vertices(mesh)
        i = mesh.indices.astype("<u4")
        name = p["name"].encode()[:31].ljust(32, b"\0")
        lo, hi = mesh.positions.min(0) * MM, mesh.positions.max(0) * MM
        pivot = np.asarray(p["pivot"], dtype=np.float64) * MM
        axis = np.asarray(p["axis"], dtype=np.float64)
        # Travel of sliding parts is in millimetres in the source, metres in the file. Turns stay in radians.
        slides = p["name"].startswith(("key_", "pill_", "home", "mute", "coil_", "cone"))
        assert np.isfinite(mesh.positions).all() and np.isfinite(mesh.normals).all(), p["name"]
        travel = np.asarray(p["travel"], dtype=np.float64) * (MM if slides else 1.0)
        header.append(name + struct.pack("<IIIIii", len(v), len(i), v_at, i_at, MATERIALS.index(p["material"]), PASSES[p["pass"]]) +
                      struct.pack("<3f3f2fi3f3f", *pivot, *axis, *travel, p.get("parent", -1), *lo, *hi))
        blob_v.append(v.tobytes())
        blob_i.append(i.tobytes())
        v_at += len(v)
        i_at += len(i)
    with open(path, "wb") as f:
        f.write(b"HSM1" + struct.pack("<III", len(parts), v_at, i_at))
        f.write(b"".join(header))
        f.write(b"".join(blob_v))
        f.write(b"".join(blob_i))
    print(f"{len(parts)} parts, {v_at} vertices, {i_at // 3} triangles, {os.path.getsize(path) / 1e6:.1f} MB")


def build(cell):
    t = time.time()
    shell, hollow = body.meshes(cell)
    # The wall's strength at each point, for the light crossing it.
    p = shell.positions.astype(np.float64)
    shell.bake[:, 1] = np.clip(body.inner(p) / 10.0, 0.02, 1.0)
    hollow.bake[:, 1] = 0.26
    parts = [part("body", shell, "ACRYLIC", "clear"), part("body_inner", hollow, "ACRYLIC", "clear_back")]
    parts += mechanism.all_parts()
    parts += controls.all_parts()
    parts += crew.all_parts()
    for piece in parts:
        if piece["name"] == "dome":
            piece["mesh"].bake[:, 1] = 0.22
    print(f"built in {time.time() - t:.1f} s")
    return parts


def main():
    out = sys.argv[1]
    cell = float(sys.argv[sys.argv.index("--cell") + 1]) if "--cell" in sys.argv else 0.7
    parts = build(cell)
    write(parts, out)
    for piece in parts:
        print(f"  {piece['name']:22} {piece['mesh'].triangle_count():8} tris  {piece['material']}")


if __name__ == "__main__":
    main()
