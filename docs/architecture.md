# Architecture

Layers, each only depending on the ones above it in this list.

```
Layer 2  geometry.h            Vec3, Triangle, Mesh, GeometryData: plain data, no logic
         transform.h           Quat, Mat4, TRS, Euler angles
         scene.h               Scene: source meshes + a hierarchy of transformable nodes (components)
Layer 1  obj_importer.h        OBJ text -> GeometryData, or a Scene (one component per `o` object)
         gltf_importer.h       GLB -> Scene (glTF nodes are the components)
         scene_io.h            format selection, the transforms file, the JSON result
         schema.h              checking JSON documents against the schemas in schema/
Layer 3  intersection_verifier GeometryData -> VerificationReport   (knows nothing of files, scenes or viewers)
         VerifyScene           Scene -> world-space GeometryData -> VerificationReport with component indices
         view.h                Scene + report -> the view: placed parts and highlight overlays (what renderers draw)
Layer 4  web_viewer.h          GeometryData + VerificationReport -> a static HTML page (never re-checks anything)
         gui_server.h          the HTTP API + the web GUI page; edits go to the Scene, then VerifyScene
Clients  gui_page.html         the web GUI: draws the view, sends edits
         blender/mesh_verifier the Blender add-on: sends Blender's scene and edits, draws the view
```

Any program can build a `GeometryData` in memory and call `VerifyIntersections` without the importer; the viewers
and renderers only draw what they're given. More formats (FBX, USD) are more Layer 1 importers producing a `Scene`; an
animated sequence is the same verifier called on each frame's geometry.

## The scene

A `Scene` owns the source meshes (never modified after import), the nodes (name, parent, children, optional mesh,
local transform) and a copy of every node's imported transform, for reset. A node's world matrix is the product of
the local matrices from the root down: `world = parentWorld * local`, each local being `T * R * S`.

Edits (`TransformEdit`) replace translation, rotation (Euler degrees, `R = Rz Ry Rx`) and/or scale:

- **local**: the supplied parts of the local TRS are replaced, the rest kept bit for bit.
- **world**: the node's world transform is split into TRS, the supplied parts replaced, and the local transform
  solved as `inverse(parentWorld) * targetWorld`. Under identity parents this is the same as a local edit (exact
  values kept); a translation-only world edit keeps the local rotation and scale and moves only the local origin.
  When the solved local isn't a TRS (shear, from a rotated child of a non-uniformly scaled parent) it's stored as a
  matrix, so the node lands exactly where asked; its nearest TRS is kept for display.

`Apply` validates everything first and restores the previous state if any edit fails, so a batch is all or
nothing. Children aren't touched by an edit: they follow through the hierarchy.

`WorldGeometry` copies each in-scene node's mesh with its vertices multiplied by the world matrix (a node whose
world matrix is exactly the identity keeps the source coordinates bit for bit), and `VerifyScene` runs the
verifier on that, renumbering the meshes back to node indices. Rotation matrices have rounding noise around 0 and
+-1 removed, so axis-aligned rotations stay exact.

## Importers

- **OBJ**: `ParseOBJ` (one mesh, unchanged); `ParseOBJScene` splits by `o` into components with the vertices their
  faces use. A file with at most one object gives exactly the `ParseOBJ` mesh as its single component.
- **GLB**: header and chunks checked against the file length; the JSON parsed with nlohmann/json; buffers from
  the BIN chunk, base64 data URIs or files next to the model; accessors read with stride, normalization and sparse
  substitution, all bounds-checked; primitives of modes 4/5/6 unrolled into one triangle list per glTF mesh, with
  indices checked against the primitive's vertex count. Nodes keep their glTF indices; matrices are split into TRS
  when they have no shear. Only nodes reachable from the active scene are drawn and verified.

## The GUI

`GuiApp` holds the scene, the last report and an undo/redo history of node transforms, and answers HTTP requests
(it's socket-free, so tests drive it directly). `RunGuiServer` serves it with cpp-httplib on 127.0.0.1, the app
behind a mutex. Request bodies are parsed with nlohmann/json and checked against their JSON Schema (`schema/`,
embedded at build time, checked with valijson) before anything is applied; the tests check every response against
its schema too.

The engine decides what is highlighted: `BuildView` (`src/view/`) turns the scene and its report into the view, the
placed parts plus overlays of world-space triangles and lines with a colour and a role. Renderers draw the overlays
as given. The page (`src/server/gui_page.html`, embedded at build time) mirrors the hierarchy as three.js objects,
uses `TransformControls` for the gizmo and sends each change as a local edit (a drag is throttled to one request in
flight and grouped into one undo step by its `group`); the server re-verifies and returns the state with the new
view, so the page always shows the server's result.

## The Blender add-on

`blender/mesh_verifier/` uses the same API, with Blender as the editor. **Verify** writes every visible mesh object
into a GLB in memory (one root node per object, placed by its world matrix; triangle i of a node is the object's
loop triangle i, so results map straight back to Blender's faces) and loads it with `POST /api/load?...&up=z`. In
**Live** mode a depsgraph handler notes moved objects and changed meshes; a timer sends moved objects as world-space
edits (`POST /api/transform`, translation, XYZ Euler and scale decomposed from the world matrix; Blender's XYZ Euler
is the contract's `R = Rz Ry Rx`) and the whole scene again when a mesh changed or a matrix has shear. The add-on
draws the returned view's overlays with Blender's `gpu` module in the viewport's 3D pass; Blender's own camera,
selection, gizmos and undo are used as they are. It never copies transforms back from the engine.

## The verifier

1. **Validation.** Every triangle index must be inside its mesh's vertex array and every coordinate finite;
   otherwise a `ValidationError` is thrown before anything is checked. Nothing else is rejected.
2. **Broad phase.** Every triangle of every mesh goes into one bounding volume hierarchy (axis-aligned boxes,
   median split on the widest axis, up to 4 triangles per leaf). Traversing the tree against itself yields every
   pair of triangles whose boxes overlap (closed boxes, so touching boxes count), each pair once.
3. **Narrow phase**, exact, for each candidate pair (`triangle_intersection.cpp`):
   - The side of each triangle's three vertices against the other's plane: `Orient3D`, exact.
   - All on one side (or on the plane only at a vertex or edge, the rest on one side): no intersection.
   - All six on the planes: the coplanar case. Both triangles are projected to 2D along the normal's largest
     axis and tested with exact `Orient2D`: their interiors overlap unless some edge has the other triangle wholly
     on its outer side or on its line (the separating axis theorem for convex polygons, closed half-planes).
   - Otherwise each triangle has vertices strictly on both sides of the other's plane, and each meets the other's
     plane along a segment; they intersect exactly when those segments overlap over a nonzero length. Put in a
     canonical form (each triangle's lone vertex first and on the positive side of the other's plane), this is two
     exact signs: `Orient3D(a, b, p, q) < 0` and `Orient3D(a, c, p, r) > 0` (the test of Guigue and Devillers,
     made strict, so ends that only meet count as touching).
   - The weight and segment are then computed in floating point.
4. **Report.** Pairs are stored with the lower triangle first and sorted, so the output is the same whatever the
   input order or the tree's shape.

## Exactness

`predicates.cpp` computes the sign of the orientation determinants exactly. It first evaluates them in floating
point and accepts the sign when it exceeds Shewchuk's proven error bound; otherwise it recomputes them with
floating-point expansions (sums of non-overlapping doubles, with error-free sums and products, using `std::fma`).
This needs plain IEEE double arithmetic: the build turns off fused contractions for that file. It also assumes no
underflow in the intermediate products (coordinates above about 1e-150 in size), which covers any real mesh.

## The static web viewer

`WebViewer::Render` writes one self-contained HTML page: the geometry and the report embedded as JSON, drawn
with three.js from a CDN. Layers: the base geometry (neutral, flat-shaded), the triangles the report flags (red),
the wireframe and the intersection segments (yellow), each its own object so more overlays can be added later.
Orbit, pan and zoom with the mouse. (It predates the view and works out its red triangles from the report itself.)
