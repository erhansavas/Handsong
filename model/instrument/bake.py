# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Bakes ambient occlusion from Cycles into a model pack's vertices, so light truth comes from an offline renderer.
Runs inside Blender: Blender -b --factory-startup --python model/instrument/bake.py -- pack.hsmesh [samples]
The crew move and the clear parts let light through, so neither casts anything here. The file is rewritten in place."""
import struct
import sys
import numpy as np
import bpy

NAME = 32
HEADER = NAME + 6 * 4 + 8 * 4 + 4 + 6 * 4 + 2 * 4       # keeps in step with export.py: name, six ints, pivot axis travel, parent, bounds
VERTEX = 32


def read(path):
    data = bytearray(open(path, "rb").read())
    if data[:4] != b"HSM1":
        raise SystemExit("not a model pack")
    parts, vertices, indices = struct.unpack_from("<III", data, 4)
    size = (len(data) - 16 - vertices * VERTEX - indices * 4) // parts
    return data, parts, vertices, indices, size


def main():
    arguments = sys.argv[sys.argv.index("--") + 1:]
    path = arguments[0]
    samples = int(arguments[1]) if len(arguments) > 1 else 48
    data, part_count, vertex_count, index_count, part_size = read(path)
    table_at = 16
    vertex_at = table_at + part_count * part_size
    index_at = vertex_at + vertex_count * VERTEX
    raw = np.frombuffer(data, dtype=np.uint8, count=vertex_count * VERTEX, offset=vertex_at).reshape(-1, VERTEX)
    positions = np.frombuffer(data, dtype="<f4", count=vertex_count * 8, offset=vertex_at).reshape(-1, 8)[:, :3] * 1000.0
    all_indices = np.frombuffer(data, dtype="<u4", count=index_count, offset=index_at)

    packed = np.frombuffer(data, dtype="<u4", count=vertex_count * 8, offset=vertex_at).reshape(-1, 8)[:, 3]
    signed = np.stack([(packed << 22).astype(np.int32) >> 22, (packed << 12).astype(np.int32) >> 22, (packed << 2).astype(np.int32) >> 22], -1) / 511.0
    positions_out, faces_out, ranges = [], [], []
    at = 0
    no_shadow = ("body", "dome", "lattice", "lid", "ball", "socket_", "pack_")
    for i in range(part_count):
        offset = table_at + i * part_size
        name = bytes(data[offset:offset + NAME]).split(b"\0")[0].decode()
        v_count, i_count, v_at, i_at = struct.unpack_from("<IIII", data, offset + NAME)
        pass_kind = struct.unpack_from("<i", data, offset + NAME + 20)[0]
        parent = struct.unpack_from("<i", data, offset + NAME + 24 + 32)[0]
        if parent >= 0 or pass_kind != 0 or i_count <= 3 or name.startswith(no_shadow):
            continue
        p = positions[v_at:v_at + v_count]
        f = all_indices[i_at:i_at + i_count].reshape(-1, 3)
        # Blender decides a face's side from its winding, so each face is wound to agree with the vertex normals it carries.
        wind = np.cross(p[f[:, 1]] - p[f[:, 0]], p[f[:, 2]] - p[f[:, 0]])
        wrong = np.sum(wind * (signed[v_at:v_at + v_count][f[:, 0]] + signed[v_at:v_at + v_count][f[:, 1]] + signed[v_at:v_at + v_count][f[:, 2]]), axis=1) < 0
        f = f.copy()
        f[wrong] = f[wrong][:, ::-1]
        f = f.ravel()
        positions_out.append(p)
        faces_out.append(f + at)
        ranges.append((v_at, v_count, at))
        at += v_count
    joined_p = np.vstack(positions_out).astype(np.float32)
    joined_f = np.concatenate(faces_out).astype(np.int32)
    print(f"baking {len(joined_p)} vertices, {len(joined_f) // 3} triangles")

    for leftover in list(bpy.data.objects):
        bpy.data.objects.remove(leftover)
    mesh = bpy.data.meshes.new("instrument")
    mesh.vertices.add(len(joined_p))
    mesh.vertices.foreach_set("co", joined_p.ravel())
    triangles = len(joined_f) // 3
    mesh.loops.add(len(joined_f))
    mesh.loops.foreach_set("vertex_index", joined_f)
    mesh.polygons.add(triangles)
    mesh.polygons.foreach_set("loop_start", np.arange(triangles, dtype=np.int32) * 3)
    mesh.update()
    mesh.validate(verbose=False)
    mesh.shade_smooth()
    color = mesh.color_attributes.new("ao", "BYTE_COLOR", "POINT")
    mesh.color_attributes.active_color = color
    obj = bpy.data.objects.new("instrument", mesh)
    scene = bpy.context.scene
    scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    material = bpy.data.materials.new("plain")
    mesh.materials.append(material)
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.seed = 20261103
    scene.world = bpy.data.worlds.new("w")
    scene.world.light_settings.distance = 22.0     # millimetres: only what is really near casts onto a thing
    scene.render.bake.target = "VERTEX_COLORS"
    bpy.ops.object.bake(type="AO")
    out = np.zeros(len(joined_p) * 4, dtype=np.float32)
    color.data.foreach_get("color", out)
    ao = out.reshape(-1, 4)[:, 0]
    ao = np.clip(ao, 0, 1)
    print(f"occlusion mean {ao.mean():.3f}, p5 {np.percentile(ao, 5):.3f}")
    bytes_out = np.rint(ao * 255).astype(np.uint8)
    writable = np.frombuffer(data, dtype=np.uint8, count=vertex_count * VERTEX, offset=vertex_at).reshape(-1, VERTEX)
    for v_at, v_count, joined_at in ranges:
        writable[v_at:v_at + v_count, 28] = bytes_out[joined_at:joined_at + v_count]     # the bake's first channel
    open(path, "wb").write(bytes(data))


main()
