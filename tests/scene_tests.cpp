#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "mesh_verifier/intersection_verifier.h"
#include "mesh_verifier/obj_importer.h"
#include "mesh_verifier/scene.h"

using namespace mesh_verifier;
using Catch::Approx;

namespace {

void CheckNear(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    CHECK(a.x == Approx(b.x).margin(eps));
    CHECK(a.y == Approx(b.y).margin(eps));
    CHECK(a.z == Approx(b.z).margin(eps));
}

void CheckNear(const Mat4& a, const Mat4& b, double eps = 1e-9) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) CHECK(a.m[r][c] == Approx(b.m[r][c]).margin(eps));
}

// A unit square in the XY plane centred on the origin (two triangles).
Mesh Square() {
    Mesh m;
    m.name = "square";
    m.vertices = {{-0.5, -0.5, 0}, {0.5, -0.5, 0}, {0.5, 0.5, 0}, {-0.5, 0.5, 0}};
    m.triangles = {{0, 1, 2}, {0, 2, 3}};
    return m;
}

// node 0 "group" (no mesh) -> node 1 "a" (square); node 2 "b" (square, a root, at z = 5)
Scene ThreeNodes() {
    std::vector<SceneNode> nodes(3);
    nodes[0].name = "group";
    nodes[0].children = {1};
    nodes[1].name = "a";
    nodes[1].mesh = 0;
    nodes[1].parent = 0;
    nodes[2].name = "b";
    nodes[2].mesh = 0;
    nodes[2].local.trs.translation = {0, 0, 5};
    return Scene("test", {Square()}, nodes);
}

TransformEdit Edit(size_t node, Space space) {
    TransformEdit e;
    e.node = node;
    e.space = space;
    return e;
}

}  // namespace

TEST_CASE("a child follows its parent") {
    Scene s = ThreeNodes();
    TransformEdit e = Edit(0, Space::Local);
    e.translation = Vec3{1, 2, 3};
    e.rotationEuler = Vec3{0, 0, 90};
    s.Apply(e);
    CheckNear(TransformPoint(s.WorldMatrix(1), {1, 0, 0}), {1, 3, 3});
    CHECK(s.LocalTransform(1).trs.translation == Vec3{});  // the child's own transform is unchanged
}

TEST_CASE("omitted fields keep their values") {
    Scene s = ThreeNodes();
    TransformEdit e = Edit(2, Space::Local);
    e.scale = Vec3{2, 2, 2};
    s.Apply(e);
    CHECK(s.LocalTransform(2).trs.translation == Vec3{0, 0, 5});
    CHECK(s.LocalTransform(2).trs.scale == Vec3{2, 2, 2});
    e = Edit(2, Space::Local);
    e.rotationEuler = Vec3{0, 0, 30};
    s.Apply(e);
    CHECK(s.LocalTransform(2).trs.scale == Vec3{2, 2, 2});
    CHECK(s.LocalTransform(2).EulerDegrees() == Vec3{0, 0, 30});
}

TEST_CASE("a world-space edit places the node in the world, whatever its parent does") {
    Scene s = ThreeNodes();
    TransformEdit p = Edit(0, Space::Local);
    p.translation = Vec3{10, 0, 0};
    p.rotationEuler = Vec3{0, 0, 90};
    p.scale = Vec3{2, 2, 2};
    s.Apply(p);

    TransformEdit e = Edit(1, Space::World);
    e.translation = Vec3{1, 1, 1};
    s.Apply(e);
    CheckNear(s.WorldTRS(1).translation, {1, 1, 1});
    // the local value is in the parent's space: (1,1,1) = (10,0,0) + Rz(90) * 2 * local
    CheckNear(s.LocalTransform(1).trs.translation, {0.5, 4.5, 0.5});

    e = Edit(1, Space::World);
    e.rotationEuler = Vec3{30, 0, 0};
    e.scale = Vec3{1, 1, 1};
    s.Apply(e);
    const TRS world = s.WorldTRS(1);
    CheckNear(world.translation, {1, 1, 1});
    CheckNear(world.scale, {1, 1, 1});
    CheckNear(EulerFromQuat(world.rotation), {30, 0, 0});
    CheckNear(s.LocalTransform(1).trs.scale, {0.5, 0.5, 0.5});
}

TEST_CASE("a local-space edit is relative to the parent") {
    Scene s = ThreeNodes();
    TransformEdit p = Edit(0, Space::Local);
    p.translation = Vec3{10, 0, 0};
    s.Apply(p);
    TransformEdit e = Edit(1, Space::Local);
    e.translation = Vec3{1, 0, 0};
    s.Apply(e);
    CheckNear(s.WorldTRS(1).translation, {11, 0, 0});
}

TEST_CASE("world and local are the same for a root") {
    Scene a = ThreeNodes(), b = ThreeNodes();
    TransformEdit e = Edit(2, Space::World);
    e.translation = Vec3{1, 2, 3};
    e.rotationEuler = Vec3{0, 0, 45};
    e.scale = Vec3{1.5, 0.5, 0.5};
    a.Apply(e);
    e.space = Space::Local;
    b.Apply(e);
    CHECK(a.WorldMatrix(2) == b.WorldMatrix(2));
    CHECK(a.LocalTransform(2).EulerDegrees() == Vec3{0, 0, 45});
}

TEST_CASE("a world edit under a non-uniformly scaled, rotated parent is exact in the world") {
    Scene s = ThreeNodes();
    TransformEdit p = Edit(0, Space::Local);
    p.rotationEuler = Vec3{0, 0, 30};
    p.scale = Vec3{3, 1, 1};
    s.Apply(p);
    TransformEdit e = Edit(1, Space::World);
    e.translation = Vec3{1, 2, 3};
    e.rotationEuler = Vec3{0, 0, 45};
    e.scale = Vec3{1, 1, 1};
    s.Apply(e);
    CheckNear(s.WorldMatrix(1), Compose({{1, 2, 3}, QuatFromEuler({0, 0, 45}), {1, 1, 1}}));
    CHECK(s.LocalTransform(1).matrix.has_value());  // (not expressible as a local TRS: it has shear)
}

TEST_CASE("edits are all or nothing") {
    Scene s = ThreeNodes();
    TransformEdit good = Edit(2, Space::Local), bad = Edit(7, Space::Local);
    good.translation = Vec3{9, 9, 9};
    CHECK_THROWS_AS(s.Apply({good, bad}), SceneError);
    CHECK(s.LocalTransform(2).trs.translation == Vec3{0, 0, 5});

    TransformEdit nan = Edit(2, Space::Local);
    nan.scale = Vec3{1, NAN, 1};
    CHECK_THROWS_AS(s.Apply({good, nan}), SceneError);
    CHECK(s.LocalTransform(2).trs.translation == Vec3{0, 0, 5});

    // a world edit under a parent with zero scale can't be solved
    TransformEdit flat = Edit(0, Space::Local), world = Edit(1, Space::World);
    flat.scale = Vec3{1, 0, 1};
    world.translation = Vec3{1, 1, 1};
    CHECK_THROWS_AS(s.Apply({flat, world}), SceneError);
    CHECK(s.LocalTransform(0).trs.scale == Vec3{1, 1, 1});
}

TEST_CASE("resetting restores the imported transforms") {
    Scene s = ThreeNodes();
    TransformEdit e0 = Edit(0, Space::Local), e1 = Edit(1, Space::Local), e2 = Edit(2, Space::World);
    e0.translation = Vec3{1, 0, 0};
    e1.scale = Vec3{2, 2, 2};
    e2.rotationEuler = Vec3{10, 20, 30};
    s.Apply({e0, e1, e2});
    CHECK(s.IsModified(0));
    s.Reset(0);
    CHECK_FALSE(s.IsModified(0));
    CHECK(s.IsModified(1));
    s.Apply(e0);
    s.Reset(0, true);
    CHECK_FALSE(s.IsModified(0));
    CHECK_FALSE(s.IsModified(1));
    CHECK(s.IsModified(2));
    s.ResetAll();
    CHECK_FALSE(s.IsModified(2));
    CHECK(s.LocalTransform(2).trs.translation == Vec3{0, 0, 5});
}

TEST_CASE("the source geometry is never changed") {
    Scene s = ThreeNodes();
    const std::vector<Mesh> before = s.Meshes();
    TransformEdit e = Edit(1, Space::World);
    e.translation = Vec3{3, 2, 1};
    e.scale = Vec3{4, 4, 4};
    s.Apply(e);
    s.WorldGeometry();
    VerifyScene(s);
    REQUIRE(s.Meshes().size() == before.size());
    CHECK(s.Meshes()[0].vertices == before[0].vertices);
    CHECK(s.Meshes()[0].triangles == before[0].triangles);
}

TEST_CASE("verification follows the transforms, reporting component indices") {
    Scene s = ThreeNodes();
    CHECK(VerifyScene(s).intersections.empty());
    // stand b upright through a's centre: a vertical square crossing the horizontal one
    TransformEdit e = Edit(2, Space::World);
    e.translation = Vec3{0, 0, 0};
    e.rotationEuler = Vec3{90, 0, 0};
    s.Apply(e);
    const VerificationReport r = VerifyScene(s);
    REQUIRE_FALSE(r.intersections.empty());
    for (const Intersection& hit : r.intersections) {
        CHECK(hit.a.meshIndex == 1);  // node 0 has no mesh; the meshes are nodes 1 and 2
        CHECK(hit.b.meshIndex == 2);
    }
    // moving the parent of a carries a out of the way
    TransformEdit away = Edit(0, Space::Local);
    away.translation = Vec3{0, 0, 3};
    s.Apply(away);
    CHECK(VerifyScene(s).intersections.empty());
    s.ResetAll();
    CHECK(VerifyScene(s).intersections.empty());
}

TEST_CASE("an untransformed scene verifies exactly like its meshes") {
    const Scene s = ParseOBJScene("v 0 0 0\nv 2 0 0\nv 0 2 0\nv 0.5 0.2 -1\nv 0.5 0.2 1\nv 0.5 1 0\nf 1 2 3\nf 4 5 6\n", "x");
    const VerificationReport a = VerifyScene(s), b = VerifyIntersections(ParseOBJ("v 0 0 0\nv 2 0 0\nv 0 2 0\nv 0.5 0.2 -1\nv 0.5 0.2 1\nv 0.5 1 0\nf 1 2 3\nf 4 5 6\n"));
    REQUIRE(a.intersections.size() == b.intersections.size());
    CHECK(a.intersections[0].weight == b.intersections[0].weight);
}

TEST_CASE("an OBJ without objects is one component, the same as LoadOBJ") {
    const Scene s = ParseOBJScene("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 5 5 5\nf 1 2 3\n", "file");
    REQUIRE(s.NodeCount() == 1);
    CHECK(s.Node(0).name == "file");
    CHECK(s.Meshes()[0].vertices.size() == 4);
    CHECK(s.WorldMatrix(0).IsIdentity());
}

TEST_CASE("OBJ objects are separate components that can be transformed") {
    const std::string text =
        "v 0 0 0\nv 2 0 0\nv 0 2 0\n"
        "o floor\nf 1 2 3\n"
        "o post\nv 0.5 0.2 4\nv 0.5 0.2 6\nv 0.5 1 5\nf 4 5 6\n";
    Scene s = ParseOBJScene(text, "file");
    REQUIRE(s.NodeCount() == 2);
    CHECK(s.Node(0).name == "floor");
    CHECK(s.Node(1).name == "post");
    CHECK(s.Meshes()[1].vertices.size() == 3);
    CHECK(s.Meshes()[1].triangles[0] == Triangle{0, 1, 2});
    CHECK(VerifyScene(s).intersections.empty());
    TransformEdit e = Edit(1, Space::Local);
    e.translation = Vec3{0, 0, -5};
    s.Apply(e);
    const VerificationReport r = VerifyScene(s);
    REQUIRE(r.intersections.size() == 1);
    CHECK(r.intersections[0].a == TriangleRef{0, 0});
    CHECK(r.intersections[0].b == TriangleRef{1, 0});
}

TEST_CASE("an invalid hierarchy is rejected") {
    std::vector<SceneNode> cycle(2);
    cycle[0].parent = 1;
    cycle[0].children = {1};
    cycle[1].parent = 0;
    cycle[1].children = {0};
    CHECK_THROWS_AS(Scene("x", {}, cycle), SceneError);
    std::vector<SceneNode> badMesh(1);
    badMesh[0].mesh = 3;
    CHECK_THROWS_AS(Scene("x", {}, badMesh), SceneError);
}
