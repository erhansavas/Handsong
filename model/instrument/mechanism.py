# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""What lives inside: a watchmaker's plate, bobbins, gears, and the dome. Everything a feature of the controller really is,
shown as the honest mechanism it could be. Sizes are this object's own; none is measured from any product."""
import numpy as np
from . import body as b
from . import geometry as g
from .controls import part


def extrude_sdf(sdf_2d, y0, y1, low, high, cell, chamfer=0.25):
    """A flat machined part from an outline given as a 2D distance function, with chamfered edges."""
    mid, half = (y0 + y1) / 2, (y1 - y0) / 2

    def sdf(p):
        flat = sdf_2d(p[..., 0], p[..., 2])
        dy = np.abs(p[..., 1] - mid) - half + chamfer
        outside = np.hypot(np.maximum(flat + chamfer, 0), np.maximum(dy, 0)) - chamfer
        return outside + np.minimum(np.maximum(flat + chamfer, dy), 0)

    return g.surface_nets(sdf, (low[0], y0 - 0.5, low[1]), (high[0], y1 + 0.5, high[1]), cell)


def plate():
    """The main plate: one piece of brass across the object, skeletonised like a watch movement, carrying everything."""
    y = -7.5

    def outline(x, z):
        center = np.hypot(x, z + 10) - 30.0
        lobes = np.minimum(np.hypot(x - 54, z - 6) - 25.0, np.hypot(x + 54, z - 6) - 25.0)
        neck = np.maximum(np.abs(x) - 54, np.abs(z - 2) - 8.0)
        d = np.minimum(np.minimum(center, lobes), neck)
        d = np.maximum(d, -(np.hypot(x, z + 10) - 11.9))
        windows = np.minimum(np.hypot(np.abs(x) - 52, z - 12) - 9.5, np.hypot(np.abs(x) - 60, z + 8) - 7.0)
        windows = np.minimum(windows, np.maximum(np.abs(np.abs(x) - 62) - 10.4, np.abs(z - 12) - 7.4))     # the coil stands up through the plate
        return np.maximum(d, -windows)

    return part("plate", extrude_sdf(outline, y - 1.0, y + 0.2, (-82, -44), (82, 34), 0.74), "BRASS_PERLAGE")


def hall_floor():
    """The bench the crew work on. Four inlaid surfaces for the hands to tell apart, a lid over the speaker at its centre."""
    x, _, z = b.DOME_CENTER
    y = -5.4
    profile = g.profile_with_rounds([(11.8, y - 1.6), (27.2, y - 1.6), (27.2, y - 0.1), (26.6, y), (26.45, y - 0.25), (11.95, y - 0.25), (11.8, y)], 0.06, steps=2)
    pieces = [part("hall_floor", g.lathe(profile, 192).transformed(g.translate(x, 0, z)), "BRASS_BRUSHED")]
    inlay = [(11.9, y - 0.25), (12.0, y - 0.03), (26.4, y - 0.03), (26.5, y - 0.25)]
    # In the order the hands meet them going round: each spans a quarter, with a brass rib between.
    for name, material, a0 in (("floor_glass", "LENS", 0.0), ("floor_gravel", "GRAVEL", 90.0), ("floor_water", "WATER", 180.0), ("floor_wood", "WOOD", 270.0)):
        pieces.append(part(name, g.lathe_arc(inlay, a0 + 0.8, a0 + 89.2, 72).transformed(g.translate(x, 0, z)), material))
    ribs = []
    for a in (0.0, 90.0, 180.0, 270.0):
        c, s_ = np.cos(np.radians(a)), np.sin(np.radians(a))
        ribs.append(g.tube([(x + c * r, y + 0.05, z + s_ * r) for r in np.linspace(11.9, 26.5, 8)], 0.32, 10))
    pieces.append(part("floor_ribs", g.merge(ribs), "BRASS_POLISHED"))
    # The wall round the hall, with its rivets: it gives the crew a room, and the eye a level to read the depth by.
    wall = g.profile_with_rounds([(26.95, y - 1.0), (28.0, y - 1.0), (28.0, y + 2.7), (27.6, y + 3.2), (27.05, y + 3.2), (26.95, y + 2.9), (26.95, y - 1.0)], 0.12, steps=2)
    pieces.append(part("hall_wall", g.lathe(wall, 192).transformed(g.translate(x, 0, z)), "BRONZE_DARK"))
    rivet = g.lathe(g.profile_with_rounds([(0.0, -0.1), (0.42, -0.1), (0.42, 0.05), (0.0, 0.3)], [0, 0.03, 0.2, 0], steps=2), 16)
    studs = [rivet.transformed(g.translate(x + 26.98 * np.cos(a), y + 1.3, z + 26.98 * np.sin(a)) @ g.rotate_y(-np.degrees(a) - 90) @ g.rotate_x(90)) for a in np.linspace(0, 2 * np.pi, 36, endpoint=False)]
    pieces.append(part("hall_studs", g.merge(studs), "BRASS_POLISHED"))
    # The lid: a dark bronze disc on a hinge at the back. It carries sand for the voice, and lifts to show the speaker below.
    lid_profile = g.profile_with_rounds([(0.0, y - 0.9), (11.7, y - 0.9), (11.75, y - 0.1), (11.65, y), (0.0, y + 0.02)], 0.06, steps=2)
    hinge = (x, y + 0.1, z - 11.6)
    pieces.append(part("lid", g.lathe(lid_profile, 128).transformed(g.translate(x, 0, z)), "BRONZE_DARK", pivot=hinge, axis=(1, 0, 0), travel=(0, -1.75)))
    pieces.append(part("lid_hinge", g.lathe(g.profile_with_rounds([(0.0, -3.2), (0.65, -3.2), (0.65, 3.2), (0.0, 3.2)], 0.08, steps=2), 20).transformed(g.translate(*hinge) @ g.rotate_z(90)), "BRASS_POLISHED"))
    # The speaker itself, in the well: paper cone, rubber edge, a copper dust cap.
    cone = g.profile_with_rounds([(2.6, y - 6.3), (9.4, y - 3.1)], 0, steps=1)
    cap = g.profile_with_rounds([(2.8, y - 6.45), (2.0, y - 5.25), (0.0, y - 4.8)], 1.0, steps=4)
    pieces.append(part("cone", g.lathe(cone, 128).transformed(g.translate(x, 0, z)), "PAPER", pivot=(x, y - 4, z), axis=(0, 1, 0), travel=(-0.7, 0.7)))
    pieces.append(part("cone_cap", g.lathe(cap, 64).transformed(g.translate(x, 0, z)), "COPPER", pivot=(x, y - 4, z), axis=(0, 1, 0), travel=(-0.7, 0.7)))
    surround = g.profile_with_rounds([(9.3, y - 3.2), (9.9, y - 2.6), (10.5, y - 3.1)], 0.3, steps=4)
    pieces.append(part("cone_edge", g.lathe(surround, 128).transformed(g.translate(x, 0, z)), "RUBBER"))
    basket = g.profile_with_rounds([(0.0, y - 10.5), (6.5, y - 10.5), (7.0, y - 8.5), (11.6, y - 3.6), (11.7, y - 2.6), (10.3, y - 2.7), (9.9, y - 3.4), (0.0, y - 6.8)], 0.12, steps=2)
    pieces.append(part("cone_basket", g.lathe(basket, 96).transformed(g.translate(x, 0, z)), "STEEL_BLUE"))
    return pieces


def lattice():
    """The sensing lattice, hung just under the dome: copper wire and small junctions that wake under a finger."""
    x, y, z = b.DOME_CENTER
    radius = b.DOME_RADIUS
    rise = 7.5
    sphere_r = (radius ** 2 + rise ** 2) / (2 * rise) - 2.2 - 0.3

    def height(r):
        return y + rise - 2.2 - 0.3 - (sphere_r - np.sqrt(sphere_r ** 2 - r ** 2))

    rings = (5.0, 10.0, 15.0, 20.0, 25.0)
    spokes = 12
    wires, nodes = [], []
    bead = g.lathe(g.profile_with_rounds([(0.0, -0.24), (0.2, -0.2), (0.27, 0.0), (0.2, 0.2), (0.0, 0.24)], 0.1, steps=2), 16)
    for r in rings:
        t = np.linspace(0, 2 * np.pi, 96, endpoint=False)
        wires.append(g.tube(np.stack([x + r * np.cos(t), np.full_like(t, height(r)), z + r * np.sin(t)], -1), 0.07, 6, closed=True))
        for k in range(spokes):
            a = 2 * np.pi * (k + (0.5 if r in (10.0, 20.0) else 0)) / spokes
            nodes.append(bead.transformed(g.translate(x + r * np.cos(a), height(r), z + r * np.sin(a))))
    for k in range(spokes):
        a = 2 * np.pi * k / spokes
        rs = np.linspace(rings[0], rings[-1], 24)
        wires.append(g.tube(np.stack([x + rs * np.cos(a), height(rs), z + rs * np.sin(a)], -1), 0.07, 6))
    return [part("lattice", g.merge(wires), "LATTICE"), part("lattice_nodes", g.merge(nodes), "LATTICE_NODE")]


def bellows(side, sign):
    """A paper bellows in each lobe, filling as the trigger comes in, with a valve on top and a pipe to the pressure gauge."""
    x, z = sign * 75.0, 13.0
    y0, y1 = -7.2, 0.0
    points = []
    folds = 7
    for i in range(folds * 2 + 1):
        points.append((6.2 if i % 2 else 4.9, y0 + (y1 - y0) * i / (folds * 2)))
    mesh = g.lathe(g.profile_with_rounds(points, 0.15, steps=2), 96, hard_angle=70).transformed(g.translate(x, 0, z))
    pieces = [part(f"bellows_{side}", mesh, "PAPER", pivot=(x, y0, z), axis=(0, 1, 0), travel=(0.72, 1.38))]
    cap = g.profile_with_rounds([(0.0, y1 - 0.5), (6.4, y1 - 0.5), (6.6, y1 - 0.1), (6.2, y1 + 0.3), (1.7, y1 + 0.4), (1.3, y1 + 0.8), (1.3, y1 + 2.0), (1.9, y1 + 2.3), (1.9, y1 + 2.9), (0.0, y1 + 2.9)], 0.1, steps=2)
    pieces.append(part(f"bellows_{side}_cap", g.lathe(cap, 72).transformed(g.translate(x, 0, z)), "BRASS_POLISHED", pivot=(x, y0, z), axis=(0, 1, 0), travel=(0.72, 1.38)))
    nozzle = np.array([x, y1 + 3.0, z])
    pieces.append(socket(f"socket_valve_{side}", tuple(nozzle)))
    return pieces


def pipes():
    """Brass pipe from each bellows along the plate to the gauge. Curved by hand, as a watchmaker's solder work would be."""
    runs = []
    for sign in (-1, 1):
        path = [(sign * 75.0, -7.0, 13.0), (sign * 72.0, -6.6, 22.0), (sign * 60.0, -6.4, 30.0), (sign * 30.0, -6.4, 31.5), (sign * 12.0, -5.2, 30.0), (sign * 4.0, 2.0, 28.0), (sign * 0.5, 4.5, 27.0)]
        t = np.linspace(0, len(path) - 1, 90)
        pts = np.array([_catmull(path, u) for u in t])
        runs.append(g.tube(pts, 0.55, 10))
    return part("pipes", g.merge(runs), "COPPER_PLAIN")


def _catmull(path, u):
    i = min(int(u), len(path) - 2)
    t = u - i
    p = [np.array(path[max(i - 1, 0)]), np.array(path[i]), np.array(path[i + 1]), np.array(path[min(i + 2, len(path) - 1)])]
    return 0.5 * (2 * p[1] + (p[2] - p[0]) * t + (2 * p[0] - 5 * p[1] + 4 * p[2] - p[3]) * t * t + (3 * p[1] - p[0] - 3 * p[2] + p[3]) * t ** 3)


def gauge():
    """One dial for both pipes, between the sticks. Its needle is the pressure the triggers hold."""
    x, y, z = 0.0, 5.0, 27.0
    ring = g.profile_with_rounds([(3.7, y - 1.0), (4.6, y - 1.0), (4.8, y + 0.2), (4.5, y + 0.8), (4.05, y + 0.8), (3.8, y + 0.2), (3.7, y - 1.0)], 0.12, steps=2)
    face = g.profile_with_rounds([(0.0, y - 0.6), (3.9, y - 0.6), (3.9, y - 0.05), (0.0, y)], 0.03, steps=1)
    post = g.lathe(g.profile_with_rounds([(0.0, -7.5), (1.4, -7.5), (1.0, -6.8), (1.0, y - 1.0), (0.0, y - 1.0)], 0.1, steps=2), 24)
    needle = g.merge([g.tube([(0.0, y + 0.22, 0.0), (0.0, y + 0.22, -3.3)], 0.13, 8).transformed(g.translate(x, 0, z)),
                      g.lathe(g.profile_with_rounds([(0.0, y + 0.05), (0.42, y + 0.05), (0.42, y + 0.35), (0.0, y + 0.42)], 0.06, steps=2), 20).transformed(g.translate(x, 0, z))])
    marks = []
    for a in np.linspace(-2.2, 2.2, 13):
        s, c = np.sin(a), np.cos(a)
        marks.append(g.tube([(x + 2.7 * s, y + 0.02, z - 2.7 * c), (x + 3.4 * s, y + 0.02, z - 3.4 * c)], 0.055, 6))
    return [part("gauge_ring", g.lathe(ring, 72).transformed(g.translate(x, 0, z)), "BRASS_POLISHED"),
            part("gauge_face", g.lathe(face, 72).transformed(g.translate(x, 0, z)), "CERAMIC"),
            part("gauge_post", post.transformed(g.translate(x, 0, z)), "BRONZE_DARK"),
            part("gauge_marks", g.merge(marks), "BRONZE_DARK"),
            part("needle", needle, "STEEL_BLUE", pivot=(x, y, z), axis=(0, -1, 0), travel=(-2.2, 2.2))]


def music_box():
    """The cylinder and comb at the back of the hall, a working part of the workshop: tines ring out."""
    y, z = -1.5, -24.2
    half = 11.0
    drum = g.profile_with_rounds([(0.0, -half), (3.5, -half), (3.6, -half + 0.2), (3.6, half - 0.2), (3.5, half), (0.0, half)], 0.05, steps=2)
    pieces = [part("cylinder", g.lathe(drum, 128).transformed(g.translate(0, y, z) @ g.rotate_z(-90)), "PINS", pivot=(0, y, z), axis=(1, 0, 0), travel=(0, 1))]
    shaft = g.lathe(g.profile_with_rounds([(0.0, -13.0), (0.7, -13.0), (0.7, 13.0), (0.0, 13.0)], 0.1, steps=2), 20).transformed(g.translate(0, y, z) @ g.rotate_z(-90))
    pieces.append(part("cylinder_shaft", shaft, "STEEL_BRUSHED", pivot=(0, y, z), axis=(1, 0, 0), travel=(0, 1)))
    posts = []
    for sign in (-1, 1):
        post = g.profile_with_rounds([(0.0, -5.4), (1.9, -5.4), (1.4, -4.9), (1.1, -3.0), (1.1, y + 1.1), (1.5, y + 1.6), (1.1, y + 2.0), (0.0, y + 2.0)], 0.1, steps=2)
        posts.append(g.lathe(post, 32).transformed(g.translate(sign * 12.6, 0, z)))
    pieces.append(part("cylinder_posts", g.merge(posts), "BRASS_BRUSHED"))
    base_z = z - 10.4
    block = g.surface_nets(lambda p: g.rounded_box(p, (0.0, y + 0.7, base_z), (11.0, 1.05, 1.35), 0.25), (-12.0, y - 1.0, base_z - 2.2), (12.0, y + 2.4, base_z + 2.2), 0.24)
    pieces.append(part("comb_block", block, "BRONZE_DARK"))
    pieces.append(socket("socket_comb", (0.0, y + 2.5, z - 3.0)))
    for i in range(8):
        tx = -9.1 + 2.6 * i
        length = 9.1
        def tine_sdf(p, tx=tx, length=length):
            return g.rounded_box(p, (tx, y + 3.15, base_z + 0.9 + length / 2), (0.95, 0.16, length / 2), 0.1)
        mesh = g.surface_nets(tine_sdf, (tx - 1.3, y + 2.7, base_z + 0.2), (tx + 1.3, y + 3.6, base_z + 1.4 + length), 0.17)
        pieces.append(part(f"tine_{i}", mesh, "STEEL_BRUSHED", pivot=(tx, y + 3.15, base_z + 0.9), axis=(1, 0, 0), travel=(0, 0.16)))
    return pieces


def socket(name, at):
    """A named place on the rig with nothing to draw, so effects can find it on any model."""
    empty = g.Mesh(np.array([at, at, at], dtype=np.float32), np.array([(0, 1, 0)] * 3, dtype=np.float32), np.array([0, 1, 2], dtype=np.uint32))
    return part(name, empty, "BRASS_BRUSHED", pivot=at)


def dome():
    """A thick cast dome: both faces are spheres, so light bends through it as through a lens."""
    x, y, z = b.DOME_CENTER
    radius = b.DOME_RADIUS
    rise = 7.5
    sphere_r = (radius ** 2 + rise ** 2) / (2 * rise)
    thickness = 2.2
    outer_angles = np.linspace(0.0, np.arcsin(radius / sphere_r), 48)
    outer = [(sphere_r * np.sin(a), y + rise - sphere_r * (1 - np.cos(a))) for a in outer_angles]
    outer_mesh = g.lathe(np.array(outer)[::-1], 192).transformed(g.translate(x, 0, z))
    inner_r = sphere_r - thickness
    inner_angles = np.linspace(0.0, np.arcsin((radius - 1.2) / inner_r), 32)
    inner = [(inner_r * np.sin(a), y + rise - thickness - inner_r * (1 - np.cos(a))) for a in inner_angles]
    inner_mesh = g.lathe(np.array(inner), 128).transformed(g.translate(x, 0, z))
    return [part("dome", outer_mesh, "ACRYLIC", "clear"), part("dome_inner", inner_mesh, "ACRYLIC", "clear_back")]


def bezel():
    x, y, z = b.DOME_CENTER
    r0, r1 = b.DOME_RADIUS - 0.4, b.DOME_RADIUS + 2.6
    profile = g.profile_with_rounds([(r0, y - 3.2), (r1, y - 3.2), (r1 + 0.5, y - 0.8), (r1 - 0.2, y + 0.3), (r0 + 0.5, y + 0.55), (r0, y + 0.1), (r0, y - 3.2)],
                                    [0, 0.25, 0.6, 0.5, 0.3, 0.1, 0], steps=5)
    ring = g.lathe(profile, 256).transformed(g.translate(x, 0, z))
    pieces = [part("bezel", ring, "BRASS_POLISHED")]
    screw = g.lathe(g.profile_with_rounds([(0.0, 0.0), (1.05, 0.0), (1.05, 0.34), (0.75, 0.52), (0.0, 0.58)], [0, 0.05, 0.12, 0.3, 0], steps=3), 32)
    screws = []
    for i in range(8):
        a = np.radians(22.5 + 45 * i)
        screws.append(screw.transformed(g.translate(x + (r1 - 1.15) * np.cos(a), y + 0.16, z + (r1 - 1.15) * np.sin(a)) @ g.rotate_y(37.0 * i)))
    pieces.append(part("bezel_screws", g.merge(screws), "STEEL_BLUE"))
    filament = g.lathe(np.array([(r0 - 0.9 + 0.32 * np.cos(t), y - 1.6 + 0.32 * np.sin(t)) for t in np.linspace(0, 2 * np.pi, 13)]), 256).transformed(g.translate(x, 0, z))
    pieces.append(part("filament", filament, "FILAMENT"))
    return pieces


def coil(side, sign):
    """A voice coil in each lobe: a copper winding on a bobbin, its magnet free to move on two springs. It really is what moves."""
    center = (sign * 62.0, -15.6, 12.0)
    winding = g.profile_with_rounds([(5.0, -5.5), (8.2, -5.5), (8.2, 5.5), (5.0, 5.5)], 0.3, steps=3)
    wound = g.lathe(winding, 96).transformed(g.translate(*center) @ g.rotate_x(90))
    wound.bake[:, 2] = 1
    flange = g.profile_with_rounds([(4.8, -0.5), (9.4, -0.5), (9.4, 0.5), (4.8, 0.5), (4.8, -0.5)], 0.15, steps=3)
    flanges = g.merge([g.lathe(flange, 96).transformed(g.translate(*center) @ g.rotate_x(90) @ g.translate(0, s * 6.0, 0)) for s in (-1, 1)])
    magnet = g.lathe(g.profile_with_rounds([(0.0, -9.0), (4.2, -9.0), (4.2, 9.0), (0.0, 9.0)], 0.5, steps=3), 64).transformed(g.translate(*center) @ g.rotate_x(90))
    pieces = [part(f"coil_{side}_winding", wound, "COPPER", pivot=center, axis=(0, 0, 1), travel=(-2.6, 2.6)), part(f"coil_{side}_flanges", flanges, "BRASS_BRUSHED", pivot=center, axis=(0, 0, 1), travel=(-2.6, 2.6)),
              part(f"coil_{side}_magnet", magnet, "STEEL_BRUSHED", pivot=center, axis=(0, 0, 1), travel=(-2.6, 2.6))]
    # A spring at each end of the magnet, to a stop on the plate: what brings it back, and what you can see trembling.
    stops = []
    for e, end in (("a", 1), ("b", -1)):
        fixed = np.array(center) + np.array([0, 0, end * 13.0])
        spring = g.helix(3.0, 3.8, 5.5, 0.22).transformed(g.translate(*(np.array(center) + np.array([0, 0, end * 9.1]))) @ (np.eye(4) if end > 0 else g.rotate_x(180)))
        pieces.append(part(f"coil_{side}_spring_{e}", spring, "STEEL_BLUE", pivot=tuple(fixed), axis=(0, 0, 1), travel=(-2.6, 2.6)))
        stops.append(g.surface_nets(lambda p, c=fixed: g.rounded_box(p, c + np.array([0, 2.0, end * 0.5]), (3.8, 6.5, 0.55), 0.2), fixed + np.array([-4.6, -5.4, -1.6]), fixed + np.array([4.6, 9.4, 1.6]), 0.42))
    pieces.append(part(f"coil_{side}_stops", g.merge(stops), "BRASS_BRUSHED"))
    return pieces


def gear_part(name, teeth, module, half, center, rotation_axis_matrix, material="BRASS_BRUSHED", ratio=1.0, bore=0.8):
    outline, _, _ = g.gear_outline(teeth, module)
    mesh = g.extrude(outline, half, chamfer=min(half * 0.3, 0.08), hole_radius=bore)
    mesh = mesh.transformed(g.translate(*center) @ rotation_axis_matrix)
    axis = (rotation_axis_matrix @ np.array([0, 1, 0, 0]))[:3]
    return part(name, mesh, material, pivot=tuple(center), axis=tuple(axis), travel=(0, ratio))


def trigger_train(side, sign):
    """Motor, worm, reduction gear, sector, and the pedal's axle. The honest order of such a mechanism, drawn large."""
    x = sign * 62.0
    axle = (x, -13.0, -16.0)
    pieces = []
    r_x = g.rotate_z(90)      # gears turning about the x axis
    pieces.append(gear_part(f"gear_{side}_sector", 34, 0.42, 0.8, axle, r_x, ratio=1.0, bore=1.2))
    # The whole train hangs under the plate, nothing of it may reach the controls above, at any point of their travel.
    second = (x, -13.0, -16.0 + 9.6)
    pieces.append(gear_part(f"gear_{side}_reduce", 12, 0.42, 1.2, second, r_x, ratio=-2.83))
    third = (x, -19.4, -4.5)      # below the reduction gear, and well behind the coil's magnet at the far end of its travel
    pieces.append(gear_part(f"gear_{side}_wheel", 28, 0.30, 0.6, third, r_x, material="BRASS_POLISHED", ratio=-2.83, bore=0.7))
    motor_center = (x - sign * 16.0, third[1], third[2] - 5.5)
    motor = g.lathe(g.profile_with_rounds([(0.0, -7.0), (5.2, -7.0), (5.2, 7.0), (0.9, 7.0), (0.9, 9.5), (0.0, 9.5)], 0.3, steps=3), 64).transformed(
        g.translate(*motor_center) @ g.rotate_z(-90 * sign))
    pieces.append(part(f"motor_{side}", motor, "STEEL_BRUSHED"))
    # A worm: a thread wound round the motor's shaft, cut as a modulated turning.
    worm_center = (x - sign * 3.6, motor_center[1], motor_center[2])
    worm = g.lathe(g.profile_with_rounds([(0.0, -4.6), (1.3, -4.6), (1.3, 4.6), (0.0, 4.6)], 0.2, steps=2), 96)
    p = worm.positions.astype(np.float64)
    t = np.arctan2(p[:, 2], p[:, 0]) + p[:, 1] * 2 * np.pi / 1.4
    r = np.hypot(p[:, 0], p[:, 2])
    grow = 1 + 0.26 * (0.5 + 0.5 * np.cos(t)) * (r > 0.1)
    p[:, 0] *= grow
    p[:, 2] *= grow
    worm.positions = p.astype(np.float32)
    worm = worm.transformed(g.translate(*worm_center) @ g.rotate_z(-90 * sign))
    pieces.append(part(f"gear_{side}_worm", worm, "STEEL_BRUSHED", pivot=worm_center, axis=(sign, 0, 0), travel=(0, 36.0)))
    return pieces


def pillars():
    """Turned posts holding the plate to the shell, each ending in a blued screw."""
    posts = []
    screws = []
    post = g.lathe(g.profile_with_rounds([(0.0, -1.0), (1.6, -1.0), (1.1, 0.0), (1.1, 12.0), (1.6, 13.0), (0.0, 13.0)], 0.2, steps=3), 32)
    screw = g.lathe(g.profile_with_rounds([(0.0, 0.0), (1.3, 0.0), (1.3, 0.5), (0.9, 0.8), (0.0, 0.85)], [0, 0.05, 0.15, 0.3, 0], steps=3), 32)
    for x, z in ((36, 2), (-36, 2), (74, 2), (-74, 2), (54, 26), (-54, 26), (54, -14), (-54, -14), (20, 26), (-20, 26), (0, -40)):
        y = -7.0
        posts.append(post.transformed(g.translate(x, y, z)))
        screws.append(screw.transformed(g.translate(x, y + 13.2, z)))
    return [part("pillars", g.merge(posts), "BRONZE_DARK"), part("pillar_screws", g.merge(screws), "STEEL_BLUE")]


def wheels():
    """A going train across the plate: it turns while the instrument is alive, and gives the eye something true to follow."""
    pieces = []
    for name, teeth, module, x, y, z, ratio in (("gear_l_going1", 44, 0.34, -60.0, -5.0, 14.0, 0.6), ("gear_l_going2", 16, 0.34, -47.5, -4.6, 8.0, -1.65),
                                                ("gear_r_going1", 44, 0.34, 60.0, -5.0, 14.0, -0.6), ("gear_r_going2", 16, 0.34, 47.5, -4.6, 8.0, 1.65)):
        pieces.append(gear_part(name, teeth, module, 0.5, (x, y, z), np.eye(4), ratio=ratio))
    return pieces


def all_parts():
    parts = [plate()] + hall_floor() + dome() + bezel() + lattice() + pillars() + wheels() + gauge() + [pipes()] + music_box()
    for side, sign in (("l", -1), ("r", 1)):
        parts += coil(side, sign)
        parts += trigger_train(side, sign)
        parts += bellows(side, sign)
    return parts
