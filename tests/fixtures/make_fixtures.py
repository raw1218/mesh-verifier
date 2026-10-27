#!/usr/bin/env python3
"""Writes the GLB test fixtures (standard library only). Run from anywhere: python3 tests/fixtures/make_fixtures.py"""
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))

CUBE_POSITIONS = [(x, y, z) for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)]
# 12 triangles of the unit cube centred on the origin (vertex i has bits x=4, y=2, z=1)
CUBE_TRIANGLES = [
    (0, 1, 3), (0, 3, 2),  # x = -0.5
    (4, 6, 7), (4, 7, 5),  # x = +0.5
    (0, 4, 5), (0, 5, 1),  # y = -0.5
    (2, 3, 7), (2, 7, 6),  # y = +0.5
    (0, 2, 6), (0, 6, 4),  # z = -0.5
    (1, 5, 7), (1, 7, 3),  # z = +0.5
]


def glb(document, binary):
    while len(binary) % 4:
        binary += b"\0"
    document["buffers"] = [{"byteLength": len(binary)}]
    text = json.dumps(document, separators=(",", ":")).encode()
    while len(text) % 4:
        text += b" "
    total = 12 + 8 + len(text) + 8 + len(binary)
    return (struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(text), 0x4E4F534A) + text
            + struct.pack("<II", len(binary), 0x004E4942) + binary)


def cube_buffers():
    """bufferViews and accessors for one cube mesh: accessor 0 positions, accessor 1 indices."""
    pos = b"".join(struct.pack("<3f", *p) for p in CUBE_POSITIONS)
    idx = b"".join(struct.pack("<3H", *t) for t in CUBE_TRIANGLES)
    binary = pos + idx
    views = [{"buffer": 0, "byteOffset": 0, "byteLength": len(pos)}, {"buffer": 0, "byteOffset": len(pos), "byteLength": len(idx)}]
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3", "min": [-0.5] * 3, "max": [0.5] * 3},
        {"bufferView": 1, "componentType": 5123, "count": 36, "type": "SCALAR"},
    ]
    return binary, views, accessors


def write(name, data):
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(data)


def hierarchy():
    """
    node 0 "root"   (no mesh) children 1, 2
    node 1 "cubeA"  cube at the origin
    node 2 "arm"    cube, translation (3, 0, 0), children 3
    node 3 "hand"   cube, translation (0, 3, 0) relative to arm: (3, 3, 0) in the world
    node 4 "unused" cube at the origin, not in the scene
    No two cubes in the scene touch: no intersections as imported.
    """
    binary, views, accessors = cube_buffers()
    doc = {
        "asset": {"version": "2.0", "generator": "mesh-verifier fixtures"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "root", "children": [1, 2]},
            {"name": "cubeA", "mesh": 0},
            {"name": "arm", "mesh": 0, "translation": [3, 0, 0], "children": [3]},
            {"name": "hand", "mesh": 0, "translation": [0, 3, 0]},
            {"name": "unused", "mesh": 0},
        ],
        "meshes": [{"name": "cube", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "bufferViews": views,
        "accessors": accessors,
    }
    write("hierarchy.glb", glb(doc, binary))


def overlapping():
    """Two root cubes overlapping: node 0 at the origin, node 1 at (0.5, 0.25, 0.1)."""
    binary, views, accessors = cube_buffers()
    doc = {
        "asset": {"version": "2.0"},
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": "a", "mesh": 0}, {"name": "b", "mesh": 0, "translation": [0.5, 0.25, 0.1]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "bufferViews": views,
        "accessors": accessors,
    }
    write("overlapping.glb", glb(doc, binary))


def transformed_parent():
    """
    node 0 "parent": rotated 90 degrees about Z, scaled 2, translated (10, 0, 0); children 1
    node 1 "child":  cube, translation (1, 0, 0) local: (10, 2, 0) in the world, edge length 2
    node 2 "probe":  cube at the origin (a root)
    """
    binary, views, accessors = cube_buffers()
    h = math.sqrt(0.5)
    doc = {
        "asset": {"version": "2.0"},
        "scenes": [{"nodes": [0, 2]}],
        "nodes": [
            {"name": "parent", "translation": [10, 0, 0], "rotation": [0, 0, h, h], "scale": [2, 2, 2], "children": [1]},
            {"name": "child", "mesh": 0, "translation": [1, 0, 0]},
            {"name": "probe", "mesh": 0},
        ],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "bufferViews": views,
        "accessors": accessors,
    }
    write("transformed_parent.glb", glb(doc, binary))


if __name__ == "__main__":
    hierarchy()
    overlapping()
    transformed_parent()
