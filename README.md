# Mesh Verifier

Checks triangle meshes for triangles that pass through each other, and shows the intersecting triangles in red: in
the terminal, in an interactive web viewer, or inside Blender.

It's the baseline of a tool for verifying the geometry that character creation, rigging and animation produce:
later it will take the vertex positions of animated characters frame by frame. For now it does one thing well:
**find every pair of triangles that intersect**, exactly.

One C++ engine does all the checking and decides what to highlight; the web viewer and the Blender add-on only draw
what it sends them, over the HTTP API in [contracts.md](contracts.md).

## What counts as an intersection

Two triangles intersect when

- **Crossing**: each passes through the other's plane (it has vertices strictly on both sides of it) and the two
  crossings overlap along a segment of nonzero length: one pokes through the other; or
- **CoplanarOverlap**: they lie in one plane and overlap over a nonzero area.

Touching is not intersecting. Triangles that meet only at a point or along an edge (a shared vertex or edge, a
vertex or an edge resting on the other triangle) are not reported.

Every yes/no decision is **exact**: it uses exact orientation predicates on the input doubles (a fast
floating-point evaluation with a proven error bound, falling back to exact arithmetic when that can't decide).
There is no tolerance, so the answer doesn't depend on the mesh's scale or position, and two runs on the same
input always agree.

Each reported pair carries a **weight**, how much they intersect: the length of the intersection segment
(Crossing) or the overlap area (CoplanarOverlap), in the mesh's units, plus the segment's ends. The weight is
computed in floating point.

Zero-area triangles and repeated indices are taken as they are; a zero-area triangle has no interior to cross or
overlap, so it is never reported.

## Build and test

Needs CMake 3.24+ and a C++20 compiler; Linux, Windows or macOS. CMake fetches the libraries at configure time:
[nlohmann/json](https://github.com/nlohmann/json) (JSON), [valijson](https://github.com/tristanpenman/valijson)
(checking documents against the JSON Schemas in `schema/`), [cpp-httplib](https://github.com/yhirose/cpp-httplib)
(the local HTTP server) and, for the tests, Catch2 3 (unless one is installed).

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The CTest suite includes the Catch2 unit tests and `tests/cli_tests.py`, end-to-end tests of `mesh-verify` (Python 3,
standard library only). The GLB fixtures are written by `tests/fixtures/make_fixtures.py`.

Every push is built and tested on Linux and Windows by GitHub Actions. The run keeps, as downloads, the report and
static page of every example and model, and the `mesh-verify` binaries.

## Use

The command line contract is in [contracts.md](contracts.md):

```
mesh-verify --format <obj|glb> <model> [--headless] [--port <port>] [--transforms <file>] [--json]
```

```
./build/mesh-verify --format glb robot.glb --headless --json                    # verify, print JSON pairs
./build/mesh-verify --format glb robot.glb --headless --transforms edits.json   # verify after component edits
./build/mesh-verify --format obj model.obj                                      # the GUI on http://127.0.0.1:8080/
./build/mesh-verify --format obj model.obj --port 9000
./build/mesh-verifier-web model.glb -o model.html                               # a static report page
```

`--format`, not the extension, picks the loader. Exit code 0 when verification ran (whatever it found), 1 for any
error (message on stderr). Without `--headless` the URL is printed once the server is listening; `--port 0` picks a
free port. Pairs are `{"a": {"node", "triangle"}, "b": {...}}`: component index and triangle index within it.

### Components and transforms

A loaded model is a **scene**: source meshes, placed by a hierarchy of **components** (nodes). Each component has a
local transform (translation, rotation, scale) relative to its parent; its world transform is
`parentWorld * local`, so transforming a component carries its children along.

- **GLB**: the components are the glTF nodes, with the same indices, names, hierarchy and transforms. A node's
  triangles are those of all its mesh's primitives, in order.
- **OBJ**: each `o` object with faces is a component (faces before the first `o` form one more). A file with at
  most one object is a single component, exactly the mesh it always was. OBJ components are roots with identity
  transforms (so they rotate and scale about the origin).

An edit replaces any of translation, rotation and scale, in `local` space (relative to the parent) or `world`
space (the result in the world, whatever the parents do; the local transform is solved for). Omitted parts keep their
current value. Rotations are Euler angles in degrees, applied about X, then Y, then Z (fixed axes), i.e.
`R = Rz * Ry * Rx` (scipy `from_euler("xyz")`, three.js order `"ZYX"`). A world edit under a non-uniformly scaled,
rotated parent may need shear, which TRS can't express; then the local transform is kept as a matrix, so the world
result is still exactly what was asked.

The source geometry is never modified: verification runs on a world-space copy, and the imported transforms are
kept, so a component, a component with its subtree, or everything can be reset at any time.

### The GUI

`mesh-verify` without `--headless` serves an interactive page on `http://127.0.0.1:<port>/`:

- **Load** an OBJ or GLB with *Open...* (or drop it on the viewport); the format follows the extension or the
  format menu.
- **Select** a component by clicking it in the viewport or in the component tree (which shows the hierarchy,
  transformed components and per-component intersection counts). Its children are highlighted with it.
- **Transform** it with the gizmo: `W` move, `E` rotate, `R` scale, `Q` toggle local/world space; hold `Shift` (or
  tick *Snap*) to snap. Or type exact values in the Transform panel, in local or world space.
- **Reset** the component, the component with its children, or everything; `Ctrl+Z` / `Ctrl+Shift+Z` undo and redo
  (a whole drag is one step). `F` frames the selection, `Esc` deselects. Orbit with the left mouse button, pan with
  the right, zoom with the wheel.

Every edit is verified again on the server, and the page redraws the highlight layers the server sends (the view):
intersecting triangles in red, the crossings in yellow. The page has no highlighting logic of its own, so any other
renderer (such as a Blender add-on) shows exactly the same. The page loads three.js from a local npm install of
`three` when one is found (at build time, or `$MESH_VERIFIER_THREE_DIR` at run time), otherwise from a CDN.

The page uses the HTTP API in [contracts.md](contracts.md#http-api), which scripts and other renderers can use too.
Every request and response is described by a JSON Schema in [`schema/`](schema/). In the browser console,
`meshVerifier` exposes `api`, `select`, `edit` and `reset`.

### In Blender

`blender/mesh_verifier/` is a Blender add-on (4.2 or later). It adds a **Mesh Verifier** tab to the 3D viewport's
sidebar (`N`):

- **Verify** checks every visible mesh object; each object is a component. Intersecting triangles are drawn red and
  the crossing lines yellow, over the meshes (**X-Ray**) or not.
- **Live** (on by default) checks again as you work: moving, rotating or scaling an object sends only its new
  transform; editing a mesh sends the scene again when you leave Edit Mode.
- The panel shows the number of intersecting pairs, a checkbox per highlight layer, and the red triangles per
  object. Blender's own selection, gizmos, camera and undo are used as they are.

The add-on starts `mesh-verify` itself and talks to it on localhost. To install: put the `mesh-verify` executable
(from a build, or from a CI run's downloads) in `blender/mesh_verifier/bin/`, zip the `mesh_verifier` folder, and
in Blender use *Edit > Preferences > Add-ons > Install from Disk*. The executable's path can also be set in the
add-on's preferences.

### From C++

```cpp
#include "mesh_verifier/scene.h"
#include "mesh_verifier/scene_io.h"

using namespace mesh_verifier;

Scene scene = LoadScene("robot.glb", ModelFormat::Glb);      // or LoadGLB / LoadOBJScene
TransformEdit edit;
edit.node = 2;
edit.space = Space::World;
edit.rotationEuler = Vec3{0, 0, 45};
scene.Apply(edit);                                           // throws SceneError (changing nothing) if invalid
VerificationReport report = VerifyScene(scene);              // TriangleRef::meshIndex is the component index
scene.Reset(2, /*subtree=*/true);                            // or ResetAll()
```

The single-mesh API still works: `LoadOBJ` gives a `GeometryData`, `VerifyIntersections` checks any `GeometryData`,
and `WebViewer` writes a static page.

## OBJ input

`v` lines are vertices, `f` lines are triangles (`i`, `i/t`, `i//n` or `i/t/n`; only the position index is used;
negative indices count back). Faces with other than three vertices are an error; `o` lines start components (see
above); every other kind of line is ignored. Positions are kept exactly as written.

## GLB input

glTF 2.0 binary. Geometry and hierarchy only: materials, textures, animations, skins and cameras are ignored.
Triangle primitives (also strips and fans) with float or quantized positions, indexed or not, sparse accessors,
node matrices or TRS, embedded or external buffers. The displayed scene is the file's `scene` (nodes outside it are
listed but not drawn or verified). Draco / meshopt compressed geometry is not supported.

## Layout

```
include/mesh_verifier/   the public headers: geometry, transforms, scene, importers, verifier, report, viewers
src/import/              the OBJ and GLB importers
src/scene/               transforms, the scene (hierarchy, edits, reset, world geometry), the transforms file
src/verification/        the verifier: exact predicates, the triangle-pair test, the BVH broad phase
src/view/                the view: what renderers draw (placed parts and highlight overlays)
src/viewer/              the static web page
src/server/              the GUI: the HTTP server and API, and the page (gui_page.html)
src/util/                checking JSON against the schemas
schema/                  JSON Schemas of every document read or written
cmake/                   embedding the schemas and the page in the library
tools/                   mesh-verify and mesh-verifier-web
blender/mesh_verifier/   the Blender add-on
tests/                   Catch2 tests, the CLI tests and their fixtures
examples/                example meshes
docs/architecture.md     the design
```

## Known limitations

- The GUI server listens on localhost only and is meant for one user.
- Verification is exact on the world-space coordinates, which are rounded once when a transform is applied
  (axis-aligned quarter turns, unit scales and integer translations of integer data stay exact).
- One rule only: triangle intersections. No other checks, and nothing is repaired.
- Every edit re-checks the whole scene, so on large meshes (tens of thousands of triangles) each update takes a
  noticeable time; the Blender add-on waits for each answer, so Blender pauses while it does.
- The Blender add-on checks each mesh's own shape: modifiers (subdivision, armature) are not applied.
- `mesh-verifier-web`'s static page still works out its highlighting from the report itself rather than from the
  engine's view.

MIT licence.
