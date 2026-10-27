"""Builds the circle character in Blender and exports it as circle.glb (next to this script).

    blender -b --factory-startup --python examples/rig/circle/make_circle.py

A round head (a sphere, flattened front to back) and two eyebrows floating just in front of it. A small skeleton:
`root`, with `brow_L` and `brow_R` under it; the head is skinned to `root`, each eyebrow (`eyebrow_L`, `eyebrow_R`)
to its bone. The head has two morph targets, `squash` and `stretch`. Everything the rig does with these is in circle.rig.json.

Blender is Z-up and the character faces -Y; the GLB is glTF's Y-up, facing +Z.
"""
import math
import os

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
DEPTH = 0.75  # the head's front-to-back scale


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def skin_to(obj, armature, bone):
    group = obj.vertex_groups.new(name=bone)
    group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
    obj.parent = armature
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = armature


def head():
    mesh = bpy.data.meshes.new("head")
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=1.0)
    for v in bm.verts:
        v.co.y *= DEPTH
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("head", mesh)
    bpy.context.scene.collection.objects.link(obj)
    for poly in mesh.polygons:
        poly.use_smooth = True
    # morph targets: the whole head squashed (shorter and wider) or stretched (taller and narrower)
    obj.shape_key_add(name="Basis")
    for name, (sx, sy, sz) in {"squash": (1.15, 1.1, 0.75), "stretch": (0.88, 0.92, 1.25)}.items():
        key = obj.shape_key_add(name=name)
        for i, v in enumerate(mesh.vertices):
            key.data[i].co = (v.co.x * sx, v.co.y * sy, v.co.z * sz)
    return obj


def brow(name, x):
    """A slightly arched bar, 0.36 wide, 0.06 tall, 0.06 deep, centred at (x, y, 0.45) in front of the head."""
    z = 0.45
    # the head's surface in front of the brow's inner end and lower edge, and a small gap
    reach = max(abs(x) - 0.18, 0.0)
    y = -DEPTH * math.sqrt(1 - reach ** 2 - (z - 0.05) ** 2) - 0.03 - 0.04
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.x - e.verts[1].co.x) > 0.5], cuts=6)
    for v in bm.verts:
        arch = 0.04 * (1 - (2 * v.co.x) ** 2)
        v.co = (x + v.co.x * 0.36, y + v.co.y * 0.06, z + v.co.z * 0.06 + arch)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj, (x, y, z)


def main():
    reset()
    arm_data = bpy.data.armatures.new("skeleton")
    armature = bpy.data.objects.new("skeleton", arm_data)
    bpy.context.scene.collection.objects.link(armature)
    bpy.context.view_layer.objects.active = armature
    h = head()
    brow_l, at_l = brow("eyebrow_L", 0.35)   # the character's left is +X (it faces -Y)
    brow_r, at_r = brow("eyebrow_R", -0.35)

    bpy.ops.object.mode_set(mode="EDIT")
    root = arm_data.edit_bones.new("root")
    root.head, root.tail = (0, 0, 0), (0, 0, 0.3)
    for name, at in (("brow_L", at_l), ("brow_R", at_r)):
        b = arm_data.edit_bones.new(name)
        b.head, b.tail = at, (at[0], at[1], at[2] + 0.15)
        b.parent = root
    bpy.ops.object.mode_set(mode="OBJECT")

    skin_to(h, armature, "root")
    skin_to(brow_l, armature, "brow_L")
    skin_to(brow_r, armature, "brow_R")

    out = os.path.join(HERE, "circle.glb")
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True, export_skins=True, export_morph=True,
                              export_materials="NONE", export_animations=False, export_apply=False)
    print("wrote", out)


main()
