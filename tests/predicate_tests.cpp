#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "../src/verification/predicates.h"

using namespace mesh_verifier;
using detail::Orient2D;
using detail::Orient3D;

TEST_CASE("orient3d: the side of the plane") {
    const Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};
    CHECK(Orient3D(a, b, c, {0.2, 0.2, 1}) == 1);
    CHECK(Orient3D(a, b, c, {0.2, 0.2, -1}) == -1);
    CHECK(Orient3D(a, b, c, {7, -3, 0}) == 0);
    CHECK(Orient3D(a, c, b, {0.2, 0.2, 1}) == -1);  // the other orientation
}

TEST_CASE("orient3d is exact where floating point is not") {
    // points exactly on one plane, with coordinates whose products don't fit in a double
    const double s = 1.0 + std::ldexp(1.0, -30);
    const Vec3 a{s, s * 3, s * 5}, b{s * 2, s * 7, s * 11}, c{s * 13, s * 17, s * 19};
    const Vec3 d{a.x + b.x - c.x, a.y + b.y - c.y, a.z + b.z - c.z};  // exact: these sums of multiples of s are representable
    CHECK(Orient3D(a, b, c, d) == 0);
    // one unit in the last place off the plane
    const Vec3 up{d.x, d.y, std::nextafter(d.z, INFINITY)}, down{d.x, d.y, std::nextafter(d.z, -INFINITY)};
    CHECK(Orient3D(a, b, c, up) == -Orient3D(a, b, c, down));
    CHECK(Orient3D(a, b, c, up) != 0);
}

TEST_CASE("orient3d is the same at any scale and offset") {
    const Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0}, d{0.25, 0.25, 0.5};
    for (const double k : {1e-12, 1e-6, 1.0, 1e6, 1e12}) {
        const auto t = [k](const Vec3& p) { return Vec3{p.x * k + 1e3 * k, p.y * k - 7 * k, p.z * k + 3 * k}; };
        CHECK(Orient3D(t(a), t(b), t(c), t(d)) == 1);
    }
}

TEST_CASE("orient2d: the turn, exactly") {
    CHECK(Orient2D(0, 0, 1, 0, 0, 1) == 1);
    CHECK(Orient2D(0, 0, 0, 1, 1, 0) == -1);
    CHECK(Orient2D(0, 0, 1, 1, 3, 3) == 0);
    // nearly collinear: the classic case where floating point gives the wrong sign
    const double x = 0.5, e = std::numeric_limits<double>::epsilon();
    CHECK(Orient2D(x, x, 12, 12, 24, 24) == 0);
    CHECK(Orient2D(x + e, x, 12, 12, 24, 24) == -Orient2D(x - e / 2, x, 12, 12, 24, 24));
}
