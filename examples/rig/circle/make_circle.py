"""Builds the circle character in Blender and exports it as a GLB next to this script.

    blender -b --factory-startup --python examples/rig/circle/make_circle.py               -> circle.glb
    blender -b --factory-startup --python examples/rig/circle/make_circle.py -- surface    -> circle_surface.glb

A round head (a sphere, flattened front to back) and two eyebrows just in front of it, with a small skeleton: `root`,
and `brow_L` and `brow_R` under it. The head has two morph targets, `squash` and `stretch`.

  - floating (circle.glb): the head is skinned to `root` alone and each eyebrow (`eyebrow_L`, `eyebrow_R`) to its
    bone, so the brows move over the head. Its rig, circle.rig.json, moves the brows with the head's squash.
  - surface (circle_surface.glb): the brows work like muscles in the head's surface. The head's top half is skinned
    to the brow bones, fading from nothing at the middle to all of it near the top, the left side to `brow_L` and
    the right to `brow_R`; the eyebrows are skinned with the same weights as the surface under them, and squash and
    stretch with the head (morph targets of the same names). Raising a brow stretches its quarter of the head; its
    rig is circle_surface.rig.json.

Blender is Z-up and the character faces -Y; the GLB is glTF's Y-up, facing +Z.
"""
import math
import os
import sys

import bpy
import bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
DEPTH = 0.75  # the head's front-to-back scale
SHAPES = {"squash": (1.15, 1.1, 0.75), "stretch": (0.88, 0.92, 1.25)}  # x, y (depth), z (height) scales


def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def surface_weights(co):
    """The surface variant's skin weights at a point: (root, brow_L, brow_R)."""
    top = smoothstep(0.0, 0.7, co.z)          # 0 at the middle (and below), 1 near the top
    left = smoothstep(-0.35, 0.35, co.x)      # the character's left is +X
    return 1 - top, top * left, top * (1 - left)


def add_shapes(obj):
    obj.shape_key_add(name="Basis")
    for name, (sx, sy, sz) in SHAPES.items():
        key = obj.shape_key_add(name=name)
        for i, v in enumerate(obj.data.vertices):
            key.data[i].co = (v.co.x * sx, v.co.y * sy, v.co.z * sz)


def skin(obj, armature, weights):
    """Skins obj to the armature; weights(co) gives a {bone: weight} per vertex."""
    groups = {}
    for v in obj.data.vertices:
        for bone, w in weights(v.co).items():
            if w <= 0:
                continue
            if bone not in groups:
                groups[bone] = obj.vertex_groups.new(name=bone)
            groups[bone].add([v.index], w, "REPLACE")
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
    add_shapes(obj)
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


def main(variant):
    bpy.ops.wm.read_factory_settings(use_empty=True)
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

    if variant == "surface":
        def weights(co):
            r, l, rr = surface_weights(co)
            return {"root": r, "brow_L": l, "brow_R": rr}
        for brow_obj in (brow_l, brow_r):
            add_shapes(brow_obj)
        for obj in (h, brow_l, brow_r):
            skin(obj, armature, weights)
        out = os.path.join(HERE, "circle_surface.glb")
    else:
        skin(h, armature, lambda co: {"root": 1.0})
        skin(brow_l, armature, lambda co: {"brow_L": 1.0})
        skin(brow_r, armature, lambda co: {"brow_R": 1.0})
        out = os.path.join(HERE, "circle.glb")

    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True, export_skins=True, export_morph=True,
                              export_materials="NONE", export_animations=False, export_apply=False)
    print("wrote", out)


main(sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "floating")
