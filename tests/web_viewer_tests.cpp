#include <catch2/catch_test_macros.hpp>

#include "mesh_verifier/intersection_verifier.h"
#include "mesh_verifier/web_viewer.h"

using namespace mesh_verifier;

TEST_CASE("the web page carries the geometry and exactly the report's triangles") {
    GeometryData g;
    Mesh m;
    m.name = "two <triangles>";
    m.vertices = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0.5, 0.2, -1}, {0.5, 0.2, 1}, {0.5, 1, 0}, {9, 9, 9}, {10, 9, 9}, {9, 10, 9}};
    m.triangles = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
    g.meshes.push_back(m);
    const VerificationReport report = VerifyIntersections(g);
    WebViewer viewer;
    viewer.title = "test <page>";
    const std::string page = viewer.Render(g, report);
    CHECK(page.find("<title>test &lt;page&gt;</title>") != std::string::npos);
    CHECK(page.find("\"name\":\"two \\u003ctriangles>\"") != std::string::npos);
    CHECK(page.find("\"triangles\":[0,1,2,3,4,5,6,7,8]") != std::string::npos);
    CHECK(page.find("\"flagged\":[0,1]") != std::string::npos);  // the far triangle isn't flagged
    CHECK(page.find("\"intersections\":1") != std::string::npos);
}
