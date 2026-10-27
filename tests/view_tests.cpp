#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "mesh_verifier/obj_importer.h"
#include "mesh_verifier/schema.h"
#include "mesh_verifier/view.h"

using namespace mesh_verifier;

namespace {

// Two triangles crossing each other along a segment on x = 0.5 (one in the plane z = 0, one in the plane x = 0.5;
// no vertex of either lies on the other's plane).
Scene Crossing() {
    return ParseOBJScene(
        "v 0 -1 0\nv 1 -1 0\nv 0.4 1 0\n"
        "v 0.5 0 -1\nv 0.5 0 1\nv 0.5 2 0.3\n"
        "f 1 2 3\nf 4 5 6\n",
        "crossing");
}

}  // namespace

TEST_CASE("the view highlights the intersecting triangles in world space") {
    Scene scene = Crossing();
    scene.Apply(TransformEdit{0, Space::Local, Vec3{10, 0, 0}, std::nullopt, std::nullopt});  // the whole component moves
    const View view = BuildView(scene, VerifyScene(scene), 7);
    CHECK(view.revision == 7);
    CHECK(view.up == UpAxis::Y);
    REQUIRE(view.parts.size() == 1);
    REQUIRE(view.overlays.size() == 2);

    REQUIRE(view.nodePairs.size() == 1);  // one component crossing itself
    CHECK(view.nodePairs[0].a == 0);
    CHECK(view.nodePairs[0].b == 0);
    CHECK(view.nodePairs[0].aName == "crossing");
    CHECK(view.nodePairs[0].pairs == 1);

    const Overlay& red = view.overlays[0];
    CHECK(red.id == "intersections");
    CHECK(red.kind == OverlayKind::Triangles);
    CHECK(red.role == OverlayRole::Error);
    REQUIRE(red.items.size() == 2);
    CHECK(red.items[0] == TriangleRef{0, 0});
    CHECK(red.items[1] == TriangleRef{0, 1});
    REQUIRE(red.positions.size() == 6);
    CHECK(red.positions[0] == Vec3{10, -1, 0});  // moved with the component

    const Overlay& lines = view.overlays[1];
    CHECK(lines.kind == OverlayKind::Lines);
    REQUIRE(lines.positions.size() == 2);
    CHECK(std::abs(lines.positions[0].x - 10.5) < 1e-9);
    CHECK(std::abs(lines.positions[1].x - 10.5) < 1e-9);

    CHECK_NOTHROW(Validate(ViewJson(view), Schema::View));
}

TEST_CASE("the view lists each pair of intersecting components once, with its count") {
    // three components: a and b cross each other, c crosses b; c and a are apart
    const Scene scene = ParseOBJScene(
        "o a\nv 0 -1 0\nv 1 -1 0\nv 0.4 1 0\nf 1 2 3\n"
        "o b\nv 0.5 0 -1\nv 0.5 0 1\nv 0.5 2 0.3\nf 4 5 6\n"
        "o c\nv 0.2 0.5 0.5\nv 0.8 0.5 0.5\nv 0.45 0.5 1.5\nf 7 8 9\n",
        "three");
    const View view = BuildView(scene, VerifyScene(scene));
    REQUIRE(view.nodePairs.size() == 2);
    CHECK(view.nodePairs[0].aName == "a");
    CHECK(view.nodePairs[0].bName == "b");
    CHECK(view.nodePairs[0].pairs == 1);
    CHECK(view.nodePairs[1].aName == "b");
    CHECK(view.nodePairs[1].bName == "c");
    CHECK_NOTHROW(Validate(ViewJson(view), Schema::View));
}

TEST_CASE("the view of a clean scene has empty layers, and carries the up axis") {
    Scene apart = ParseOBJScene("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "one");
    apart.SetUp(UpAxis::Z);
    const View view = BuildView(apart, VerifyScene(apart));
    CHECK(view.up == UpAxis::Z);
    CHECK(view.nodePairs.empty());
    for (const Overlay& o : view.overlays) CHECK(o.positions.empty());
    const auto json = ViewJson(view);
    CHECK(json["up"] == "z");
    CHECK_NOTHROW(Validate(json, Schema::View));
}
