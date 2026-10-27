# Programmatic Contract

Use the same command for the GUI and headless modes:

```sh
mesh-verify --format <format> <model file path> [--headless] [--port <port>] [--transforms <transforms file path>] [--json]
```

- `--format <format>`: required input format (`obj` or `glb`); the format, not the filename extension, selects the loader.
- `<model file path>`: model to load.
- Without `--headless`, open the GUI with the model loaded and serve it at `http://127.0.0.1:<port>/`. `--port <port>` is optional and defaults to `8080`. Print the URL when the server is ready. Component edits must update the displayed intersections.
- `--headless`: verify in the terminal without opening the GUI.
- `--port <port>`: choose the local GUI server port; ignored in headless mode.
- `[--transforms <transforms file path>]`: optional JSON file of component edits. Without it, use imported transforms.
- `[--json]`: output intersection pairs as JSON for the initially loaded model, after applying any `--transforms`.

The optional transforms file has this format:

```json
{
  "transforms": [
    {
      "node": 2,
      "space": "world",
      "translation": [0, 0, 0],
      "rotationEuler": [0, 0, 45],
      "scale": [1.5, 0.5, 0.5]
    }
  ]
}
```

`transforms` is the list of component edits to apply.

`node` is the component's zero-based index in the loaded scene.

`space` is `local` or `world`; it defaults to `local`.

`translation` is the replacement position on the X, Y, and Z axes.

`rotationEuler` is the replacement rotation around X, Y, and Z, in degrees and XYZ order.

`scale` is the replacement scale on the X, Y, and Z axes.

Each invocation starts from the imported scene. Supplied fields replace the corresponding imported transform values;
omitted fields keep their imported values. Transform edits must not modify source geometry. Invalid input must fail
without partially applying edits.

Write results to stdout in this form:

```json
{
  "intersections": [
    {"a": {"node": 0, "name": "skull", "triangle": 2}, "b": {"node": 3, "name": "masseter", "triangle": 5}},
    {"a": {"node": 1, "name": "jaw", "triangle": 0}, "b": {"node": 3, "name": "masseter", "triangle": 8}}
  ]
}
```

`intersections` contains one entry for each intersecting triangle pair, so it may be empty or contain multiple entries.
`a` and `b` identify the two triangles in a pair. Each `node` is a zero-based component index and `name` that
component's name (from the model; for OBJ the `o` name); each `triangle` is a zero-based triangle index within that
component. A pair is either present or absent; no intersection type or weight is
required. Return exit code `0` whenever verification completes successfully, whether or not intersections are found.
Return exit code `1` for an invalid argument, model, transform file, or other error. Write errors to stderr.

## JSON Schemas

Every JSON document read or written is described by a JSON Schema (draft 7) in [`schema/`](schema/). The schemas
are the definitive form; the examples here illustrate them.

| Document | Schema |
| --- | --- |
| The transforms file | `transforms.schema.json` (each entry: `transform-edit.schema.json`) |
| The `--json` output, `GET /api/intersections` | `intersections.schema.json` |
| `GET /api/view` | `view.schema.json` |
| `GET /api/state`, and every `POST` that changes the scene | `state.schema.json` |
| `GET /api/scene`, `POST /api/load` | `scene.schema.json` |
| `POST /api/reset` body | `reset.schema.json` |
| `GET /api/controls` | `controls.schema.json` |
| `POST /api/controls` body, and its result | `pose.schema.json`, `posed.schema.json` |
| A rig file, and the `mesh-rig --json` report | `rig.schema.json`, `rig-report.schema.json` |
| Every API error | `error.schema.json` |

A document read (the transforms file, a request body) that doesn't match its schema is an error: nothing is applied,
and the message names where in the document the problem is (a JSON Pointer such as `/transforms/0/space`). Fields not
in the schema are errors, and so are values of the wrong type (`"2"` and `2.5` are not node indices).

## HTTP API

Without `--headless`, `mesh-verify` serves this API at `http://127.0.0.1:<port>/`. Every renderer (the web GUI, the
Blender add-on) uses the same API. Request and response bodies are JSON.

| Request | Body | Returns |
| --- | --- | --- |
| `GET /api/scene` | | the scene: source meshes, components and the state |
| `GET /api/state` | | each component's transforms, the number of intersecting pairs, and the view |
| `GET /api/view` | | the view (below) |
| `GET /api/intersections` | | the intersecting pairs, as `--json` writes them |
| `POST /api/transform[?group=<id>]` | one edit, or a transforms document | the state |
| `POST /api/reset` | `{"node": i, "subtree": bool}`, or `{}` for every component | the state |
| `POST /api/undo`, `POST /api/redo` | | the state |
| `POST /api/load?format=obj\|glb&name=<file name>[&up=y\|z]` | the model file's bytes | the scene |
| `GET /api/controls` | | the rig's controls with their ranges and current values (none without a rig) |
| `POST /api/controls` | `{"values": {"brow_raise_L": 0.5}}` | every mesh's new vertex positions and the state |

With `--rig <rig.json>` (and a skinned GLB), `mesh-verify` poses the model with the rig: the scene is the model at
the rig's default controls, one component per part, and `POST /api/controls` poses it again. Loading another model
drops the rig.

- An edit is one entry of a transforms file: `{"node": 2, "space": "world", "translation": [0, 0, 0]}`. A transforms
  document is applied all or nothing.
- Edits sent with the same `group` (for example every step of one drag) are one undo step.
- `up` says which axis is up in the loaded model (default `y`; Blender is `z`). The geometry is never rotated for it.
- Errors are HTTP `400` (`404` for an unknown endpoint) with `{"error": "..."}`. A failed request changes nothing.

## The view: what renderers draw

The engine decides what is highlighted and how; renderers draw it as given. The view lists where each displayed
component is and a list of highlight layers (overlays). Every overlay's points are in world space, so a renderer
draws them without knowing anything about meshes or indices: `triangles` overlays are 3 points per triangle, `lines`
overlays 2 points per segment.

```json
{
  "revision": 12,
  "up": "y",
  "parts": [{"node": 0, "mesh": 0, "world": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}],
  "nodePairs": [
    {"a": {"node": 0, "name": "skull"}, "b": {"node": 3, "name": "masseter"}, "pairs": 14}
  ],
  "overlays": [
    {"id": "intersections", "label": "Intersecting triangles", "kind": "triangles", "role": "error",
     "color": [1, 0.23, 0.19, 1],
     "items": [{"node": 0, "triangle": 57}],
     "positions": [0, 0, 0, 1, 0, 0, 0, 1, 0]},
    {"id": "crossings", "label": "Crossing lines", "kind": "lines", "role": "warning",
     "color": [1, 0.88, 0.3, 1], "items": [], "positions": [0.2, 0.1, 0, 0.5, 0.1, 0]}
  ]
}
```

- `revision` increases with every change to the scene.
- `world` is the component's world matrix: 16 numbers, column-major (as glTF writes them).
- `nodePairs` is the set of components that intersect: one entry per pair of components with at least one
  intersecting triangle pair between them, with how many (`pairs`); `a` and `b` are the same component when its own
  triangles intersect. Ordered by `a`, then `b`. Renderers list it as given.
- `items` (triangles only) names each drawn triangle, in the order of `positions`, for picking and selection.
- `color` is RGBA from 0 to 1. `role` (`error`, `warning`, `info`, `selection`) says what the layer means; a renderer
  may restyle by role, and otherwise uses `color`.
- The engine currently sends two overlays: `intersections`, every triangle in an intersecting pair, and `crossings`,
  the segments along which crossing triangles meet. (Coplanar overlaps have no segment.)
