"""Mesh Verifier for Blender.

Checks the scene's meshes for intersecting triangles with mesh-verify and draws what it reports: intersecting
triangles in red, the lines along which triangles cross in yellow. The add-on has no checking or highlighting logic
of its own: it sends the scene to mesh-verify's local HTTP API (contracts.md in the repository) and draws the view it
gets back, exactly as the web GUI does.

  - Verify sends every visible mesh object (one glTF node each, in world space, triangle i = Blender's loop triangle
    i) and draws the result.
  - Live: moving, rotating or scaling an object sends only its new transform; editing a mesh sends the scene again.
"""

bl_info = {
    "name": "Mesh Verifier",
    "author": "raw1218",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "3D Viewport > Sidebar (N) > Mesh Verifier",
    "description": "Finds intersecting triangles with mesh-verify and draws them in the viewport",
    "category": "Mesh",
}

import json
import math
import os
import struct
import subprocess
import tempfile
import urllib.error
import urllib.request

import bpy
import gpu
import numpy as np
from gpu_extras.batch import batch_for_shader
from mathutils import Matrix

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- the engine (mesh-verify, a local process)

class Engine:
    """One mesh-verify process serving its API on a free localhost port."""

    process = None
    url = None

    @classmethod
    def executable(cls):
        prefs = bpy.context.preferences.addons.get(__package__)
        path = prefs.preferences.executable if prefs else ""
        if path:
            return bpy.path.abspath(path)
        return os.path.join(HERE, "bin", "mesh-verify.exe" if os.name == "nt" else "mesh-verify")

    @classmethod
    def ensure(cls):
        if cls.process and cls.process.poll() is None:
            return
        exe = cls.executable()
        if not os.path.isfile(exe):
            raise RuntimeError(f"mesh-verify not found at {exe} (set its path in the add-on's preferences)")
        # The engine starts with a model; it starts with an empty one, and the scene is loaded over the API.
        empty = os.path.join(tempfile.gettempdir(), "mesh_verifier_empty.obj")
        open(empty, "w").close()
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        cls.process = subprocess.Popen([exe, "--format", "obj", empty, "--port", "0"], stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, creationflags=flags)
        line = cls.process.stdout.readline().strip()  # the URL, printed once the server is listening
        if not line.startswith("http"):
            err = cls.process.stderr.read() if cls.process.poll() is not None else ""
            cls.stop()
            raise RuntimeError(f"mesh-verify did not start: {err or line}")
        cls.url = line.rstrip("/")

    @classmethod
    def stop(cls):
        if cls.process and cls.process.poll() is None:
            cls.process.terminate()
        cls.process = cls.url = None

    @classmethod
    def request(cls, method, path, body=None, content_type="application/json"):
        cls.ensure()
        data = None
        if body is not None:
            data = body if isinstance(body, (bytes, bytearray)) else json.dumps(body).encode()
        req = urllib.request.Request(cls.url + path, data=data, method=method, headers={"Content-Type": content_type})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            try:
                message = json.load(e).get("error", str(e))
            except Exception:
                message = str(e)
            raise RuntimeError(message) from None


# ---------------------------------------------------------------- the scene as a GLB

def mesh_objects(context):
    return [o for o in context.scene.objects if o.type == "MESH" and o.visible_get()]


def column_major(m):
    return [m[r][c] for c in range(4) for r in range(4)]


def scene_glb(objects):
    """A GLB of the objects: one node per object with triangles, placed by its world matrix. Returns the bytes and
    the objects' names in node order. Triangle i of node n is loop triangle i of that object's mesh."""
    binary = bytearray()
    views, accessors, meshes, nodes, names = [], [], [], [], []

    def view(data):
        while len(binary) % 4:
            binary.append(0)
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
        binary.extend(data)
        return len(views) - 1

    for o in objects:
        me = o.data
        me.calc_loop_triangles()
        nv, nt = len(me.vertices), len(me.loop_triangles)
        if nt == 0:
            continue
        co = np.empty(nv * 3, np.float32)
        me.vertices.foreach_get("co", co)
        tri = np.empty(nt * 3, np.int32)
        me.loop_triangles.foreach_get("vertices", tri)
        points = co.reshape(-1, 3)
        accessors.append({"bufferView": view(co.tobytes()), "componentType": 5126, "count": nv, "type": "VEC3",
                          "min": points.min(0).tolist(), "max": points.max(0).tolist()})
        accessors.append({"bufferView": view(tri.astype(np.uint32).tobytes()), "componentType": 5125,
                          "count": nt * 3, "type": "SCALAR"})
        meshes.append({"name": me.name, "primitives": [{"attributes": {"POSITION": len(accessors) - 2},
                                                         "indices": len(accessors) - 1}]})
        nodes.append({"name": o.name, "mesh": len(meshes) - 1, "matrix": column_major(o.matrix_world)})
        names.append(o.name)

    document = {"asset": {"version": "2.0", "generator": "Mesh Verifier for Blender"},
                "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes}
    if meshes:
        document.update(meshes=meshes, accessors=accessors, bufferViews=views,
                        buffers=[{"byteLength": len(binary)}])
    text = json.dumps(document).encode()
    text += b" " * (-len(text) % 4)
    while len(binary) % 4:
        binary.append(0)
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text
    if binary:
        chunks += struct.pack("<II", len(binary), 0x004E4942) + bytes(binary)
    return struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks, names


# ---------------------------------------------------------------- state and drawing

class State:
    nodes = []          # object names in node order (as last sent)
    objects = []        # every visible mesh object when last sent (some may have no triangles)
    sent = {}           # object name -> world matrix last sent
    view = None         # the engine's last view
    intersections = 0
    batches = []        # (kind, color, batch) per overlay
    error = ""
    dirty = set()       # objects whose transform changed
    resend = False      # the scene's meshes or objects changed
    draw_handle = None


def set_state(state):
    """Takes a state from the engine (state.schema.json): keeps the view and rebuilds the drawing."""
    State.view = state["view"]
    State.intersections = state["intersections"]
    State.batches = []
    if bpy.app.background:  # (no drawing without a window)
        return
    shader = gpu.shader.from_builtin("UNIFORM_COLOR")
    for o in State.view["overlays"]:
        if not o["positions"]:
            continue
        points = np.array(o["positions"], np.float32).reshape(-1, 3)
        kind = "TRIS" if o["kind"] == "triangles" else "LINES"
        State.batches.append((o["id"], o["kind"], tuple(o["color"]), batch_for_shader(shader, kind, {"pos": points})))
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == "VIEW_3D":
                area.tag_redraw()


def draw():
    settings = bpy.context.scene.mesh_verifier
    if not settings.show or not State.batches:
        return
    shader = gpu.shader.from_builtin("UNIFORM_COLOR")
    gpu.state.blend_set("ALPHA")
    gpu.state.depth_test_set("NONE" if settings.xray else "LESS_EQUAL")
    for oid, kind, color, batch in State.batches:
        if oid in settings.hidden.split(","):
            continue
        gpu.state.line_width_set(3.0 if kind == "lines" else 1.0)
        shader.uniform_float("color", color)
        batch.draw(shader)
    gpu.state.line_width_set(1.0)
    gpu.state.depth_test_set("NONE")
    gpu.state.blend_set("NONE")


def send_scene(context):
    """Sends every visible mesh object (POST /api/load) and takes the new state."""
    objects = mesh_objects(context)
    glb, names = scene_glb(objects)
    scene = Engine.request("POST", "/api/load?format=glb&name=blender.glb&up=z", glb, "application/octet-stream")
    State.nodes = names
    State.objects = sorted(o.name for o in objects)
    State.sent = {o.name: o.matrix_world.copy() for o in objects if o.name in names}
    State.dirty.clear()
    State.resend = False
    State.error = ""
    set_state(scene["state"])


def edit_for(node, matrix):
    """The world-space edit for an object, or None when its matrix isn't translation, rotation and scale (shear)."""
    loc, rot, scale = matrix.decompose()
    rebuilt = Matrix.LocRotScale(loc, rot, scale)
    if any(abs(rebuilt[r][c] - matrix[r][c]) > 1e-5 * max(1.0, abs(matrix[r][c])) for r in range(4) for c in range(4)):
        return None
    e = rot.to_euler("XYZ")  # Blender's XYZ Euler is R = Rz Ry Rx: the contract's rotationEuler
    return {"node": node, "space": "world", "translation": list(loc),
            "rotationEuler": [math.degrees(a) for a in e], "scale": list(scale)}


def send_transforms(context):
    """Sends the moved objects' transforms (POST /api/transform, one document, all or nothing)."""
    edits = []
    for name in sorted(State.dirty):
        o = context.scene.objects.get(name)
        if o is None or name not in State.nodes:
            State.resend = True
            continue
        edit = edit_for(State.nodes.index(name), o.matrix_world)
        if edit is None:
            State.resend = True
            continue
        edits.append(edit)
        State.sent[name] = o.matrix_world.copy()
    State.dirty.clear()
    if State.resend:
        send_scene(context)
    elif edits:
        set_state(Engine.request("POST", "/api/transform", {"transforms": edits}))


# ---------------------------------------------------------------- live updates

@bpy.app.handlers.persistent
def on_depsgraph(scene, depsgraph):
    if not scene.mesh_verifier.live or State.view is None:
        return
    for u in depsgraph.updates:
        if isinstance(u.id, bpy.types.Object) and u.id.type == "MESH":
            if u.is_updated_geometry:
                State.resend = True
            elif u.is_updated_transform:
                State.dirty.add(u.id.name)
        elif isinstance(u.id, bpy.types.Mesh) and u.is_updated_geometry:
            State.resend = True
        elif isinstance(u.id, bpy.types.Scene) and u.id == scene:
            names = [o.name for o in scene.objects if o.type == "MESH" and o.visible_get()]
            if sorted(names) != State.objects:
                State.resend = True


def tick():
    context = bpy.context
    if context.scene and context.scene.mesh_verifier.live and State.view is not None and (State.dirty or State.resend):
        if context.mode == "EDIT_MESH":
            return 0.1  # (the mesh is sent once edit mode ends)
        try:
            if State.resend:
                send_scene(context)
            else:
                send_transforms(context)
        except Exception as e:  # shown in the panel; the next change tries again
            State.error = str(e)
            State.dirty.clear()
            State.resend = False
    return 0.1


@bpy.app.handlers.persistent
def on_load(_):
    Engine.stop()
    State.view, State.batches, State.nodes, State.sent = None, [], [], {}


# ---------------------------------------------------------------- UI

class MeshVerifierSettings(bpy.types.PropertyGroup):
    live: bpy.props.BoolProperty(name="Live", default=True, description="Check again whenever an object moves or a mesh changes")
    show: bpy.props.BoolProperty(name="Show", default=True, description="Draw the result in the viewport")
    xray: bpy.props.BoolProperty(name="X-Ray", default=True, description="Draw the result through the meshes")
    hidden: bpy.props.StringProperty(default="")


class MESHVERIFIER_OT_verify(bpy.types.Operator):
    bl_idname = "mesh_verifier.verify"
    bl_label = "Verify"
    bl_description = "Check every visible mesh object for intersecting triangles"

    def execute(self, context):
        try:
            send_scene(context)
        except Exception as e:
            State.error = str(e)
            self.report({"ERROR"}, State.error)
            return {"CANCELLED"}
        self.report({"INFO"}, f"{State.intersections} intersecting pairs")
        return {"FINISHED"}


class MESHVERIFIER_OT_clear(bpy.types.Operator):
    bl_idname = "mesh_verifier.clear"
    bl_label = "Clear"
    bl_description = "Stop checking and clear the drawing"

    def execute(self, context):
        on_load(None)
        for area in context.screen.areas:
            area.tag_redraw()
        return {"FINISHED"}


class MESHVERIFIER_OT_toggle_layer(bpy.types.Operator):
    bl_idname = "mesh_verifier.toggle_layer"
    bl_label = "Toggle layer"
    layer: bpy.props.StringProperty()

    def execute(self, context):
        s = context.scene.mesh_verifier
        hidden = set(filter(None, s.hidden.split(",")))
        hidden ^= {self.layer}
        s.hidden = ",".join(sorted(hidden))
        context.area.tag_redraw()
        return {"FINISHED"}


class MESHVERIFIER_PT_panel(bpy.types.Panel):
    bl_label = "Mesh Verifier"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Mesh Verifier"

    def draw(self, context):
        s = context.scene.mesh_verifier
        layout = self.layout
        layout.operator(MESHVERIFIER_OT_verify.bl_idname, icon="VIEWZOOM")
        row = layout.row(align=True)
        row.prop(s, "live", toggle=True)
        row.prop(s, "show", toggle=True)
        row.prop(s, "xray", toggle=True)
        if State.error:
            box = layout.box()
            box.alert = True
            for line in State.error.split("\n")[:4]:
                box.label(text=line, icon="ERROR")
        if State.view is None:
            layout.label(text="Not checked yet")
            return
        n = State.intersections
        layout.label(text="No intersections" if n == 0 else f"{n} intersecting pair{'s' if n != 1 else ''}",
                     icon="CHECKMARK" if n == 0 else "ERROR")
        hidden = set(s.hidden.split(","))
        for o in State.view["overlays"]:
            row = layout.row()
            on = o["id"] not in hidden
            op = row.operator(MESHVERIFIER_OT_toggle_layer.bl_idname, text=o["label"],
                              icon="CHECKBOX_HLT" if on else "CHECKBOX_DEHLT", emboss=False)
            op.layer = o["id"]
        if State.view["nodePairs"]:  # the engine's set of intersecting objects, listed as sent
            box = layout.box()
            box.label(text="Intersecting objects")
            for p in State.view["nodePairs"]:
                a, b = p["a"]["name"], p["b"]["name"]
                text = f"{a} (itself)" if p["a"]["node"] == p["b"]["node"] else f"{a} × {b}"
                row = box.row()
                row.label(text=text, icon="MESH_DATA")
                row.label(text=f"{p['pairs']} pairs")
        layout.operator(MESHVERIFIER_OT_clear.bl_idname, icon="X")


class MeshVerifierPreferences(bpy.types.AddonPreferences):
    bl_idname = __package__

    executable: bpy.props.StringProperty(name="mesh-verify", subtype="FILE_PATH",
                                         description="The mesh-verify program (empty: the one in the add-on's bin folder)")

    def draw(self, context):
        self.layout.prop(self, "executable")


classes = (MeshVerifierSettings, MESHVERIFIER_OT_verify, MESHVERIFIER_OT_clear, MESHVERIFIER_OT_toggle_layer,
           MESHVERIFIER_PT_panel, MeshVerifierPreferences)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    bpy.types.Scene.mesh_verifier = bpy.props.PointerProperty(type=MeshVerifierSettings)
    State.draw_handle = bpy.types.SpaceView3D.draw_handler_add(draw, (), "WINDOW", "POST_VIEW")
    bpy.app.handlers.depsgraph_update_post.append(on_depsgraph)
    bpy.app.handlers.load_post.append(on_load)
    bpy.app.timers.register(tick, persistent=True)


def unregister():
    Engine.stop()
    if bpy.app.timers.is_registered(tick):
        bpy.app.timers.unregister(tick)
    bpy.app.handlers.depsgraph_update_post.remove(on_depsgraph)
    bpy.app.handlers.load_post.remove(on_load)
    if State.draw_handle:
        bpy.types.SpaceView3D.draw_handler_remove(State.draw_handle, "WINDOW")
        State.draw_handle = None
    del bpy.types.Scene.mesh_verifier
    for c in reversed(classes):
        bpy.utils.unregister_class(c)
