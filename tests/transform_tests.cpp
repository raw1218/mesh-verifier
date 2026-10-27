#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "mesh_verifier/transform.h"

using namespace mesh_verifier;
using Catch::Approx;

namespace {

void CheckNear(const Vec3& a, const Vec3& b, double eps = 1e-12) {
    CHECK(a.x == Approx(b.x).margin(eps));
    CHECK(a.y == Approx(b.y).margin(eps));
    CHECK(a.z == Approx(b.z).margin(eps));
}

void CheckNear(const Mat4& a, const Mat4& b, double eps = 1e-12) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) CHECK(a.m[r][c] == Approx(b.m[r][c]).margin(eps));
}

}  // namespace

TEST_CASE("Euler angles rotate about X, then Y, then Z (fixed axes)") {
    // Rx(90) takes +Y to +Z, then Ry(90) takes +Z to +X
    const Mat4 r = Compose({{}, QuatFromEuler({90, 90, 0}), {1, 1, 1}});
    CHECK(TransformPoint(r, {0, 1, 0}) == Vec3{1, 0, 0});
    // Rz(90) alone takes +X to +Y
    CHECK(TransformPoint(Compose({{}, QuatFromEuler({0, 0, 90}), {1, 1, 1}}), {1, 0, 0}) == Vec3{0, 1, 0});
    // Rx(90) then Rz(90): +Y -> +Z -> +Z
    CHECK(TransformPoint(Compose({{}, QuatFromEuler({90, 0, 90}), {1, 1, 1}}), {0, 1, 0}) == Vec3{0, 0, 1});
}

TEST_CASE("quarter turns are exact") {
    for (const double a : {90.0, 180.0, 270.0, -90.0, 360.0, 450.0}) {
        const Mat4 m = Compose({{}, QuatFromEuler({a, 0, 0}), {1, 1, 1}});
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) CHECK((m.m[r][c] == 0 || m.m[r][c] == 1 || m.m[r][c] == -1));
    }
}

TEST_CASE("Euler angles survive a round trip through a quaternion") {
    for (const Vec3& e : {Vec3{10, 20, 30}, Vec3{-170, 45, 100}, Vec3{0, -89, 0}, Vec3{33, 0, -120}, Vec3{0, 0, 45}}) CheckNear(EulerFromQuat(QuatFromEuler(e)), e, 1e-9);
    CHECK(EulerFromQuat(QuatFromEuler({0, 0, 45})) == Vec3{0, 0, 45});
    // gimbal lock: the same rotation, whatever the split
    const Quat q = QuatFromEuler({30, 90, 10});
    CheckNear(Compose({{}, QuatFromEuler(EulerFromQuat(q)), {1, 1, 1}}), Compose({{}, q, {1, 1, 1}}), 1e-9);
}

TEST_CASE("a TRS matrix splits back into its parts") {
    const TRS trs{{1, -2, 3}, QuatFromEuler({15, -40, 75}), {2, 0.5, 3}};
    bool exact = false;
    const TRS back = Decompose(Compose(trs), &exact);
    CHECK(exact);
    CheckNear(back.translation, trs.translation);
    CheckNear(back.scale, trs.scale);
    CheckNear(EulerFromQuat(back.rotation), {15, -40, 75}, 1e-9);
}

TEST_CASE("a mirroring scale is kept") {
    const TRS trs{{}, QuatFromEuler({0, 30, 0}), {-1, 2, 2}};
    const TRS back = Decompose(Compose(trs));
    CheckNear(Compose(back), Compose(trs));
    CHECK(back.scale.x * back.scale.y * back.scale.z < 0);
}

TEST_CASE("a sheared matrix is reported as not a TRS") {
    Mat4 m;
    m.m[0][1] = 0.5;  // x += 0.5 y
    bool exact = true;
    Decompose(m, &exact);
    CHECK_FALSE(exact);
}

TEST_CASE("the inverse of an affine matrix") {
    const Mat4 m = Compose({{4, 5, 6}, QuatFromEuler({20, 30, 40}), {2, 3, 4}});
    const auto inverse = InverseAffine(m);
    REQUIRE(inverse);
    CheckNear(*inverse * m, Mat4::Identity());
    CHECK_FALSE(InverseAffine(Compose({{}, {}, {1, 0, 1}})));
}

TEST_CASE("column-major matrices as glTF writes them") {
    const double v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1};
    CHECK(TransformPoint(Mat4::FromColumnMajor(v), {0, 0, 0}) == Vec3{7, 8, 9});
}
