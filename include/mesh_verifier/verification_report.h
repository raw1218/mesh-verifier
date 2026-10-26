// The verifier's output: which triangle pairs intersect, how, and by how much.
#pragma once

#include <cstdint>
#include <vector>

#include "mesh_verifier/geometry.h"

namespace mesh_verifier {

struct TriangleRef {
    uint32_t meshIndex = 0;
    uint32_t triangleIndex = 0;
};

inline bool operator==(const TriangleRef& p, const TriangleRef& q) {
    return p.meshIndex == q.meshIndex && p.triangleIndex == q.triangleIndex;
}
inline bool operator<(const TriangleRef& p, const TriangleRef& q) {
    return p.meshIndex != q.meshIndex ? p.meshIndex < q.meshIndex : p.triangleIndex < q.triangleIndex;
}

enum class IntersectionType {
    // Each triangle passes through the other's plane and the two cross along a segment of nonzero length.
    Crossing,
    // The triangles lie in one plane and overlap over a nonzero area.
    CoplanarOverlap,
};

struct Intersection {
    TriangleRef a;  // always a < b
    TriangleRef b;
    IntersectionType type = IntersectionType::Crossing;
    // How much they intersect: the length of the intersection segment (Crossing) or the overlap area
    // (CoplanarOverlap), in the input's units. Always > 0 in the exact sense; computed in floating point.
    double weight = 0.0;
    // Crossing only: the intersection segment's two ends.
    Vec3 segmentStart, segmentEnd;
};

struct VerificationReport {
    // Each intersecting pair once, sorted by (a, b).
    std::vector<Intersection> intersections;
};

}  // namespace mesh_verifier
