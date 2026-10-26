// Exact geometric predicates on doubles (internal).
//
// Each returns the exact sign (-1, 0, +1) of a determinant of the input coordinates, as if computed with
// infinite precision. A floating-point evaluation is tried first and accepted when its proven error bound
// (Shewchuk, "Adaptive Precision Floating-Point Arithmetic and Fast Robust Geometric Predicates", 1997) shows
// the sign can't be wrong; otherwise the determinant is recomputed exactly with floating-point expansions.
#pragma once

#include "mesh_verifier/geometry.h"

namespace mesh_verifier::detail {

// Sign of ((b - a) x (c - a)) . (d - a): positive when d lies on the side the normal of (a, b, c) points to.
int Orient3D(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d);

// Sign of (b - a) x (c - a) in the plane: positive when (a, b, c) turn counterclockwise.
int Orient2D(double ax, double ay, double bx, double by, double cx, double cy);

}  // namespace mesh_verifier::detail
