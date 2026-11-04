# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""G4: geometry. No moving part may touch a neighbour anywhere in its travel, no stray islands or inside-out faces, the object stays in
frame in the fixed views, and the crew keep spread out. Usage: g4.py <app binary>."""
import os
import subprocess
import sys
import numpy as np
from scipy.spatial import cKDTree
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "model"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from instrument import body, controls, crew, mechanism  # noqa: E402
from instrument import geometry as g  # noqa: E402
from instrument.export import build, wound_outward  # noqa: E402
import views as v  # noqa: E402

GAP = 0.15        # millimetres between parts that move against each other
MOVING = {"stick_l_cap": ("tilt", -1), "stick_r_cap": ("tilt", 1), "stick_l_stem": ("tilt", -1), "stick_r_stem": ("tilt", 1), "cross": ("rock", 0), "key_west": ("press", 0), "key_south": ("press", 0), "key_east": ("press", 0), "key_north": ("press", 0)}


def islands(mesh):
    """How many separate pieces a mesh has."""
    n = len(mesh.positions)
    parent = np.arange(n)

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    # Welded by position: a lathe keeps two vertices at a hard corner.
    rounded = np.round(mesh.positions, 3)
    _, first = np.unique(rounded, axis=0, return_inverse=True)
    tri = mesh.indices.reshape(-1, 3)
    for a, b, c in first[tri]:
        ra, rb, rc = find(a), find(b), find(c)
        parent[rb] = ra
        parent[find(c)] = ra
    return len({find(i) for i in np.unique(first)})


def inside_out(mesh):
    """Faces whose winding disagrees with the normals they carry, after the exporter's own fix: must be zero."""
    tri = mesh.indices.reshape(-1, 3)
    p, n = mesh.positions.astype(np.float64), mesh.normals.astype(np.float64)
    face = np.cross(p[tri[:, 1]] - p[tri[:, 0]], p[tri[:, 2]] - p[tri[:, 0]])
    return int(np.sum(np.sum(face * (n[tri[:, 0]] + n[tri[:, 1]] + n[tri[:, 2]]), axis=1) < -1e-9))


def moved(mesh, how, amount, pivot, travel):
    m = np.eye(4)
    if how == "press":
        m = g.translate(0, -travel * amount, 0)
    elif how == "rock":
        m = g.translate(*pivot) @ g.rotate_x(np.degrees(0.07) * amount) @ g.rotate_z(np.degrees(0.07) * amount * 0.7) @ g.translate(-pivot[0], -pivot[1], -pivot[2])
    elif how == "tilt":
        m = g.translate(*pivot) @ g.rotate_x(np.degrees(0.36) * amount) @ g.translate(-pivot[0], -pivot[1], -pivot[2])
    return (mesh.positions @ m[:3, :3].T) + m[:3, 3]


def surface_points(mesh, spacing=0.25):
    """Vertices and face centres; dense enough at this scale to find any contact against a neighbour."""
    tri = mesh.indices.reshape(-1, 3)
    centres = mesh.positions[tri].mean(1)
    return np.vstack([mesh.positions, centres])


def main():
    binary = sys.argv[1]
    parts = build(0.8)
    failures = []
    by_name = {p["name"]: p for p in parts}
    # Static checks.
    many = 0
    for p in parts:
        mesh = wound_outward(p["mesh"])
        wrong = inside_out(mesh)
        if p["pass"] == "solid" and wrong:
            many += 1
            failures.append(f"{p['name']}: {wrong} faces inside out")
    print(f"inside-out faces: {many} parts")
    for name in ("body", "body_inner", "plate", "cross", "dome"):
        count = islands(by_name[name]["mesh"])
        print(f"{name}: {count} piece(s)")
        if count > 1:
            failures.append(f"{name}: stray island, {count} pieces")
    # Controls against the clear walls and each other, over their whole travel.
    walls = np.vstack([surface_points(by_name["body_inner"]["mesh"]), surface_points(by_name["body"]["mesh"])])
    tree = cKDTree(walls)
    for name, (how, sign) in MOVING.items():
        p = by_name[name]
        for amount in (0, 0.5, 1.0) if how == "press" else (-1, -0.5, 0, 0.5, 1):
            at = moved(p["mesh"], how, amount, p["pivot"], abs(p["travel"][1]))
            # Only the part of it that sits in a well can touch the wall; a real gap shows as a positive distance outside the wall's own surface.
            d, _ = tree.query(at, k=1)
            near = float(d.min())
            print(f"{name:14} {how} {amount:+.1f}: nearest wall {near:.2f} mm")
            if near < GAP:
                failures.append(f"{name} at {amount}: only {near:.2f} mm from the wall")
    # Every moving part against every other part, not just the walls, at rest and at both ends of its travel: a press must never go through anything.
    def family(name):
        for root in ("key_west", "key_south", "key_east", "key_north", "cross", "stick_l", "stick_r", "pill_l", "pill_r", "home", "mute", "coil_l", "coil_r", "shoulder_l", "shoulder_r"):
            if name.startswith(root):
                return root
        return name
    # A pedal and its sector share one axle and turn as one; a key's cup carries its face and marks.
    together = {("trigger_l", "gear_l_sector"), ("trigger_r", "gear_r_sector")}
    points = {p["name"]: surface_points(p["mesh"]).astype(np.float64) for p in parts if not p["name"].startswith("crew_") and p["name"] != "ball"}
    trees = {name: cKDTree(q) for name, q in points.items()}
    boxes = {name: (q.min(0), q.max(0)) for name, q in points.items()}

    def turned(q, pivot, axis, angle):
        c, s = np.cos(angle), np.sin(angle)
        r = {"x": np.array([[1, 0, 0], [0, c, -s], [0, s, c]]), "z": np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])}[axis]
        return (q - pivot) @ r.T + pivot

    def poses(p):
        q, name, pivot = points[p["name"]], p["name"], np.array(p["pivot"], dtype=np.float64)
        limit = p["travel"][1] if "travel" in p else 0
        if name.startswith(("key_", "pill_", "mute")) or name == "home":
            return [q, q - np.array([0, abs(limit), 0])]
        if name.startswith("cross"):
            return [q] + [turned(q, pivot, axis, sign * abs(limit)) for axis in "xz" for sign in (-1, 1)]
        if name.startswith("stick_") and ("cap" in name or "stem" in name):
            return [q] + [turned(q, pivot, axis, sign * 0.36) for axis in "xz" for sign in (-1, 1)]
        if name.startswith(("trigger_", "shoulder_")):
            return [q, turned(q, pivot, "x", limit)]
        if name.startswith(("coil_l_magnet", "coil_r_magnet")):
            return [q + np.array([0, 0, sign * abs(limit)]) for sign in (-1, 0, 1)]
        return []

    clashes = 0
    for p in parts:
        if p["name"] not in points:
            continue
        for q in poses(p):
            for other, tree in trees.items():
                if family(other) == family(p["name"]) or (p["name"], other) in together:
                    continue
                if np.any(q.max(0) + GAP < boxes[other][0]) or np.any(boxes[other][1] + GAP < q.min(0)):
                    continue
                near = float(tree.query(q, k=1)[0].min())
                if near < GAP:
                    clashes += 1
                    failures.append(f"{p['name']} comes within {near:.2f} mm of {other}")
    print(f"moving parts against every part, at the ends of their travel: {clashes} too close")
    # The whole object in the frame, in every fixed view with no close-up, whatever the grip.
    points = by_name["body"]["mesh"].positions[::40].astype(np.float64) * 0.001
    eye_default = np.array([float(t) for t in v.DEFAULT_EYE.split(",")])
    worst = 1.0
    for name, view in v.VIEWS:
        if name.startswith("close"):
            continue
        pitch, yaw, roll = [np.radians(float(t)) for t in view["hold"].split(",")]
        r = g.rotate_y(np.degrees(yaw)) @ g.rotate_x(np.degrees(pitch)) @ g.rotate_z(np.degrees(roll))
        world = points @ r[:3, :3].T
        eye, target, fov = eye_default[:3], eye_default[3:6], eye_default[6]
        f = (target - eye) / np.linalg.norm(target - eye)
        s = np.cross(f, (0, 1, 0))
        s /= np.linalg.norm(s)
        u = np.cross(s, f)
        rel = world - eye
        depth = rel @ f
        nx = (rel @ s) / (depth * np.tan(np.radians(fov) / 2) * 2560 / 1664)
        ny = (rel @ u) / (depth * np.tan(np.radians(fov) / 2))
        # How much of the box round the object on screen lies inside the frame.
        box = (nx.max() - nx.min()) * (ny.max() - ny.min())
        kept = (min(nx.max(), 1) - max(nx.min(), -1)) * (min(ny.max(), 1) - max(ny.min(), -1))
        worst = min(worst, float(kept / box))
    print(f"object's box in frame, worst view: {worst * 100:.1f}% (at least 95)")
    if worst < 0.95:
        failures.append("the object leaves the frame")
    # Crew spread out over a whole tour.
    out = subprocess.run([binary, "--test", "experience", "--script", "tour", "--seconds", "300"], capture_output=True, text=True).stdout
    line = [x for x in out.splitlines() if "longest cluster" in x][0]
    cluster = float(line.split("longest cluster")[1].split()[0])
    print(f"longest time four of the crew stood within 15 mm: {cluster:.1f} s (at most 5)")
    if cluster > 5:
        failures.append("the crew crowd together")
    for f in failures:
        print("FAIL", f)
    print(f"{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
