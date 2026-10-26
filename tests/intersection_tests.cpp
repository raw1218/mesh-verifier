#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include "mesh_verifier/intersection_verifier.h"

using namespace mesh_verifier;
using Catch::Approx;

namespace {

using Tri = std::array<Vec3, 3>;

// A mesh of separate triangles (no shared vertices).
Mesh Soup(const std::vector<Tri>& tris, const std::string& name = "soup") {
    Mesh m;
    m.name = name;
    for (const Tri& t : tris) {
        const auto base = static_cast<uint32_t>(m.vertices.size());
        m.vertices.insert(m.vertices.end(), t.begin(), t.end());
        m.triangles.push_back({base, base + 1, base + 2});
    }
    return m;
}

GeometryData One(const std::vector<Tri>& tris) {
    GeometryData g;
    g.meshes.push_back(Soup(tris));
    return g;
}

VerificationReport Check(const Tri& p, const Tri& q) { return VerifyIntersections(One({p, q})); }

Tri Map(const Tri& t, double scale, Vec3 offset = {}) {
    Tri out;
    for (int k = 0; k < 3; ++k) out[k] = {t[k].x * scale + offset.x, t[k].y * scale + offset.y, t[k].z * scale + offset.z};
    return out;
}

const Tri kFloor{Vec3{0, 0, 0}, Vec3{2, 0, 0}, Vec3{0, 2, 0}};
// a vertical triangle through the floor's interior: it crosses along (0.5, 0.2, 0) - (0.5, 1, 0)
const Tri kPost{Vec3{0.5, 0.2, -1}, Vec3{0.5, 0.2, 1}, Vec3{0.5, 1, 0}};

}  // namespace

TEST_CASE("1. two triangles crossing in 3D") {
    const auto r = Check(kFloor, kPost);
    REQUIRE(r.intersections.size() == 1);
    const Intersection& h = r.intersections[0];
    CHECK(h.type == IntersectionType::Crossing);
    CHECK(h.a == TriangleRef{0, 0});
    CHECK(h.b == TriangleRef{0, 1});
    CHECK(h.weight == Approx(0.8));
    const double ys[2] = {h.segmentStart.y, h.segmentEnd.y};
    CHECK(std::min(ys[0], ys[1]) == Approx(0.2));
    CHECK(std::max(ys[0], ys[1]) == Approx(1.0));
}

TEST_CASE("2. two triangles far apart") {
    CHECK(Check(kFloor, Map(kPost, 1, {0, 0, 5})).intersections.empty());
    CHECK(Check(kFloor, Map(kPost, 1, {10, 0, 0})).intersections.empty());
}

TEST_CASE("3. two triangles sharing an edge") {
    const Tri a{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    CHECK(Check(a, {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{1, 1, 0.5}}).intersections.empty());  // folded
    CHECK(Check(a, {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{1, 1, 0}}).intersections.empty());    // flat
    // the same in one mesh with the edge's vertices really shared
    GeometryData g;
    Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    m.triangles = {{0, 1, 2}, {1, 3, 2}};
    g.meshes.push_back(m);
    CHECK(VerifyIntersections(g).intersections.empty());
}

TEST_CASE("4. two triangles sharing a vertex") {
    const Tri a{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    CHECK(Check(a, {Vec3{0, 0, 0}, Vec3{-1, 0, 1}, Vec3{0, -1, 1}}).intersections.empty());
    CHECK(Check(a, {Vec3{0, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, -1, 0}}).intersections.empty());  // coplanar, opposite
}

TEST_CASE("touching is not intersecting") {
    // a vertex resting on the face, an edge resting on the face, an edge along the other's edge
    CHECK(Check(kFloor, {Vec3{0.5, 0.5, 0}, Vec3{0.5, 0.5, 1}, Vec3{1, 0.5, 1}}).intersections.empty());
    CHECK(Check(kFloor, {Vec3{0.2, 0.2, 0}, Vec3{0.8, 0.2, 0}, Vec3{0.5, 0.5, 1}}).intersections.empty());
    CHECK(Check(kFloor, {Vec3{0.5, 0, 0}, Vec3{1.5, 0, 0}, Vec3{1, -1, 1}}).intersections.empty());
    // a triangle whose crossing line only reaches the other's edge
    CHECK(Check(kFloor, {Vec3{2.5, 0.5, -1}, Vec3{2.5, 0.5, 1}, Vec3{1.5, 0.5, 0}}).intersections.empty());
}

TEST_CASE("5. two coplanar triangles overlapping in area") {
    const Tri a{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    const auto r = Check(a, {Vec3{0.2, 0.2, 0}, Vec3{1.2, 0.2, 0}, Vec3{0.2, 1.2, 0}});
    REQUIRE(r.intersections.size() == 1);
    CHECK(r.intersections[0].type == IntersectionType::CoplanarOverlap);
    CHECK(r.intersections[0].weight == Approx(0.18));  // the overlap: legs of 0.6
}

TEST_CASE("6. two coplanar triangles that don't overlap") {
    const Tri a{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    CHECK(Check(a, {Vec3{5, 0, 0}, Vec3{6, 0, 0}, Vec3{5, 1, 0}}).intersections.empty());
    CHECK(Check(a, {Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{1, 1, 0}}).intersections.empty());          // touching at a vertex
    CHECK(Check(a, {Vec3{0.5, 0.5, 0}, Vec3{1.5, 0.5, 0}, Vec3{0.5, 1.5, 0}}).intersections.empty());  // touching at a corner
}

TEST_CASE("7. intersecting triangles in separate meshes") {
    GeometryData g;
    g.meshes.push_back(Soup({kFloor}, "floor"));
    g.meshes.push_back(Soup({kPost}, "post"));
    const auto r = VerifyIntersections(g);
    REQUIRE(r.intersections.size() == 1);
    CHECK(r.intersections[0].a == TriangleRef{0, 0});
    CHECK(r.intersections[0].b == TriangleRef{1, 0});
}

TEST_CASE("8. self-intersection within one mesh") {
    // a strip whose last triangle folds back through its first
    GeometryData g;
    Mesh m;
    m.vertices = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {2, 2, 0}, {0.5, 0.2, -1}, {0.5, 0.2, 1}, {0.5, 1, 0}};
    m.triangles = {{0, 1, 2}, {1, 3, 2}, {4, 5, 6}};
    g.meshes.push_back(m);
    const auto r = VerifyIntersections(g);
    REQUIRE(r.intersections.size() == 1);
    CHECK(r.intersections[0].a == TriangleRef{0, 0});
    CHECK(r.intersections[0].b == TriangleRef{0, 2});
}

TEST_CASE("9. adjacent triangles that overlap beyond their shared boundary") {
    // sharing an edge and folded flat onto each other
    GeometryData g;
    Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0.2, 0.2, 0}};
    m.triangles = {{0, 1, 2}, {1, 2, 3}};
    g.meshes.push_back(m);
    auto r = VerifyIntersections(g);
    REQUIRE(r.intersections.size() == 1);
    CHECK(r.intersections[0].type == IntersectionType::CoplanarOverlap);
    // sharing a vertex and passing through each other from it
    const Tri a{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    r = Check(a, {Vec3{0, 0, 0}, Vec3{0.6, 0.2, 1}, Vec3{0.2, 0.6, -1}});
    REQUIRE(r.intersections.size() == 1);
    CHECK(r.intersections[0].type == IntersectionType::Crossing);
    CHECK(r.intersections[0].weight == Approx(0.4 * std::sqrt(2.0)));
}

TEST_CASE("10. several intersecting pairs, each reported once, in order") {
    const Tri second = Map(kPost, 1, {0.6, 0, 0});
    const auto r = VerifyIntersections(One({kPost, kFloor, second, Map(kFloor, 1, {0, 0, 9})}));
    REQUIRE(r.intersections.size() == 2);
    CHECK(r.intersections[0].a == TriangleRef{0, 0});
    CHECK(r.intersections[0].b == TriangleRef{0, 1});
    CHECK(r.intersections[1].a == TriangleRef{0, 1});
    CHECK(r.intersections[1].b == TriangleRef{0, 2});
}

TEST_CASE("11. the same findings at any scale and position") {
    for (const double k : {1e-9, 1e-6, 1.0, 1e6, 1e9}) {
        const Vec3 off{3e3 * k, -7e3 * k, 1e4 * k};
        const auto r = Check(Map(kFloor, k, off), Map(kPost, k, off));
        REQUIRE(r.intersections.size() == 1);
        CHECK(r.intersections[0].weight == Approx(0.8 * k));
        CHECK(Check(Map(kFloor, k, off), Map(Map(kPost, 1, {0, 0, 5}), k, off)).intersections.empty());
    }
}

TEST_CASE("12. nearly coplanar triangles") {
    const Tri big{Vec3{-1, -1, 0}, Vec3{3, -1, 0}, Vec3{-1, 3, 0}};
    for (const double e : {1e-6, 1e-12, 1e-100}) {
        // tilted through the big triangle by e: it crosses
        const Tri tilted{Vec3{0.1, 0.1, -e}, Vec3{0.5, 0.1, e}, Vec3{0.1, 0.5, e}};
        const auto r = Check(big, tilted);
        REQUIRE(r.intersections.size() == 1);
        CHECK(r.intersections[0].type == IntersectionType::Crossing);
        // lifted e above it: it doesn't
        CHECK(Check(big, {Vec3{0.1, 0.1, e}, Vec3{0.5, 0.1, 2 * e}, Vec3{0.1, 0.5, 2 * e}}).intersections.empty());
    }
}

TEST_CASE("13. the order of the input doesn't change the findings") {
    // a few crossings and near misses
    std::vector<Tri> tris = {kFloor, kPost, Map(kPost, 1, {0.6, 0, 0}), Map(kPost, 1, {3, 0, 0}),
                             {Vec3{0, 0, 0}, Vec3{0.6, 0.2, 1}, Vec3{0.2, 0.6, -1}}, Map(kFloor, 0.5, {0.1, 0.1, 0})};
    const auto findings = [](const std::vector<Tri>& ts) {
        // the pairs, as pairs of triangles' vertex sets, so they can be compared whatever the order
        std::set<std::pair<std::set<std::array<double, 3>>, std::set<std::array<double, 3>>>> out;
        const auto key = [](const Tri& t) { std::set<std::array<double, 3>> s; for (const Vec3& v : t) s.insert({v.x, v.y, v.z}); return s; };
        for (const Intersection& h : VerifyIntersections(One(ts)).intersections) {
            auto p = key(ts[h.a.triangleIndex]), q = key(ts[h.b.triangleIndex]);
            if (q < p) std::swap(p, q);
            out.insert({p, q});
        }
        return out;
    };
    const auto expected = findings(tris);
    CHECK(expected.size() >= 5);
    std::mt19937 rng(7);
    for (int round = 0; round < 20; ++round) {
        std::shuffle(tris.begin(), tris.end(), rng);
        for (Tri& t : tris) std::rotate(t.begin(), t.begin() + rng() % 3, t.end());  // the vertex order too
        if (rng() % 2) std::swap(tris[0][1], tris[0][2]);                            // and a flipped triangle
        CHECK(findings(tris) == expected);
    }
}

TEST_CASE("the input is not changed") {
    GeometryData g = One({kFloor, kPost});
    const GeometryData before = g;
    VerifyIntersections(g);
    REQUIRE(g.meshes.size() == before.meshes.size());
    CHECK(g.meshes[0].vertices == before.meshes[0].vertices);
    CHECK(g.meshes[0].triangles == before.meshes[0].triangles);
}

TEST_CASE("invalid input is a validation error") {
    GeometryData g = One({kFloor});
    g.meshes[0].triangles.push_back({0, 1, 9});
    CHECK_THROWS_AS(VerifyIntersections(g), ValidationError);
    GeometryData h = One({kFloor, kPost});
    h.meshes[0].vertices[4].z = std::nan("");
    CHECK_THROWS_AS(VerifyIntersections(h), ValidationError);
    h.meshes[0].vertices[4].z = INFINITY;
    CHECK_THROWS_AS(VerifyIntersections(h), ValidationError);
}

TEST_CASE("empty geometry has no intersections") {
    CHECK(VerifyIntersections(GeometryData{}).intersections.empty());
    GeometryData g;
    g.meshes.emplace_back();
    CHECK(VerifyIntersections(g).intersections.empty());
}

TEST_CASE("many triangles: the acceleration structure finds every pair a plain all-pairs check finds") {
    // a grid of small random triangles, checked through the verifier and pair by pair
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> u(0, 1), jitter(-0.6, 0.6);
    std::vector<Tri> tris;
    for (int i = 0; i < 400; ++i) {
        const Vec3 c{u(rng) * 6, u(rng) * 6, u(rng) * 6};
        tris.push_back({Vec3{c.x + jitter(rng), c.y + jitter(rng), c.z + jitter(rng)}, Vec3{c.x + jitter(rng), c.y + jitter(rng), c.z + jitter(rng)},
                        Vec3{c.x + jitter(rng), c.y + jitter(rng), c.z + jitter(rng)}});
    }
    const auto all = VerifyIntersections(One(tris)).intersections;
    std::vector<std::pair<uint32_t, uint32_t>> brute;
    for (uint32_t i = 0; i < tris.size(); ++i)
        for (uint32_t j = i + 1; j < tris.size(); ++j)
            if (!Check(tris[i], tris[j]).intersections.empty()) brute.push_back({i, j});
    REQUIRE(all.size() == brute.size());
    for (size_t k = 0; k < all.size(); ++k) {
        CHECK(all[k].a.triangleIndex == brute[k].first);
        CHECK(all[k].b.triangleIndex == brute[k].second);
    }
    CHECK(!brute.empty());
}

// An independent check of the exact test's sign conventions: random triangle pairs in general position, compared
// with a plain floating-point construction of both crossing segments. Pairs too close to call in floating point
// are skipped (that's what the exact test is for).
TEST_CASE("random pairs agree with a floating-point construction") {
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(-1, 1);
    const auto sub = [](Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; };
    const auto dot = [](Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto cross = [](Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    // the interval along `dir` where triangle t meets the plane (n, o); false if it doesn't
    const auto interval = [&](const Tri& t, Vec3 n, Vec3 o, Vec3 dir, double& lo, double& hi) {
        double d[3];
        for (int k = 0; k < 3; ++k) d[k] = dot(n, sub(t[k], o));
        std::vector<double> ts;
        for (int k = 0; k < 3; ++k) {
            const int m = (k + 1) % 3;
            if ((d[k] > 0) != (d[m] > 0)) {
                const double s = d[k] / (d[k] - d[m]);
                const Vec3 p{t[k].x + (t[m].x - t[k].x) * s, t[k].y + (t[m].y - t[k].y) * s, t[k].z + (t[m].z - t[k].z) * s};
                ts.push_back(dot(p, dir));
            }
        }
        if (ts.size() != 2) return false;
        lo = std::min(ts[0], ts[1]);
        hi = std::max(ts[0], ts[1]);
        return true;
    };
    int hits = 0, misses = 0;
    for (int round = 0; round < 20000; ++round) {
        Tri a, b;
        for (Vec3& v : a) v = {u(rng), u(rng), u(rng)};
        for (Vec3& v : b) v = {u(rng), u(rng), u(rng)};
        const Vec3 na = cross(sub(a[1], a[0]), sub(a[2], a[0])), nb = cross(sub(b[1], b[0]), sub(b[2], b[0]));
        const Vec3 dir = cross(na, nb);
        if (dot(dir, dir) < 1e-6) continue;
        bool clear = true;  // every vertex well away from the other plane
        for (const Vec3& v : a) clear = clear && std::fabs(dot(nb, sub(v, b[0]))) > 1e-6;
        for (const Vec3& v : b) clear = clear && std::fabs(dot(na, sub(v, a[0]))) > 1e-6;
        if (!clear) continue;
        double alo, ahi, blo, bhi;
        const bool cuts = interval(a, nb, b[0], dir, alo, ahi) && interval(b, na, a[0], dir, blo, bhi);
        const double overlap = cuts ? std::min(ahi, bhi) - std::max(alo, blo) : -1;
        if (std::fabs(overlap) < 1e-6) continue;
        const bool expected = overlap > 0;
        const auto r = Check(a, b);
        CHECK(r.intersections.size() == (expected ? 1u : 0u));
        (expected ? hits : misses)++;
    }
    CHECK(hits > 500);
    CHECK(misses > 500);
}
