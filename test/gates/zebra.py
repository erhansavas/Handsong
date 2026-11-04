# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""Zebra stripes on a shell, so its reflections can be judged: on a continuous surface they run as smooth unbroken lines.
Runs inside Blender: Blender -b --factory-startup --python test/gates/zebra.py -- pack.hsmesh part out_prefix"""
import math
import struct
import sys
import numpy as np
import bpy

NAME, VERTEX = 32, 32
VIEWS = {"top": (0, 170, 170), "rear": (0, -170, -170), "side": (260, 40, 30), "corner": (-190, -60, -190)}


def read_part(path, wanted):
    data = open(path, "rb").read()
    parts, vertices, indices = struct.unpack_from("<III", data, 4)
    size = (len(data) - 16 - vertices * VERTEX - indices * 4) // parts
    vertex_at = 16 + parts * size
    index_at = vertex_at + vertices * VERTEX
    for i in range(parts):
        at = 16 + i * size
        name = data[at:at + NAME].split(b"\0")[0].decode()
        if name != wanted:
            continue
        v_count, i_count, v_first, i_first = struct.unpack_from("<IIII", data, at + NAME)
        words = np.frombuffer(data, dtype="<u4", count=v_count * 8, offset=vertex_at + v_first * VERTEX).reshape(-1, 8)
        positions = words[:, :3].view("<f4").astype(np.float32) * 1000.0
        packed = words[:, 3]
        normals = np.stack([(packed << 22).astype(np.int32) >> 22, (packed << 12).astype(np.int32) >> 22, (packed << 2).astype(np.int32) >> 22], -1) / 511.0
        faces = np.frombuffer(data, dtype="<u4", count=i_count, offset=index_at + i_first * 4).astype(np.int32)
        return positions, normals, faces
    raise SystemExit(f"no part named {wanted}")


def make_mesh(positions, normals, faces):
    mesh = bpy.data.meshes.new("shell")
    mesh.vertices.add(len(positions))
    mesh.vertices.foreach_set("co", positions.ravel())
    count = len(faces) // 3
    mesh.loops.add(len(faces))
    mesh.loops.foreach_set("vertex_index", faces)
    mesh.polygons.add(count)
    mesh.polygons.foreach_set("loop_start", np.arange(count, dtype=np.int32) * 3)
    mesh.update()
    mesh.validate(verbose=False)
    mesh.shade_smooth()
    if len(mesh.vertices) == len(normals):
        mesh.normals_split_custom_set_from_vertices(normals.tolist())
    return mesh


def stripes(mesh):
    """White and black bands, by the direction a mirror at that point would reflect."""
    material = bpy.data.materials.new("zebra")
    material.use_nodes = True
    tree = material.node_tree
    tree.nodes.clear()
    out = tree.nodes.new("ShaderNodeOutputMaterial")
    coord = tree.nodes.new("ShaderNodeTexCoord")
    split = tree.nodes.new("ShaderNodeSeparateXYZ")
    wave = tree.nodes.new("ShaderNodeMath")
    wave.operation = "SINE"
    gain = tree.nodes.new("ShaderNodeMath")
    gain.operation = "MULTIPLY"
    gain.inputs[1].default_value = 34.0
    edge = tree.nodes.new("ShaderNodeMath")
    edge.operation = "GREATER_THAN"
    edge.inputs[1].default_value = 0.0
    light = tree.nodes.new("ShaderNodeEmission")
    tree.links.new(coord.outputs[6], split.inputs[0])
    tree.links.new(split.outputs[0], gain.inputs[0])
    tree.links.new(gain.outputs[0], wave.inputs[0])
    tree.links.new(wave.outputs[0], edge.inputs[0])
    tree.links.new(edge.outputs[0], light.inputs[1])
    tree.links.new(light.outputs[0], out.inputs[0])
    mesh.materials.append(material)


def main():
    path, name, prefix = sys.argv[sys.argv.index("--") + 1:][:3]
    for leftover in list(bpy.data.objects):
        bpy.data.objects.remove(leftover)
    positions, normals, faces = read_part(path, name)
    # The pack's y is up; Blender's z is, and turning it keeps the same stripes on the same surface.
    turned = np.stack([positions[:, 0], -positions[:, 2], positions[:, 1]], -1)
    normal_turned = np.stack([normals[:, 0], -normals[:, 2], normals[:, 1]], -1)
    mesh = make_mesh(turned, normal_turned, faces)
    stripes(mesh)
    scene = bpy.context.scene
    obj = bpy.data.objects.new("shell", mesh)
    scene.collection.objects.link(obj)
    camera_data = bpy.data.cameras.new("eye")
    camera_data.lens = 70
    camera_data.clip_end = 5000
    camera = bpy.data.objects.new("eye", camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    scene.world = bpy.data.worlds.new("w")
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 16
    scene.cycles.seed = 20261103
    scene.render.resolution_x, scene.render.resolution_y = 1400, 900
    scene.render.image_settings.file_format = "PNG"
    centre = (turned.min(0) + turned.max(0)) / 2
    for view, (x, y, z) in VIEWS.items():
        eye = np.array([x, -z, y], dtype=np.float64) * 1.6 + centre
        camera.location = eye
        forward = centre - eye
        forward /= np.linalg.norm(forward)
        camera.rotation_euler = (math.acos(-forward[2]), 0.0, math.atan2(forward[1], forward[0]) - math.pi / 2)
        scene.render.filepath = f"{prefix}_{view}.png"
        bpy.ops.render.render(write_still=True)
        print("zebra", scene.render.filepath)


main()
