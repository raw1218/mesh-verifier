"""Turns a VRM avatar's expressions into a rig file (schema/rig.schema.json).

A .vrm file is a GLB, so mesh-verify loads its meshes, skin and morph targets as they are; this writes the part
glTF can't carry: the expressions (happy, blink, aa, lookUp, ...), each a set of morph target weights.

    python vrm_to_rig.py Seed-san.vrm Seed-san.rig.json

Every expression becomes a control from 0 to 1 driving its morph targets. VRM's rules become rig stages:
  isBinary          the value jumps from 0 to 1 at 0.5
  override "blend"  the overridden expressions (blink, mouth or look-at) are multiplied by 1 - this expression
  override "block"  the overridden expressions are 0 while this expression is above 0
Not carried over (reported): texture and material colour bindings, bone look-at, spring bones and constraints.
Reads VRM 1.0 (VRMC_vrm) and VRM 0.x (VRM.blendShapeMaster).
"""
import json
import re
import struct
import sys

GROUPS = {
    "Blink": ["blink", "blinkLeft", "blinkRight"],
    "Mouth": ["aa", "ih", "ou", "ee", "oh"],
    "LookAt": ["lookUp", "lookDown", "lookLeft", "lookRight"],
}
# VRM 0.x preset names, as VRM 1.0 names them.
PRESETS_0X = {
    "joy": "happy", "angry": "angry", "sorrow": "sad", "fun": "relaxed", "a": "aa", "i": "ih", "u": "ou",
    "e": "ee", "o": "oh", "blink": "blink", "blink_l": "blinkLeft", "blink_r": "blinkRight",
    "lookup": "lookUp", "lookdown": "lookDown", "lookleft": "lookLeft", "lookright": "lookRight", "neutral": "neutral",
}
BINARY_STEP = [[0.4999, 0], [0.5, 1]]


def read_glb_json(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"glTF":
        sys.exit(f"{path}: not a GLB/VRM file")
    length, kind = struct.unpack_from("<I4s", data, 12)
    if kind != b"JSON":
        sys.exit(f"{path}: the first chunk isn't JSON")
    return json.loads(data[20:20 + length])


def rig_name(name):
    """A name the rig schema accepts: letters, digits, _ and ."""
    name = re.sub(r"[^A-Za-z0-9_.]", "_", name)
    return name if re.match(r"[A-Za-z_]", name) else "_" + name


def morph_names(gltf, mesh):
    names = (gltf["meshes"][mesh].get("extras") or {}).get("targetNames")
    if not names:
        sys.exit(f"mesh {mesh} has morph targets without names (extras.targetNames)")
    return names


def expressions_1(gltf, vrm):
    """VRM 1.0: [(name, morph binds, isBinary, {group: override}, unsupported binds)]."""
    out = []
    groups = vrm.get("expressions", {})
    for name, e in list(groups.get("preset", {}).items()) + list(groups.get("custom", {}).items()):
        binds = []
        for b in e.get("morphTargetBinds", []):
            mesh = gltf["nodes"][b["node"]]["mesh"]
            binds.append((morph_names(gltf, mesh)[b["index"]], b["weight"]))
        overrides = {g: e.get("override" + g, "none") for g in GROUPS}
        dropped = [k for k in ("materialColorBinds", "textureTransformBinds") if e.get(k)]
        out.append((name, binds, bool(e.get("isBinary")), overrides, dropped))
    return out


def expressions_0x(gltf, vrm):
    """VRM 0.x: weights are 0 to 100 and there are no overrides."""
    out = []
    for g in vrm.get("blendShapeMaster", {}).get("blendShapeGroups", []):
        preset = g.get("presetName", "unknown")
        name = PRESETS_0X.get(preset, g.get("name", preset)) if preset != "unknown" else g.get("name", "custom")
        binds = [(morph_names(gltf, b["mesh"])[b["index"]], b["weight"] / 100) for b in g.get("binds", [])]
        dropped = ["materialValues"] if g.get("materialValues") else []
        out.append((name, binds, bool(g.get("isBinary")), {k: "none" for k in GROUPS}, dropped))
    return out


def build_rig(name, expressions):
    controls, values, combinations, morphs, notes = [], [], [], [], []
    names = {e[0]: rig_name(e[0]) for e in expressions}
    # Which expressions override each expression, and how.
    overridden_by = {e[0]: [] for e in expressions}
    for expr, _, binary, overrides, _ in expressions:
        for group, how in overrides.items():
            if how == "none":
                continue
            keep = f"{names[expr]}.keep_{group.lower()}"
            if binary:
                keep_map = [[0.4999, 1], [0.5, 0]]
            elif how == "block":
                keep_map = [[0, 1], [0.001, 0]]
            else:
                keep_map = [[0, 1], [1, 0]]
            values.append({"name": keep, "control": names[expr], "map": keep_map})
            for target in GROUPS[group]:
                if target in overridden_by and target != expr:
                    overridden_by[target].append(keep)

    for expr, binds, binary, _, dropped in expressions:
        c = names[expr]
        controls.append({"name": c, "min": 0, "max": 1})
        weight = f"{c}.w"
        values.append({"name": weight, "control": c, "map": BINARY_STEP if binary else [[0, 0], [1, 1]]})
        source = weight
        if overridden_by[expr]:
            source = f"{c}.final"
            combinations.append({"name": source, "of": [weight] + overridden_by[expr]})
        for morph, w in binds:
            morphs.append({"morph": morph, "from": source, "weight": w})
        if dropped:
            notes.append(f"{expr}: not carried over: {', '.join(dropped)}")
        if not binds:
            notes.append(f"{expr}: drives no morph targets")
    rig = {"name": name, "controls": controls, "values": values}
    if combinations:
        rig["combinations"] = combinations
    rig["morphs"] = morphs
    return rig, notes


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: vrm_to_rig.py <model.vrm> <out.rig.json>")
    gltf = read_glb_json(sys.argv[1])
    ext = gltf.get("extensions", {})
    if "VRMC_vrm" in ext:
        expressions = expressions_1(gltf, ext["VRMC_vrm"])
        name = ext["VRMC_vrm"].get("meta", {}).get("name", "vrm")
    elif "VRM" in ext:
        expressions = expressions_0x(gltf, ext["VRM"])
        name = ext["VRM"].get("meta", {}).get("title", "vrm")
    else:
        sys.exit(f"{sys.argv[1]}: no VRM extension")
    rig, notes = build_rig(name, expressions)
    for other in ("VRMC_springBone", "VRMC_node_constraint", "secondaryAnimation"):
        if other in ext or other in ext.get("VRM", {}):
            notes.append(f"not carried over: {other}")
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(rig, f, indent=2)
    print(f"{len(rig['controls'])} controls, {len(rig['morphs'])} morph outputs -> {sys.argv[2]}")
    for n in notes:
        print("  " + n)


if __name__ == "__main__":
    main()
