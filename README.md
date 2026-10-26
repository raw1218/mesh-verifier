# Mesh Verifier

Checks triangle meshes for triangles that pass through each other, and shows the result as a web page with the
intersecting triangles in red.

It's the baseline of a tool for verifying the geometry that character creation, rigging and animation produce:
later it will take the vertex positions of animated characters frame by frame. For now it does one thing well:
**find every pair of triangles that intersect**, exactly.

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

Needs CMake 3.20+ and a C++20 compiler. Nothing graphical.

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Every push is built and tested on Linux by GitHub Actions, which also renders `examples/sphere_through_torus.obj`
and publishes it to GitHub Pages: https://raw1218.github.io/mesh-verifier/

## Use

From the command line:

```
./build/mesh-verify model.obj            # the report; exit code 1 if anything intersects
./build/mesh-verify model.obj --json
./build/mesh-verifier-web model.obj -o model.html   # the web page
```

From C++, with a file or with geometry built in memory:

```cpp
#include "mesh_verifier/intersection_verifier.h"
#include "mesh_verifier/obj_importer.h"
#include "mesh_verifier/web_viewer.h"

using namespace mesh_verifier;

GeometryData geometry = LoadOBJ("raven_head.obj");      // or fill geometry.meshes yourself
VerificationReport report = VerifyIntersections(geometry);
for (const Intersection& hit : report.intersections) {
    // hit.a, hit.b: {meshIndex, triangleIndex}; hit.type; hit.weight
}
WebViewer{}.Write("raven_head.html", geometry, report);
```

## OBJ input

One file is one mesh. `v` lines are vertices, `f` lines are triangles (`i`, `i/t`, `i//n` or `i/t/n`; only the
position index is used; negative indices count back). Faces with other than three vertices are an error; every
other kind of line is ignored. Positions are kept exactly as written.

## Layout

```
include/mesh_verifier/   the public headers: geometry, OBJ importer, verifier, report, web viewer
src/import/              the OBJ importer
src/verification/        the verifier: exact predicates, the triangle-pair test, the BVH broad phase
src/viewer/              the web viewer
tools/                   mesh-verify and mesh-verifier-web
tests/                   Catch2 tests (run by CTest) and their fixtures
examples/                example meshes
docs/architecture.md     the design
```

## Known limitations

- OBJ is the only input format.
- The web page loads three.js from a CDN, so it needs internet access to display.
- One rule only: triangle intersections. No other checks, and nothing is repaired.

MIT licence.
