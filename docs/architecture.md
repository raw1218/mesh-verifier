# Architecture

Four layers, each only depending on the ones above it in this list.

```
Layer 2  geometry.h            Vec3, Triangle, Mesh, GeometryData: plain data, no logic
Layer 1  obj_importer.h        OBJ text -> GeometryData
Layer 3  intersection_verifier GeometryData -> VerificationReport   (knows nothing of files or viewers)
Layer 4  web_viewer.h          GeometryData + VerificationReport -> an HTML page (never re-checks anything)
```

Any program can build a `GeometryData` in memory and call `VerifyIntersections` without the importer; the viewer
only draws a report it's given. Future formats (glTF, FBX, USD) are more Layer 1 importers producing the same
`GeometryData`; an animated sequence is the same verifier called on each frame's `GeometryData`.

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

## The web viewer

`WebViewer::Render` writes one self-contained HTML page: the geometry and the report embedded as JSON, drawn
with three.js from a CDN. Layers: the base geometry (neutral, flat-shaded), the triangles the report flags (red),
the wireframe and the intersection segments (yellow), each its own object so more overlays can be added later.
Orbit, pan and zoom with the mouse.
