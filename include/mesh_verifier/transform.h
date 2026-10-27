// Affine transforms for scene components: vectors, quaternions, 4x4 matrices, TRS and Euler angles.
//
// Conventions:
//  - Points are column vectors: p' = M p. A node's world matrix is parentWorld * local.
//  - A TRS transform is the matrix T * R * S: scale first, then rotate, then translate.
//  - Euler angles are in degrees, applied in X, Y, Z order about the fixed (parent) axes:
//    R = Rz(z) * Ry(y) * Rx(x). (The same rotation as intrinsic Z-Y'-X''; three.js Euler order "ZYX";
//    scipy Rotation.from_euler("xyz", ..., degrees=True).)
#pragma once

#include <array>
#include <optional>

#include "mesh_verifier/geometry.h"

namespace mesh_verifier {

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Length(const Vec3& v);
bool IsFinite(const Vec3& v);

// A rotation as a unit quaternion (x, y, z, w), as in glTF.
struct Quat {
    double x = 0.0, y = 0.0, z = 0.0, w = 1.0;
};

inline bool operator==(const Quat& a, const Quat& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
Quat operator*(const Quat& a, const Quat& b);
Quat Normalized(const Quat& q);

// Euler angles in degrees (X, Y, Z order about fixed axes, see above) to a quaternion and back. EulerFromQuat gives
// x, z in (-180, 180] and y in [-90, 90]; at y = +-90 (gimbal lock) it puts all of the remaining turn in x.
Quat QuatFromEuler(const Vec3& degrees);
Vec3 EulerFromQuat(const Quat& q);

// A 4x4 matrix, m[row][column], acting on column vectors. The bottom row is (0, 0, 0, 1) for every affine
// transform built here.
struct Mat4 {
    std::array<std::array<double, 4>, 4> m{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};

    static Mat4 Identity() { return {}; }
    // From 16 numbers in column-major order (as glTF writes them).
    static Mat4 FromColumnMajor(const double* values);
    bool IsIdentity() const;
};

inline bool operator==(const Mat4& a, const Mat4& b) { return a.m == b.m; }
Mat4 operator*(const Mat4& a, const Mat4& b);
Vec3 TransformPoint(const Mat4& m, const Vec3& p);
double Determinant3(const Mat4& m);
// The inverse of an affine matrix; nullopt when its 3x3 part is singular.
std::optional<Mat4> InverseAffine(const Mat4& m);

// Translation, rotation and scale; the matrix T * R * S.
struct TRS {
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1, 1, 1};
};

Mat4 Compose(const TRS& trs);

// Splits an affine matrix into translation, rotation and scale (a negative determinant goes into scale.x). Exact
// only when the matrix is a TRS (its 3x3 columns orthogonal); `exact` says whether it was (within rounding).
TRS Decompose(const Mat4& m, bool* exact = nullptr);

}  // namespace mesh_verifier
