// The exact triangle-triangle test (internal).
#pragma once

#include <optional>

#include "mesh_verifier/geometry.h"
#include "mesh_verifier/verification_report.h"

namespace mesh_verifier::detail {

struct PairResult {
    IntersectionType type;
    double weight;            // segment length (Crossing) or overlap area (CoplanarOverlap)
    Vec3 segmentStart, segmentEnd;
};

// Whether triangles (a0, a1, a2) and (b0, b1, b2) intersect, as defined in intersection_verifier.h; if so, how
// and by how much. The yes/no answer and its type are exact.
std::optional<PairResult> IntersectTriangles(const Vec3& a0, const Vec3& a1, const Vec3& a2, const Vec3& b0, const Vec3& b1, const Vec3& b2);

}  // namespace mesh_verifier::detail
