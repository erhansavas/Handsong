# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Everything a hand touches. Each control maps one to one onto the real one under the player's fingers."""
import numpy as np
from . import body as b
from . import geometry as g


def part(name, mesh, material, parts_pass="solid", pivot=(0, 0, 0), axis=(0, 1, 0), travel=(0, 0)):
    return {"name": name, "mesh": mesh, "material": material, "pass": parts_pass, "pivot": pivot, "axis": axis, "travel": travel}


def rotate_about(mesh, pivot, matrix):
    return mesh.transformed(g.translate(*pivot) @ matrix @ g.translate(-pivot[0], -pivot[1], -pivot[2]))


def key_cup(x, z):
    """A woodwind key: a brass cup with a rolled lip, and a ceramic face in it with raised dots to read by touch."""
    top = b.top_y(x, z) + 1.2
    cup = g.profile_with_rounds([(0.0, -5.0), (3.8, -5.0), (3.8, -1.2), (5.1, -0.9), (5.3, -0.2), (5.05, 0.25), (4.55, 0.3), (4.45, -0.15), (0.0, -0.15)],
                                [0, 0.2, 0.25, 0.25, 0.22, 0.14, 0.1, 0, 0], steps=4)
    return g.lathe(cup, 96).transformed(g.translate(x, top, z)), top


def key_face(x, z, top):
    face = g.profile_with_rounds([(4.45, -0.25), (4.40, 0.02), (2.2, 0.16), (0.0, 0.20)], [0, 0.1, 2.0, 0], steps=6)
    mesh = g.lathe(face, 80).transformed(g.translate(x, top, z))
    return mesh


def key_dots(x, z, top, dots):
    """The key's mark, in dark enamel on the ivory: one to four dots, big enough to read from across a room and raised enough to find by touch."""
    spread = {1: [(0, 0)], 2: [(-1.35, 0), (1.35, 0)], 3: [(-1.45, 0.85), (1.45, 0.85), (0, -1.45)], 4: [(-1.3, -1.3), (1.3, -1.3), (-1.3, 1.3), (1.3, 1.3)]}[dots]
    dot = g.lathe(g.profile_with_rounds([(0.78, -0.1), (0.76, 0.12), (0.0, 0.24)], [0, 0.12, 0], steps=3), 32)
    return g.merge([dot.transformed(g.translate(x + dx, top + 0.10, z + dz)) for dx, dz in spread])


def cross_part():
    """One rigid piece of steel on a ball: it rocks as a whole, as such a cross does."""
    x0, z0 = b.CROSS_AT
    top = b.top_y(x0, z0) + 1.4
    center = np.array([x0, top - 3.0, z0])

    def sdf(p):
        arm_x = g.rounded_box(p, center, (12.6, 3.0, 4.1), 1.0)
        arm_z = g.rounded_box(p, center, (4.1, 3.0, 12.6), 1.0)
        shape = g.smooth_min(arm_x, arm_z, 1.0)
        dish = g.sphere(p, center + np.array([0, 41.5, 0]), 39.0)
        return g.smooth_max(shape, -dish, 0.5)

    low = center - np.array([14.0, 4.0, 14.0])
    high = center + np.array([14.0, 4.0, 14.0])
    mesh = g.surface_nets(sdf, low, high, 0.32)
    pivot = (x0, top - 7.0, z0)
    # Blued, like the instrument's springs and rollers, so it reads against the brass plate. A brass boss sits in the dish, and each arm carries a brass point to read by touch.
    boss = g.lathe(g.profile_with_rounds([(3.5, -0.5), (3.5, 0.10), (0.0, 0.26)], [0, 0.25, 0], steps=4), 64).transformed(g.translate(x0, top - 3.0 + 2.5, z0))

    def point_mesh(dx, dz):
        tip = center + np.array([dx * 9.6, 3.0, dz * 9.6])
        ax, az = dx, dz       # along the arm, outward
        side = np.array([az, 0.0, ax])

        def point(p):
            v = p - tip
            along = v[..., 0] * ax + v[..., 2] * az
            across = np.abs(v[..., 0] * side[0] + v[..., 2] * side[2])
            flat = np.maximum(np.maximum(along * 0.7071 + across * 0.7071 - 0.95, -along - 0.9), np.abs(v[..., 1]) - 0.22)
            return flat

        return g.surface_nets(point, tip - 2.2, tip + 2.2, 0.1)

    marks = g.merge([boss] + [point_mesh(dx, dz) for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1))])
    return [part("cross", mesh, "STEEL_BLUE", pivot=pivot, travel=(0, 0.07)), part("cross_marks", marks, "BRASS_BRUSHED", pivot=pivot, travel=(0, 0.07))]


def knurled_lathe(profile, band, segments=160):
    """A turned part whose knurl lives in the shader, marked here in the bake's third channel over one band of the profile."""
    mesh = g.lathe(profile, segments)
    y = mesh.positions[:, 1].astype(np.float64)
    mesh.bake[:, 2] = np.clip(np.minimum(y - band[0], band[1] - y) / 0.35, 0, 1)
    return mesh


def stick(side, x, z):
    top = b.top_y(x, z) + 7.5
    pivot = (x, top - 15.0, z)
    profile = g.profile_with_rounds(
        [(0.0, -1.6), (5.8, -1.75), (7.6, -1.55), (8.75, -0.55), (9.1, 0.4), (9.1, 1.6), (8.5, 2.5), (7.4, 2.75), (5.9, 2.15), (3.0, 1.55), (0.0, 1.35)],
        [0, 1.0, 0.8, 0.5, 0.25, 0.35, 0.25, 0.5, 1.8, 2.0, 0], steps=6)
    cap = knurled_lathe(profile, (0.5, 1.6)).transformed(g.translate(x, top, z))
    stem_profile = g.profile_with_rounds([(0.0, -2.0), (3.0, -2.0), (3.0, -6.0), (3.6, -7.0), (5.4, -8.0), (5.4, -9.4), (2.8, -10.0), (2.4, -15.5), (0.0, -15.5)],
                                         [0, 0.3, 0.6, 0.5, 0.3, 0.4, 0.4, 0.2, 0], steps=4)
    stem = g.lathe(stem_profile, 72).transformed(g.translate(x, top, z))
    return [part(f"stick_{side}_cap", cap, "KNURLED", pivot=pivot, travel=(0, 0.36)),
            part(f"stick_{side}_stem", stem, "STEEL_BRUSHED", pivot=pivot, travel=(0, 0.36))]


def stick_gimbal(x, z):
    """The ring a stick swings in, jewelled like a watch's pivots, seen through the clear body."""
    top = b.top_y(x, z) + 7.5
    y = top - 17.0
    ring = g.profile_with_rounds([(9.2, -0.9), (11.6, -0.9), (11.6, 0.9), (9.2, 0.9), (9.2, -0.9)], 0.25, steps=3)
    mesh = g.lathe(ring, 96).transformed(g.translate(x, y, z))
    jewels = []
    for a in (0, 90, 180, 270):
        jewel = g.lathe(g.profile_with_rounds([(0.95, -0.2), (0.95, 0.15), (0.4, 0.5), (0.0, 0.55)], [0, 0.1, 0.3, 0], steps=4), 32)
        c, s = np.cos(np.radians(a)), np.sin(np.radians(a))
        jewels.append(jewel.transformed(g.translate(x + 10.4 * c, y + 0.92, z + 10.4 * s)))
    return mesh, g.merge(jewels)


def home_key():
    x, z = b.HOME_AT
    top = b.top_y(x, z) + 0.9
    gem = g.profile_with_rounds([(2.65, -0.6), (2.75, -0.1), (2.2, 0.75), (1.1, 1.25), (0.0, 1.4)], [0, 0.2, 0.8, 0.8, 0], steps=6)
    bezel = g.profile_with_rounds([(2.65, -1.2), (3.05, -1.2), (3.1, 0.1), (2.75, 0.3), (2.65, -0.2)], 0.1, steps=3)
    return [part("home", g.lathe(gem, 64).transformed(g.translate(x, top, z)), "RUBY", pivot=(x, top, z), axis=(0, -1, 0), travel=(0, 0.9)),
            part("home_bezel", g.lathe(bezel, 64).transformed(g.translate(x, top, z)), "BRASS_POLISHED", pivot=(x, top, z), axis=(0, -1, 0), travel=(0, 0.9))]


def small_slab(name, x, z, half, material, travel=1.2, cell=0.2):
    top = b.top_y(x, z) + 0.7
    center = np.array([x, top - 1.0, z])

    def sdf(p):
        return g.rounded_box(p, center, half, min(half[0], half[2]) * 0.9)

    mesh = g.surface_nets(sdf, center - np.array(half) - 0.8, center + np.array(half) + 0.8, cell)
    return part(name, mesh, material, pivot=tuple(center), axis=(0, -1, 0), travel=(0, travel))


def trigger(side, sign):
    """A long curved pedal under the far edge: brushed steel on a real axle, driven through a visible gear train."""
    axle = np.array([sign * 62.0, -13.0, -16.0])
    center = axle + np.array([0.0, -5.5, -15.5])

    def sdf(p):
        d = g.rounded_box(p, center, (15.0, 7.5, 3.2), 2.5)
        curve = g.sphere(p, center + np.array([0, 1.0, -55.0]), 54.0)      # the finger's hollow
        arm = g.rounded_box(p, axle + np.array([0, -2.6, -6.5]), (4.4, 2.0, 7.5), 1.5)
        return g.smooth_min(g.smooth_max(d, -curve, 1.0), arm, 2.0)

    mesh = g.surface_nets(sdf, center - np.array([17.0, 11.0, 12.0]), center + np.array([17.0, 11.0, 24.0]), 0.42)
    return part(f"trigger_{side}", mesh, "STEEL_BRUSHED", pivot=tuple(axle), axis=(1, 0, 0), travel=(0, -0.48))


def shoulder(side, sign):
    """A slim roller of blued steel above the pedal. Only what has real hardware gets more than that."""
    center = np.array([sign * 62.0, -2.4, -27.5])
    profile = g.profile_with_rounds([(0.0, -12.0), (2.9, -12.0), (3.3, -11.0), (3.3, 11.0), (2.9, 12.0), (0.0, 12.0)], [0, 0.4, 0.5, 0.5, 0.4, 0], steps=4)
    mesh = g.lathe(profile, 64).transformed(g.translate(*center) @ g.rotate_z(90))
    return part(f"shoulder_{side}", mesh, "STEEL_BLUE", pivot=(center[0], center[1] + 0.2, center[2] + 7.0), axis=(1, 0, 0), travel=(0, -0.09))


def all_parts():
    parts = []
    key_at = b.KEYS_AT
    for name, dx, dz, dots in (("west", -10.5, 0.0, 3), ("south", 0.0, 10.5, 1), ("east", 10.5, 0.0, 2), ("north", 0.0, -10.5, 4)):
        x, z = key_at[0] + dx, key_at[1] + dz
        cup, top = key_cup(x, z)
        parts.append(part(f"key_{name}", cup, "BRASS_POLISHED", pivot=(x, top, z), axis=(0, -1, 0), travel=(0, 1.4)))
        parts.append(part(f"key_{name}_face", key_face(x, z, top), "CERAMIC", pivot=(x, top, z), axis=(0, -1, 0), travel=(0, 1.4)))
        parts.append(part(f"key_{name}_dots", key_dots(x, z, top, dots), "RUBBER", pivot=(x, top, z), axis=(0, -1, 0), travel=(0, 1.4)))
    parts += cross_part()
    parts += stick("l", *b.LEFT_STICK_AT)
    parts += stick("r", *b.RIGHT_STICK_AT)
    rings, jewels = [], []
    for at in (b.LEFT_STICK_AT, b.RIGHT_STICK_AT):
        ring, jewel = stick_gimbal(*at)
        rings.append(ring)
        jewels.append(jewel)
    parts.append(part("gimbal_rings", g.merge(rings), "BRASS_BRUSHED"))
    parts.append(part("gimbal_jewels", g.merge(jewels), "RUBY"))
    parts += home_key()
    parts.append(small_slab("mute", b.MUTE_AT[0], b.MUTE_AT[1], (3.0, 1.3, 1.4), "BRASS_BRUSHED"))
    # The controller's lights, on the dome's ring where every close shot can see them: five lamps in a row along its front, and the microphone's beside them.
    def on_ring(name, degrees, radius, material):
        at = np.radians(degrees)
        c = np.array([29.6 * np.sin(at), 15.4, -10.0 + 29.6 * np.cos(at)])
        lamp = g.surface_nets(lambda p, c=c: g.sphere(p, c, radius), c - radius - 0.5, c + radius + 0.5, 0.12)
        return part(name, lamp, material)
    for i in range(5):
        parts.append(on_ring(f"player_light_{i}", (i - 2) * 6.0, 1.0, "RUBBER"))
    parts.append(on_ring("mic_lamp", 22.0, 1.0, "RUBY"))
    parts.append(small_slab("pill_l", -b.PILL_AT[0], b.PILL_AT[1], (1.5, 1.3, 4.0), "BRASS_POLISHED"))
    parts.append(small_slab("pill_r", b.PILL_AT[0], b.PILL_AT[1], (1.5, 1.3, 4.0), "BRASS_POLISHED"))
    for side, sign in (("l", -1), ("r", 1)):
        parts.append(trigger(side, sign))
        parts.append(shoulder(side, sign))
    return parts
