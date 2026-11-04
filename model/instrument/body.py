# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""The instrument's cast clear body: three pebbles around a dome, wells for its controls, hollow inside."""
import numpy as np
from . import geometry as g

WALL = 2.6                                  # the moulded wall, thicker than a lid of this size needs, for its look
DOME_CENTER = (0.0, 13.0, -10.0)
DOME_RADIUS = 31.0
LOBE_AT = (56.0, 6.0)                       # the two side pebbles, in x, z
CROSS_AT = (-63.0, -6.0)
KEYS_AT = (63.0, -6.0)
LEFT_STICK_AT = (-41.0, 21.0)
RIGHT_STICK_AT = (41.0, 21.0)
PILL_AT = (33.5, 8.5)
HOME_AT = (-10.5, 25.5)
MUTE_AT = (10.5, 25.5)


def top_y(x, z):
    """Where the face lies, for placing controls: found on the solid itself by walking down from above."""
    p = np.array([[x, 30.0, z]], dtype=np.float64)
    for _ in range(40):
        p[0, 1] -= float(solid(p)[0])
    return float(p[0, 1])


def silhouette(x, z):
    """Three overlapping rounds seen from above, joined by soft necks: the outline of the object, in millimetres from its edge."""
    center = np.hypot(x, z + 8.0) - 41.0
    right = np.hypot(x - LOBE_AT[0], z - LOBE_AT[1]) - 34.0
    left = np.hypot(x + LOBE_AT[0], z - LOBE_AT[1]) - 34.0
    return g.smooth_min(center, np.minimum(right, left), 16.0)


def solid(p):
    """A cast slab with that outline: polished, nearly flat faces that keep the interior sharp, and a generous rolled edge.
    Below each side a palm swell fills the hand: an ocarina's language, nothing of a game pad's horns."""
    x, y, z = p[..., 0], p[..., 1], p[..., 2]
    flat = silhouette(x, z)
    crown = (x * x + (z + 4.0) ** 2) / (2.0 * 900.0)
    top, bottom = 11.5 - crown, -13.0 + crown * 0.5
    mid, half = (top + bottom) / 2, (top - bottom) / 2
    edge = np.where(y > mid, 4.2, 7.5)
    h = np.abs(y - mid) - half + edge
    w = flat + edge
    slab = np.hypot(np.maximum(w, 0), np.maximum(h, 0)) + np.minimum(np.maximum(w, h), 0) - edge
    palms = np.minimum(g.ellipsoid(p, (LOBE_AT[0] + 3.0, -12.0, 12.0), (27.0, 15.0, 27.0)),
                       g.ellipsoid(p, (-LOBE_AT[0] - 3.0, -12.0, 12.0), (27.0, 15.0, 27.0)))
    return g.smooth_min(slab, palms, 10.0)


def wells(p):
    """Everything cut from the face. Each cut blends into the surface, as a moulded recess does. Each is deep enough that its control
    can travel all the way in and still clear the floor."""
    yy = 30.0
    deep = 27.5       # the floors lie at y = 2.5
    cross = np.minimum(g.rounded_box(p, (CROSS_AT[0], yy, CROSS_AT[1]), (13.4, deep, 4.8), 1.2),
                       g.rounded_box(p, (CROSS_AT[0], yy, CROSS_AT[1]), (4.8, deep, 13.4), 1.2))
    d = g.cylinder_y(p, (KEYS_AT[0], yy, KEYS_AT[1] - 10.5), 5.6, deep, 0.3)
    for dx, dz in ((10.5, 0.0), (-10.5, 0.0), (0.0, 10.5)):
        d = np.minimum(d, g.cylinder_y(p, (KEYS_AT[0] + dx, yy, KEYS_AT[1] + dz), 5.6, deep, 0.3))
    d = np.minimum(d, cross)
    d = np.minimum(d, g.cylinder_y(p, (LEFT_STICK_AT[0], yy, LEFT_STICK_AT[1]), 11.0, 25.0, 0.3))
    d = np.minimum(d, g.cylinder_y(p, (RIGHT_STICK_AT[0], yy, RIGHT_STICK_AT[1]), 11.0, 25.0, 0.3))
    d = np.minimum(d, g.cylinder_y(p, (DOME_CENTER[0], yy, DOME_CENTER[2]), DOME_RADIUS - 0.8, 28.0, 0.3))
    for x in (-PILL_AT[0], PILL_AT[0]):
        d = np.minimum(d, g.rounded_box(p, (x, yy, PILL_AT[1]), (2.0, 23.5, 4.5), 1.6))
        # Each shoulder roller and each pedal's arm comes out through the back wall by its own window, with room for their whole swing.
        side = 62.0 if x > 0 else -62.0
        d = np.minimum(d, g.rounded_box(p, (side, -2.4, -32.0), (12.7, 4.6, 8.5), 1.5))
        d = np.minimum(d, g.rounded_box(p, (side, -16.0, -26.0), (5.4, 9.5, 14.5), 1.5))
    d = np.minimum(d, g.cylinder_y(p, (HOME_AT[0], yy, HOME_AT[1]), 3.2, 22.0, 0.3))
    d = np.minimum(d, g.rounded_box(p, (MUTE_AT[0], yy, MUTE_AT[1]), (3.4, 22.0, 1.8), 1.2))
    return d


def bores(p):
    """What goes right through the wall: each stick's stem passes down through the floor of its well into the mechanism."""
    d = g.cylinder_y(p, (LEFT_STICK_AT[0], 0.0, LEFT_STICK_AT[1]), 7.2, 9.0, 0.2)
    return np.minimum(d, g.cylinder_y(p, (RIGHT_STICK_AT[0], 0.0, RIGHT_STICK_AT[1]), 7.2, 9.0, 0.2))


def outer(p):
    return g.smooth_max(solid(p), -wells(p), 1.2)


def inner(p):
    """The hollow: the same form, moved in by the wall, with the wells' walls standing as tubes."""
    return g.smooth_max(solid(p) + WALL, -(wells(p) - WALL * 0.8), 2.2)


def meshes(cell=0.7):
    """One surface for all of the wall's material, so that a well's sleeve really opens into the hollow where a stem goes through. It is
    then split in two by which side each face belongs to: the outside, and the hollow's walls seen from within."""
    low, high = (-98.0, -32.0, -56.0), (98.0, 20.0, 50.0)

    def material(p):
        return g.smooth_max(g.smooth_max(outer(p), -inner(p), 0.25), -bores(p), 0.25)

    def split(whole, want_hollow):
        tri = whole.indices.reshape(-1, 3)
        centre = whole.positions[tri].mean(1).astype(np.float64)
        # Inside the hollow's boundary and not on a bore: a face of the far wall.
        on_hollow = np.abs(inner(centre)) < np.abs(outer(centre))
        keep = on_hollow if want_hollow else ~on_hollow
        part = g.Mesh(whole.positions, whole.normals, tri[keep].ravel(), whole.tangents, whole.uvs)
        part.bake = whole.bake.copy()
        part = compact(part)
        # At the far wall's coarser cells a corner can pinch off a crumb, which nobody could see and nothing should draw.
        return compact(g.largest_piece(part)) if want_hollow else part

    # The far walls are only ever seen through the near ones, faintly, so they need far fewer faces.
    return split(g.surface_nets(material, low, high, cell), False), split(g.surface_nets(material, low, high, cell * 1.7), True)


def compact(mesh):
    """Only the vertices its faces really use."""
    used, remap = np.unique(mesh.indices, return_inverse=True)
    out = g.Mesh(mesh.positions[used], mesh.normals[used], remap.astype(np.uint32), mesh.tangents[used], mesh.uvs[used])
    out.bake = mesh.bake[used]
    return out
