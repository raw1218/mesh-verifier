// Layer 3: geometry verification. Triangle intersection detection.
#pragma once

#include <stdexcept>

#include "mesh_verifier/geometry.h"
#include "mesh_verifier/verification_report.h"

namespace mesh_verifier {

// Thrown before any checking when the input itself is invalid: a triangle index outside its mesh's vertex
// array, or a vertex coordinate that is NaN or infinite. These are input errors, not geometric findings.
class ValidationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Finds every pair of triangles that intersect, within each mesh and between meshes.
//
// A pair intersects when
//  - each triangle passes through the other's plane (it has vertices strictly on both sides of it) and their
//    two crossings overlap over a segment of nonzero length: one pokes through the other (Crossing), or
//  - both lie in one plane and overlap over a nonzero area (CoplanarOverlap).
// Touching is not intersecting: triangles that meet only at a point or along an edge (a shared vertex or edge,
// a vertex or edge resting on the other triangle) are not reported. Every one of these decisions is exact
// (exact orientation predicates on the input doubles), so there is no tolerance and the result doesn't depend
// on the input's scale. Only the reported weight and segment are computed in floating point.
//
// The geometry is not modified. The result is deterministic and sorted.
VerificationReport VerifyIntersections(const GeometryData& geometry);

}  // namespace mesh_verifier
