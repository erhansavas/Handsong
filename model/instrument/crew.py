# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""The crew: six small technicians built from the instrument's own materials, each with its own silhouette and trade.
Every part is rigid and rides on one bone, like the machines they are. A head turns about its own centre, so the one eye
never leaves its face. src/world/crew.c holds the same joint positions, and the two must change together."""
import numpy as np
from . import geometry as g

BODY, HEAD, ARM_L, ARM_R, LEG_L, LEG_R, EXTRA = range(7)
PIP, TAM, MOX, OLA, BO, WREN = range(6)
SCALE = 0.82


def turned(points, rounds=0.2, segments=48, steps=3):
    """Points go from the axis at the bottom, up the outside, to the axis at the top, so that faces look outward."""
    return g.lathe(g.profile_with_rounds(points, rounds, steps=steps), segments)


def rod(length, radius, segments=16, tip=None):
    """A limb hanging from the origin down along -y, with a rounded end and a small boss at its joint."""
    tip = radius * 1.15 if tip is None else tip
    return turned([(0.0, -length), (tip, -length), (tip, -length + tip), (radius, -length + tip * 2.2), (radius, -0.5), (radius * 1.7, -0.3), (radius * 1.7, 0.3), (0.0, 0.45)], 0.12, segments, 2)


def eye(radius, at):
    """The eye: a brass rim, a copper iris and a black glossy pupil, looking along +z from a point `at` millimetres before the head's centre."""
    rim = turned([(radius * 0.80, -radius * 0.5), (radius * 1.10, -radius * 0.42), (radius * 1.14, radius * 0.1), (radius * 0.96, radius * 0.28), (radius * 0.80, radius * 0.10), (radius * 0.80, -radius * 0.1)], 0.06, 48, 2)
    iris = turned([(0.0, -radius * 0.1), (radius * 0.84, -radius * 0.1), (radius * 0.84, 0.0), (radius * 0.5, radius * 0.06), (0.0, radius * 0.06)], 0.02, 40, 1)
    pupil = turned([(radius * 0.42, radius * 0.05), (radius * 0.3, radius * 0.2), (0.0, radius * 0.27)], 0.2, 28, 3)
    place = g.translate(0, 0, at) @ g.rotate_x(90)
    return rim.transformed(place), iris.transformed(place), pupil.transformed(place)


def add(parts, who, bone, name, mesh, material):
    parts.append({"name": f"crew_{who}_{bone}_{name}"[:31], "mesh": mesh, "material": material, "pass": "solid", "pivot": (0, 0, 0), "axis": (0, 1, 0),
                  "travel": (0, 0), "parent": who * 16 + bone})


def add_eye(parts, who, radius, at, rim_material="BRASS_POLISHED"):
    rim, iris, pupil = eye(radius, at)
    add(parts, who, HEAD, "rim", rim, rim_material)
    add(parts, who, HEAD, "iris", iris, "CERAMIC")
    add(parts, who, HEAD, "pupil", pupil, "LENS")


def rivets(radius, y, count, size=0.28, offset=0.0):
    head = g.lathe(g.profile_with_rounds([(0.0, -0.1), (size, -0.1), (size, 0.05), (0.0, size * 0.75)], [0, 0.03, 0.12, 0], steps=2), 12)
    pieces = []
    for i in range(count):
        a = (i + offset) * 2 * np.pi / count
        pieces.append(head.transformed(g.translate(radius * np.cos(a), y, radius * np.sin(a)) @ g.rotate_y(-np.degrees(a) + 90) @ g.rotate_x(90)))
    return g.merge(pieces)


def pip(parts):
    """The messenger: a bell on two tuning forks. It hops, and rings where it lands."""
    bell = turned([(0.0, -1.1), (2.9, -1.1), (3.7, -1.7), (3.95, -1.45), (3.0, -0.2), (2.6, 1.4), (1.9, 2.8), (0.55, 3.4), (0.55, 3.9), (0.95, 4.25), (0.0, 4.65)], [0, 0.08, 0.08, 0.2, 0.8, 1.2, 0.9, 0.1, 0.1, 0.3, 0], 56, 4)
    add(parts, PIP, BODY, "bell", bell, "BRASS_POLISHED")
    add_eye(parts, PIP, 1.35, 2.25, "STEEL_BLUE")
    for bone in (LEG_L, LEG_R):
        fork = g.merge([rod(3.5, 0.3).transformed(g.translate(dx, 0, 0)) for dx in (-0.48, 0.48)] +
                       [turned([(0.0, -3.9), (0.95, -3.9), (1.05, -3.55), (0.5, -3.25), (0.0, -3.2)], 0.1, 16, 2)])
        add(parts, PIP, bone, "fork", fork, "STEEL_BRUSHED")


def tam(parts):
    """The machinist: a squat riveted drum on short legs, with thick arms and a spanner in one hand."""
    drum = turned([(0.0, -3.3), (4.1, -3.3), (4.75, -2.95), (4.75, -2.45), (4.35, -2.2), (4.35, 2.2), (4.75, 2.45), (4.75, 2.95), (4.1, 3.3), (1.5, 3.4), (1.3, 3.9), (0.0, 4.0)], 0.12, 64, 2)
    add(parts, TAM, BODY, "drum", drum, "BRASS_BRUSHED")
    add(parts, TAM, BODY, "rivets", g.merge([rivets(4.72, 2.7, 16), rivets(4.72, -2.7, 16, offset=0.5)]), "STEEL_BLUE")
    add_eye(parts, TAM, 2.2, 3.9)
    for bone, name in ((ARM_L, "l"), (ARM_R, "r")):
        arm = turned([(0.0, -4.9), (1.15, -4.9), (1.25, -4.1), (0.75, -3.5), (0.75, -0.9), (1.1, -0.4), (1.1, 0.4), (0.0, 0.7)], 0.15, 24, 2)
        add(parts, TAM, bone, "arm" + name, arm, "STEEL_BRUSHED")
    def spanner(p):
        handle = g.rounded_box(p, (0.0, -5.0, 2.9), (0.42, 0.22, 2.9), 0.18)
        head = g.cylinder_y(p, (0.0, -5.0, 6.2), 1.45, 0.28, 0.08)
        bite = g.rounded_box(p, (0.0, -5.0, 7.2), (0.62, 0.8, 1.05), 0.08)
        return g.smooth_max(g.smooth_min(handle, head, 0.2), -bite, 0.08)
    add(parts, TAM, ARM_R, "spanner", g.surface_nets(spanner, (-2.0, -6.0, -0.4), (2.0, -4.0, 8.2), 0.14), "STEEL_BLUE")
    for bone in (LEG_L, LEG_R):
        leg = turned([(0.0, -3.5), (1.75, -3.5), (1.85, -3.05), (1.15, -2.5), (1.0, 0.0), (0.0, 0.2)], 0.15, 24, 2)
        add(parts, TAM, bone, "leg", leg, "BRONZE_DARK")


def mox(parts):
    """The winder: a heavy wound coil of a body, walking slowly, with a crank at its side."""
    winding = turned([(2.6, -4.2), (5.9, -4.2), (6.05, -4.05), (6.05, 4.05), (5.9, 4.2), (2.6, 4.2)], 0.08, 72, 2)
    winding.bake[:, 2] = 1
    add(parts, MOX, BODY, "coil", winding, "COPPER")
    flange = turned([(0.0, -5.1), (6.5, -5.1), (6.9, -4.8), (6.5, -4.2), (2.8, -4.2), (2.8, 4.2), (6.5, 4.2), (6.9, 4.8), (6.5, 5.1), (2.2, 5.15), (1.6, 5.9), (0.0, 6.1)], 0.14, 72, 2)
    add(parts, MOX, BODY, "spool", flange, "BRASS_BRUSHED")
    add_eye(parts, MOX, 2.1, 5.8, "STEEL_BRUSHED")
    arm = turned([(0.0, -5.6), (1.1, -5.6), (1.15, -4.9), (0.7, -4.4), (0.7, -0.8), (1.05, -0.4), (1.05, 0.4), (0.0, 0.6)], 0.14, 20, 2)
    add(parts, MOX, ARM_L, "arm", arm, "BRONZE_DARK")
    throw = rod(3.0, 0.38).transformed(g.rotate_z(90))
    grip = turned([(0.0, -2.7), (0.75, -2.7), (0.85, -2.4), (0.75, -0.4), (0.0, -0.2)], 0.15, 16, 2).transformed(g.translate(3.2, 0, 0))
    add(parts, MOX, ARM_R, "crank", g.merge([throw, grip]), "BRASS_POLISHED")
    for bone in (LEG_L, LEG_R):
        leg = turned([(0.0, -3.0), (2.2, -3.0), (2.3, -2.5), (1.35, -1.9), (1.2, 0.0), (0.0, 0.2)], 0.16, 24, 2)
        add(parts, MOX, bone, "leg", leg, "BRONZE_DARK")


def ola(parts):
    """The lamplighter: tall and thin, a lantern on a long neck. Wherever it walks, the light walks too."""
    stalk = turned([(0.0, -1.0), (1.25, -1.0), (1.4, -0.5), (0.9, 0.2), (0.42, 1.2), (0.36, 6.6), (0.7, 7.2), (0.0, 7.6)], 0.1, 24, 2)
    add(parts, OLA, BODY, "stalk", stalk, "STEEL_BLUE")
    cap = turned([(1.5, 1.05), (1.75, 1.15), (0.85, 2.05), (0.4, 2.25), (0.4, 2.6), (0.75, 2.8), (0.0, 3.15)], 0.1, 36, 2)
    base = turned([(0.0, -1.55), (1.55, -1.55), (1.75, -1.3), (1.35, -1.0), (0.0, -1.0)], 0.08, 36, 2)
    add(parts, OLA, HEAD, "cage", g.merge([cap, base]), "BRASS_POLISHED")
    bars = g.merge([turned([(0.0, -1.2), (0.13, -1.2), (0.13, 1.2), (0.0, 1.2)], 0.0, 8, 1).transformed(g.translate(1.32 * np.cos(a), 0, 1.32 * np.sin(a))) for a in np.linspace(0, 2 * np.pi, 6, endpoint=False)])
    add(parts, OLA, HEAD, "bars", bars, "BRASS_BRUSHED")
    flame = turned([(0.0, -0.8), (0.45, -0.55), (0.62, -0.05), (0.38, 0.5), (0.0, 0.95)], 0.3, 24, 3)
    add(parts, OLA, HEAD, "flame", flame, "FILAMENT")
    for bone, name in ((ARM_L, "l"), (ARM_R, "r")):
        add(parts, OLA, bone, "arm" + name, rod(4.6, 0.22), "STEEL_BRUSHED")
    for bone in (LEG_L, LEG_R):
        add(parts, OLA, bone, "leg", rod(5.2, 0.27, tip=0.7), "BRONZE_DARK")


def bo(parts):
    """The balancer: one wheel under a round body, always correcting, the first to feel a tilt."""
    wheel = turned([(3.0, -0.75), (4.2, -0.75), (4.6, -0.3), (4.6, 0.3), (4.2, 0.75), (3.0, 0.75), (3.0, -0.75)], 0.14, 64, 2).transformed(g.rotate_z(90))
    add(parts, BO, EXTRA, "wheel", wheel, "RUBBER")
    hub = turned([(0.0, -0.95), (0.7, -0.95), (1.1, -0.4), (3.05, -0.3), (3.05, 0.3), (1.1, 0.4), (0.7, 0.95), (0.0, 0.95)], 0.1, 40, 2).transformed(g.rotate_z(90))
    add(parts, BO, EXTRA, "hub", hub, "BRASS_BRUSHED")
    for piece in parts:     # small enough to leave the hall a room to work in, and not a crowd
        piece["mesh"] = piece["mesh"].transformed(g.scale(SCALE))
    ball = turned([(0.0, -3.7), (2.6, -2.9), (3.7, -0.9), (3.7, 0.9), (2.6, 2.9), (0.0, 3.7)], 1.5, 56, 5)
    band = turned([(3.5, -0.42), (3.85, -0.32), (3.85, 0.32), (3.5, 0.42)], 0.05, 56, 2)
    fork = g.merge([rod(5.2, 0.34).transformed(g.translate(dx, -2.4, 0)) for dx in (-1.25, 1.25)])
    add(parts, BO, BODY, "ball", ball, "STEEL_BRUSHED")
    add(parts, BO, BODY, "band", band, "BRASS_POLISHED")
    add(parts, BO, BODY, "fork", fork, "BRONZE_DARK")
    add_eye(parts, BO, 1.9, 3.3)
    for bone in (ARM_L, ARM_R):
        add(parts, BO, bone, "arm", rod(3.4, 0.28, tip=0.55), "BRONZE_DARK")


def wren(parts):
    """The inspector: a large magnifying lens carried on four thin legs. It walks the sensing lattice and looks at everything."""
    body = turned([(0.0, -1.5), (2.2, -1.5), (2.55, -1.0), (2.55, 0.1), (1.9, 0.75), (0.0, 0.95)], 0.3, 40, 3)
    add(parts, WREN, BODY, "body", body, "BRONZE_DARK")
    add_eye(parts, WREN, 3.2, 2.7)
    for bone in (ARM_L, ARM_R, LEG_L, LEG_R):
        add(parts, WREN, bone, "leg", rod(6.4, 0.21, tip=0.34), "STEEL_BRUSHED")


def all_parts():
    parts = []
    for make in (pip, tam, mox, ola, bo, wren):
        make(parts)
    for piece in parts:     # small enough to leave the hall a room to work in, and not a crowd
        piece["mesh"] = piece["mesh"].transformed(g.scale(SCALE))
    ball = turned([(0.0, -2.2), (1.56, -1.56), (2.2, 0.0), (1.56, 1.56), (0.0, 2.2)], 1.0, 56, 8)
    parts.append({"name": "ball", "mesh": ball, "material": "STEEL_POLISHED", "pass": "solid", "pivot": (0, 0, 0), "axis": (0, 1, 0), "travel": (0, 0), "parent": 6 * 16})
    return parts
