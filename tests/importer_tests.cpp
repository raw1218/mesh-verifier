#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "mesh_verifier/obj_importer.h"

using namespace mesh_verifier;

namespace {
const std::filesystem::path kFixtures = FIXTURE_DIR;
}

TEST_CASE("a single triangle") {
    const GeometryData g = LoadOBJ(kFixtures / "triangle.obj");
    REQUIRE(g.meshes.size() == 1);
    const Mesh& m = g.meshes[0];
    CHECK(m.name == "triangle");
    REQUIRE(m.vertices.size() == 3);
    CHECK(m.vertices[1] == Vec3{1, 0, 0});
    REQUIRE(m.triangles.size() == 1);
    CHECK(m.triangles[0] == Triangle{0, 1, 2});
}

TEST_CASE("triangles sharing vertices keep one copy of each vertex") {
    const GeometryData g = ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nf 1 2 3\nf 2 4 3\n");
    const Mesh& m = g.meshes[0];
    CHECK(m.vertices.size() == 4);
    REQUIRE(m.triangles.size() == 2);
    CHECK(m.triangles[1] == Triangle{1, 3, 2});
}

TEST_CASE("object and group lines are ignored: one file is one mesh") {
    const GeometryData g = ParseOBJ("o first\nv 0 0 0\nv 1 0 0\nv 0 1 0\ng part\nf 1 2 3\no second\nv 0 0 1\nf 1 2 4\n");
    REQUIRE(g.meshes.size() == 1);
    CHECK(g.meshes[0].vertices.size() == 4);
    CHECK(g.meshes[0].triangles.size() == 2);
}

TEST_CASE("negative indices count back from the last vertex read") {
    const GeometryData g = ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\nv 5 5 5\nf -4 -1 -2\n");
    const Mesh& m = g.meshes[0];
    CHECK(m.triangles[0] == Triangle{0, 1, 2});
    CHECK(m.triangles[1] == Triangle{0, 3, 2});
}

TEST_CASE("every face index syntax uses only the position index") {
    const GeometryData g = ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvn 0 0 1\nf 1 2 3\nf 1/1 2/1 3/1\nf 1//1 2//1 3//1\nf 1/1/1 2/1/1 3/1/1\n");
    REQUIRE(g.meshes[0].triangles.size() == 4);
    for (const Triangle& t : g.meshes[0].triangles) CHECK(t == Triangle{0, 1, 2});
}

TEST_CASE("positions are kept exactly as written") {
    const GeometryData g = ParseOBJ("v 0.1 -2.5e-7 123456789.123456789 1.0\nv +1 2 3\nv 4 5 6\nf 1 2 3\n");
    CHECK(g.meshes[0].vertices[0] == Vec3{0.1, -2.5e-7, 123456789.123456789});
    CHECK(g.meshes[0].vertices[1] == Vec3{1, 2, 3});
}

TEST_CASE("comments, blank lines and other statements are skipped") {
    const GeometryData g = ParseOBJ("# a comment\n\nmtllib x.mtl\nusemtl y\ns off\nv 0 0 0 # trailing\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    CHECK(g.meshes[0].triangles.size() == 1);
}

TEST_CASE("zero-area triangles and repeated indices are kept as they are") {
    const GeometryData g = ParseOBJ("v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\nf 1 1 2\n");
    CHECK(g.meshes[0].triangles.size() == 2);
    CHECK(g.meshes[0].triangles[1] == Triangle{0, 0, 1});
}

TEST_CASE("invalid face indices are errors") {
    CHECK_THROWS_AS(ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n"), ObjImportError);
    CHECK_THROWS_AS(ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n"), ObjImportError);
    CHECK_THROWS_AS(ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -4 1 2\n"), ObjImportError);
    CHECK_THROWS_AS(ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 x 2\n"), ObjImportError);
}

TEST_CASE("non-triangular faces are errors") {
    CHECK_THROWS_AS(ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nf 1 2 3 4\n"), ObjImportError);
    CHECK_THROWS_AS(ParseOBJ("v 0 0 0\nv 1 0 0\nf 1 2\n"), ObjImportError);
}

TEST_CASE("malformed vertex coordinates are errors") {
    CHECK_THROWS_AS(ParseOBJ("v 0 0\n"), ObjImportError);
    CHECK_THROWS_AS(ParseOBJ("v 0 zero 0\n"), ObjImportError);
    CHECK_THROWS_AS(ParseOBJ("v 0 1.2.3 0\n"), ObjImportError);
}

TEST_CASE("an error names the line") {
    try {
        ParseOBJ("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3 1\n", "bad");
        FAIL("no error");
    } catch (const ObjImportError& e) {
        CHECK(std::string(e.what()).find("bad:4") != std::string::npos);
    }
}

TEST_CASE("an empty file is one empty mesh") {
    const GeometryData g = LoadOBJ(kFixtures / "empty.obj");
    REQUIRE(g.meshes.size() == 1);
    CHECK(g.meshes[0].vertices.empty());
    CHECK(g.meshes[0].triangles.empty());
}

TEST_CASE("a missing file is an error") {
    CHECK_THROWS_AS(LoadOBJ(kFixtures / "does_not_exist.obj"), ObjImportError);
}
