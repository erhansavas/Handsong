# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Geometry for the instrument, from first principles: signed distance fields for moulded forms, turned and cut profiles for
machined parts. No scan and no measured data from any product. Units are millimetres in the object's own axes: x right,
y out of the face, z toward the player. Numbers only: runs in Blender's Python and in plain Python alike."""
import numpy as np


# ---------------------------------------------------------------------------- Signed distances
def smooth_min(a, b, k):
    """Polynomial smooth minimum: a fillet of radius about k between two surfaces, continuous in curvature far from it."""
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


def smooth_max(a, b, k):
    return -smooth_min(-a, -b, k)


def sphere(p, center, radius):
    return np.linalg.norm(p - np.asarray(center, dtype=np.float64), axis=-1) - radius


def ellipsoid(p, center, radii):
    """A good approximation near the surface; the mesher projects onto the true zero with its gradient."""
    q = (p - np.asarray(center, dtype=np.float64)) / np.asarray(radii, dtype=np.float64)
    k0 = np.linalg.norm(q, axis=-1)
    k1 = np.linalg.norm(q / np.asarray(radii, dtype=np.float64), axis=-1)
    return k0 * (k0 - 1) / np.maximum(k1, 1e-6)


def cylinder_y(p, center, radius, half_height, round_edge=0.0):
    q = p - np.asarray(center, dtype=np.float64)
    d_radial = np.hypot(q[..., 0], q[..., 2]) - radius + round_edge
    d_axial = np.abs(q[..., 1]) - half_height + round_edge
    outside = np.hypot(np.maximum(d_radial, 0), np.maximum(d_axial, 0))
    return outside + np.minimum(np.maximum(d_radial, d_axial), 0) - round_edge


def rounded_box(p, center, half, round_edge=0.0):
    q = np.abs(p - np.asarray(center, dtype=np.float64)) - np.asarray(half, dtype=np.float64) + round_edge
    outside = np.linalg.norm(np.maximum(q, 0), axis=-1)
    return outside + np.minimum(np.max(q, axis=-1), 0) - round_edge


def arc_ribbon(p, center_xz, radius, half_angle, half_width, half_height):
    """A bar bent along a circle in the xz plane, rounded in section, with round ends."""
    x, y, z = p[..., 0] - center_xz[0], p[..., 1], p[..., 2] - center_xz[1]
    angle = np.arctan2(x, -z)
    clamped = np.clip(angle, -half_angle, half_angle)
    ax, az = radius * np.sin(clamped), -radius * np.cos(clamped)
    dx, dz = x - ax, z - az
    # squash height so that the section is an ellipse of half_width by half_height
    scale = half_width / half_height
    return (np.sqrt(dx * dx + dz * dz + (y * scale) ** 2) - half_width) / scale


# ---------------------------------------------------------------------------- Meshes
class Mesh:
    def __init__(self, positions, normals, indices, tangents=None, uvs=None):
        self.positions = np.asarray(positions, dtype=np.float32)
        self.normals = np.asarray(normals, dtype=np.float32)
        self.indices = np.asarray(indices, dtype=np.uint32)
        count = len(self.positions)
        self.tangents = np.asarray(tangents, dtype=np.float32) if tangents is not None else default_tangents(self.normals)
        self.uvs = np.asarray(uvs, dtype=np.float32) if uvs is not None else np.zeros((count, 2), dtype=np.float32)
        self.bake = np.ones((count, 4), dtype=np.float32)   # occlusion, thickness in cm, cavity, light

    def transformed(self, matrix):
        m = np.asarray(matrix, dtype=np.float64)
        out = Mesh((self.positions @ m[:3, :3].T) + m[:3, 3], _unit(self.normals @ np.linalg.inv(m[:3, :3])), self.indices,
                   _unit(self.tangents @ m[:3, :3].T), self.uvs)
        out.bake = self.bake.copy()
        return out

    def flipped(self):
        out = Mesh(self.positions, -self.normals, self.indices.reshape(-1, 3)[:, ::-1].ravel(), self.tangents, self.uvs)
        out.bake = self.bake.copy()
        return out

    def triangle_count(self):
        return len(self.indices) // 3


def merge(meshes):
    offsets = np.cumsum([0] + [len(m.positions) for m in meshes[:-1]])
    out = Mesh(np.vstack([m.positions for m in meshes]), np.vstack([m.normals for m in meshes]),
               np.concatenate([m.indices + o for m, o in zip(meshes, offsets)]), np.vstack([m.tangents for m in meshes]),
               np.vstack([m.uvs for m in meshes]))
    out.bake = np.vstack([m.bake for m in meshes])
    return out


def _unit(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)


def default_tangents(normals):
    n = np.asarray(normals, dtype=np.float64)
    helper = np.where(np.abs(n[..., 1, None]) < 0.9, np.array([0.0, 1.0, 0.0]), np.array([1.0, 0.0, 0.0]))
    return _unit(np.cross(helper, n)).astype(np.float32)


def gradient(sdf, p, eps):
    g = np.empty_like(p)
    for axis in range(3):
        d = np.zeros(3)
        d[axis] = eps
        g[..., axis] = sdf(p + d) - sdf(p - d)
    return g / (2 * eps)


def surface_nets(sdf, low, high, cell, project_steps=3, batch=400000):
    """Meshes the zero set of a distance function. Each surface vertex is pulled onto the true surface along the gradient,
    and takes its normal from the gradient too, so reflections are as smooth as the function and not as coarse as the grid."""
    low, high = np.asarray(low, dtype=np.float64), np.asarray(high, dtype=np.float64)
    counts = np.ceil((high - low) / cell).astype(int) + 1
    xs = [low[i] + np.arange(counts[i]) * cell for i in range(3)]
    field = np.empty(counts, dtype=np.float32)
    grid_yz = np.stack(np.meshgrid(xs[1], xs[2], indexing="ij"), -1)
    for i, x in enumerate(xs[0]):
        p = np.concatenate([np.full(grid_yz.shape[:2] + (1,), x), grid_yz], -1)
        field[i] = sdf(p)
    inside = field < 0
    corner = np.stack([inside[:-1, :-1, :-1], inside[1:, :-1, :-1], inside[:-1, 1:, :-1], inside[1:, 1:, :-1],
                       inside[:-1, :-1, 1:], inside[1:, :-1, 1:], inside[:-1, 1:, 1:], inside[1:, 1:, 1:]], -1)
    mixed = corner.any(-1) & ~corner.all(-1)
    cells = np.argwhere(mixed)
    lookup = -np.ones(mixed.shape, dtype=np.int64)
    lookup[tuple(cells.T)] = np.arange(len(cells))
    positions = low + (cells + 0.5) * cell

    def project(points):
        for _ in range(project_steps):
            g = gradient(sdf, points, cell * 0.25)
            d = sdf(points)
            points = points - g * (d / np.maximum((g * g).sum(-1), 1e-9))[..., None]
        return points

    out = np.empty_like(positions)
    normals = np.empty_like(positions)
    for start in range(0, len(positions), batch):
        chunk = project(positions[start:start + batch])
        out[start:start + batch] = chunk
        normals[start:start + batch] = _unit(gradient(sdf, chunk, cell * 0.1))
    faces = []
    n = counts
    # An edge along x between (i,j,k) and (i+1,j,k) crosses the surface: join the four cells around it.
    for axis in range(3):
        a, b = (axis + 1) % 3, (axis + 2) % 3
        slicer_here = [slice(None)] * 3
        slicer_next = [slice(None)] * 3
        slicer_here[axis] = slice(0, n[axis] - 1)
        slicer_next[axis] = slice(1, n[axis])
        crossing = inside[tuple(slicer_here)] != inside[tuple(slicer_next)]
        edges = np.argwhere(crossing)
        keep = (edges[:, a] > 0) & (edges[:, a] < n[a] - 1) & (edges[:, b] > 0) & (edges[:, b] < n[b] - 1)
        edges = edges[keep]
        if not len(edges):
            continue
        da, db = np.zeros(3, int), np.zeros(3, int)
        da[a], db[b] = 1, 1
        c00, c10 = edges - da - db, edges - db
        c11, c01 = edges.copy(), edges - da
        ids = [lookup[tuple(c.T)] for c in (c00, c10, c11, c01)]
        flip = inside[tuple(edges.T)]
        q = np.stack(ids, -1)
        q[flip] = q[flip][:, ::-1]
        faces.append(np.concatenate([q[:, [0, 1, 2]], q[:, [0, 2, 3]]]))
    return Mesh(out, normals, np.concatenate(faces).ravel())


def profile_with_rounds(points, radii, steps=4):
    """A polyline (r, y) with its corners rounded by the given radii. Returns points and hard-edge flags."""
    pts = [np.asarray(p, dtype=np.float64) for p in points]
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        radius = radii[i] if hasattr(radii, "__len__") else radii
        a, b, c = pts[i - 1], pts[i], pts[i + 1]
        if radius <= 0:
            out.append(b)
            continue
        u, v = (a - b) / np.linalg.norm(a - b), (c - b) / np.linalg.norm(c - b)
        cos_angle = np.clip(np.dot(u, v), -1, 1)
        half = np.arccos(cos_angle) / 2
        if half < 1e-3 or abs(half - np.pi / 2) < 1e-3:
            out.append(b)
            continue
        along = min(radius / np.tan(half), np.linalg.norm(a - b) * 0.49, np.linalg.norm(c - b) * 0.49)
        start, end = b + u * along, b + v * along
        mid = (u + v) / np.linalg.norm(u + v)
        center = b + mid * (along / np.cos(half))
        r = np.linalg.norm(start - center)
        a0 = np.arctan2(*(start - center)[::-1])
        a1 = np.arctan2(*(end - center)[::-1])
        delta = (a1 - a0 + np.pi) % (2 * np.pi) - np.pi
        for s in np.linspace(0, 1, steps + 1):
            t = a0 + delta * s
            out.append(center + r * np.array([np.cos(t), np.sin(t)]))
    out.append(pts[-1])
    return np.array(out)


def lathe(profile, segments=96, hard_angle=35.0):
    """Turns a profile of (radius, height) around the y axis, as on a lathe. Corners sharper than hard_angle stay sharp."""
    profile = np.asarray(profile, dtype=np.float64)
    tangents = np.diff(profile, axis=0)
    lengths = np.linalg.norm(tangents, axis=-1, keepdims=True)
    tangents = tangents / np.maximum(lengths, 1e-9)
    normals_2d = np.stack([tangents[:, 1], -tangents[:, 0]], -1)
    rings, ring_normals = [], []
    for i in range(len(profile)):
        before = normals_2d[i - 1] if i > 0 else normals_2d[0]
        after = normals_2d[i] if i < len(normals_2d) else normals_2d[-1]
        angle = np.degrees(np.arccos(np.clip(np.dot(before, after), -1, 1)))
        if angle > hard_angle:
            rings += [profile[i], profile[i]]
            ring_normals += [before, after]
        else:
            rings.append(profile[i])
            n = before + after
            ring_normals.append(n / np.maximum(np.linalg.norm(n), 1e-9))
    rings, ring_normals = np.array(rings), np.array(ring_normals)
    theta = np.linspace(0, 2 * np.pi, segments, endpoint=False)
    c, s = np.cos(theta)[None], np.sin(theta)[None]
    positions = np.stack([rings[:, 0, None] * c, np.repeat(rings[:, 1, None], segments, 1), rings[:, 0, None] * s], -1).reshape(-1, 3)
    normals = np.stack([ring_normals[:, 0, None] * c, np.repeat(ring_normals[:, 1, None], segments, 1), ring_normals[:, 0, None] * s], -1).reshape(-1, 3)
    tangent = np.stack([-s * np.ones((len(rings), 1)), np.zeros((len(rings), segments)), c * np.ones((len(rings), 1))], -1).reshape(-1, 3)
    uvs = np.stack([np.repeat(theta[None], len(rings), 0) / (2 * np.pi), np.repeat(rings[:, 0, None], segments, 1)], -1).reshape(-1, 2)
    faces = []
    for i in range(len(rings) - 1):
        if np.allclose(rings[i], rings[i + 1]):
            continue
        for j in range(segments):
            k = (j + 1) % segments
            a, b, cc, d = i * segments + j, i * segments + k, (i + 1) * segments + k, (i + 1) * segments + j
            faces += [a, cc, b, a, d, cc]
    return Mesh(positions, _unit(normals), np.array(faces, dtype=np.uint32), tangent, uvs)


def lathe_arc(profile, a0, a1, segments=48):
    """Part of a turned surface, from angle a0 to a1 in degrees: a flat inlay, for instance. Faces follow lathe()."""
    whole = lathe(profile, segments * 2)
    rings = len(whole.positions) // (segments * 2)
    theta = np.radians(np.linspace(a0, a1, segments + 1))
    out_p, out_n, out_t, out_uv = [], [], [], []
    for r in range(rings):
        p0, n0 = whole.positions[r * segments * 2], whole.normals[r * segments * 2]
        for t in theta:
            c, s = np.cos(t), np.sin(t)
            out_p.append((p0[0] * c, p0[1], p0[0] * s))
            out_n.append((n0[0] * c, n0[1], n0[0] * s))
            out_t.append((-s, 0.0, c))
            out_uv.append((t / (2 * np.pi), p0[0]))
    faces = []
    w = segments + 1
    for i in range(rings - 1):
        if np.allclose(out_p[i * w], out_p[(i + 1) * w]):
            continue
        for j in range(segments):
            a, b, cc, d = i * w + j, i * w + j + 1, (i + 1) * w + j + 1, (i + 1) * w + j
            faces += [a, cc, b, a, d, cc]
    return Mesh(np.array(out_p), np.array(out_n), np.array(faces, dtype=np.uint32), np.array(out_t), np.array(out_uv))


def tube(path, radius, sides=8, closed=False):
    """A round wire swept along a path of 3D points."""
    path = np.asarray(path, dtype=np.float64)
    count = len(path)
    if closed:
        forward = np.roll(path, -1, 0) - np.roll(path, 1, 0)
    else:
        forward = np.gradient(path, axis=0)
    forward = _unit(forward)
    helper = np.array([0.0, 1.0, 0.0]) if abs(forward[0, 1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    side = _unit(np.cross(forward[0], helper))
    positions, normals, tangents, uvs = [], [], [], []
    for i in range(count):
        side = _unit(side - forward[i] * np.dot(side, forward[i]))
        up = np.cross(forward[i], side)
        for k in range(sides):
            a = 2 * np.pi * k / sides
            n = side * np.cos(a) + up * np.sin(a)
            positions.append(path[i] + n * radius)
            normals.append(n)
            tangents.append(forward[i])
            uvs.append((k / sides, i / max(count - 1, 1)))
    faces = []
    rows = count if closed else count - 1
    for i in range(rows):
        i2 = (i + 1) % count
        for k in range(sides):
            k2 = (k + 1) % sides
            a, b, c, d = i * sides + k, i * sides + k2, i2 * sides + k2, i2 * sides + k
            faces += [a, b, c, a, c, d]
    mesh = Mesh(np.array(positions), np.array(normals), np.array(faces, dtype=np.uint32), np.array(tangents), np.array(uvs))
    return _wind_by_normals(mesh)


def helix(radius, length, turns, wire, points_per_turn=20, sides=6):
    """A spring along +z from the origin."""
    n = int(turns * points_per_turn) + 1
    t = np.linspace(0, turns * 2 * np.pi, n)
    return tube(np.stack([radius * np.cos(t), radius * np.sin(t), np.linspace(0, length, n)], -1), wire, sides)


def gear_outline(teeth, module, pressure_angle=20.0, points_per_flank=4):
    """A closed outline of a spur gear with straight-sided involute-like flanks, in (x, z)."""
    pitch = module * teeth / 2
    outer, root = pitch + module, pitch - 1.25 * module
    base = pitch * np.cos(np.radians(pressure_angle))
    tooth = 2 * np.pi / teeth
    outline = []
    for t in range(teeth):
        center = t * tooth
        half_top, half_pitch, half_root = tooth * 0.14, tooth * 0.25, tooth * 0.34
        flank = np.linspace(0, 1, points_per_flank)
        rising = [(root + (outer - root) * f, center - (half_root + (half_top - half_root) * f)) for f in flank]
        falling = [(root + (outer - root) * f, center + (half_root + (half_top - half_root) * f)) for f in flank[::-1]]
        outline += rising + falling
    outline = np.array(outline)
    return np.stack([outline[:, 0] * np.cos(outline[:, 1]), outline[:, 0] * np.sin(outline[:, 1])], -1), outer, root


def extrude(outline, half_height, chamfer=0.08, hole_radius=0.0, hole_segments=48):
    """A closed outline (x, z) as a machined part: flat faces, chamfered edges, optionally a round bore."""
    outline = np.asarray(outline, dtype=np.float64)
    count = len(outline)
    next_point, previous = np.roll(outline, -1, 0), np.roll(outline, 1, 0)
    edge = _unit(next_point - previous)
    side = np.stack([edge[:, 1], -edge[:, 0]], -1)
    area = 0.5 * np.sum(outline[:, 0] * next_point[:, 1] - next_point[:, 0] * outline[:, 1])
    if area > 0:   # keep the side normal pointing outward for this winding
        side = -side
    inner = outline - side * chamfer
    positions, normals, faces = [], [], []

    def add_strip(a2, ay, b2, by, n2, ny):
        base = sum(len(p) for p in positions)
        ring_a = np.c_[a2[:, 0], np.full(count, ay), a2[:, 1]]
        ring_b = np.c_[b2[:, 0], np.full(count, by), b2[:, 1]]
        positions.append(np.vstack([ring_a, ring_b]))
        n = np.c_[n2[:, 0], np.full(count, ny), n2[:, 1]]
        normals.append(_unit(np.vstack([n, n])))
        for i in range(count):
            j = (i + 1) % count
            quad = [base + i, base + j, base + count + j, base + count + i]
            faces.extend([quad[0], quad[2], quad[1], quad[0], quad[3], quad[2]])

    top, bottom = half_height, -half_height
    add_strip(outline, top - chamfer, outline, bottom + chamfer, side, 0.0)
    add_strip(inner, top, outline, top - chamfer, side, 1.0)
    add_strip(outline, bottom + chamfer, inner, bottom, side, -1.0)
    mesh = Mesh(np.vstack(positions), np.vstack(normals), np.array(faces, dtype=np.uint32))
    # Fix the winding of everything at once, from the normals.
    mesh = _wind_by_normals(mesh)
    caps = [_annulus_cap(inner, top, hole_radius, 1.0, hole_segments), _annulus_cap(inner, bottom, hole_radius, -1.0, hole_segments)]
    parts = [mesh] + caps
    if hole_radius > 0:
        parts.append(lathe(profile_with_rounds([(hole_radius, bottom), (hole_radius, top)], 0.0), hole_segments).flipped())
    return merge(parts)


def _annulus_cap(outline, y, hole_radius, ny, hole_segments):
    """A flat face between an outline and a round bore, or a fan from the centre when there is no bore."""
    count = len(outline)
    angles = np.arctan2(outline[:, 1], outline[:, 0])
    radius = max(hole_radius, 1e-3)
    inner = np.c_[radius * np.cos(angles), radius * np.sin(angles)]
    pts = np.vstack([np.c_[outline[:, 0], np.full(count, y), outline[:, 1]], np.c_[inner[:, 0], np.full(count, y), inner[:, 1]]])
    faces = []
    for i in range(count):
        j = (i + 1) % count
        faces += [i, j, count + j, i, count + j, count + i]
    mesh = Mesh(pts, np.tile([0.0, ny, 0.0], (len(pts), 1)), np.array(faces, dtype=np.uint32))
    return _wind_by_normals(mesh)


def _wind_by_normals(mesh):
    tri = mesh.indices.reshape(-1, 3)
    a, b, c = mesh.positions[tri[:, 0]], mesh.positions[tri[:, 1]], mesh.positions[tri[:, 2]]
    geometric = np.cross(b - a, c - a)
    wrong = (geometric * mesh.normals[tri[:, 0]]).sum(-1) < 0
    tri[wrong] = tri[wrong][:, ::-1]
    mesh.indices = tri.ravel()
    return mesh


def translate(x, y, z):
    m = np.eye(4)
    m[:3, 3] = (x, y, z)
    return m


def rotate_y(degrees):
    a = np.radians(degrees)
    m = np.eye(4)
    m[0, 0], m[0, 2], m[2, 0], m[2, 2] = np.cos(a), np.sin(a), -np.sin(a), np.cos(a)
    return m


def rotate_x(degrees):
    a = np.radians(degrees)
    m = np.eye(4)
    m[1, 1], m[1, 2], m[2, 1], m[2, 2] = np.cos(a), -np.sin(a), np.sin(a), np.cos(a)
    return m


def rotate_z(degrees):
    a = np.radians(degrees)
    m = np.eye(4)
    m[0, 0], m[0, 1], m[1, 0], m[1, 1] = np.cos(a), -np.sin(a), np.sin(a), np.cos(a)
    return m


def scale(s):
    m = np.eye(4)
    m[0, 0] = m[1, 1] = m[2, 2] = s
    return m


def largest_piece(mesh):
    """The biggest connected piece of a mesh, joined by position."""
    tri = mesh.indices.reshape(-1, 3)
    _, weld = np.unique(np.round(mesh.positions, 3), axis=0, return_inverse=True)
    parent = np.arange(weld.max() + 1)

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for a, b, c in weld.ravel()[tri]:
        ra = find(a)
        parent[find(b)] = ra
        parent[find(c)] = ra
    roots = np.array([find(w) for w in weld.ravel()[tri[:, 0]]])
    keep = roots == np.bincount(roots).argmax()
    out = Mesh(mesh.positions, mesh.normals, tri[keep].ravel(), mesh.tangents, mesh.uvs)
    out.bake = mesh.bake
    return out
