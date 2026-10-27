#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include <nlohmann/json.hpp>

#include "mesh_verifier/gltf_importer.h"
#include "mesh_verifier/gui_server.h"
#include "mesh_verifier/schema.h"

using namespace mesh_verifier;
using nlohmann::json;

namespace {

const std::filesystem::path kFixtures = FIXTURE_DIR;

// The response's body, checked against the schema it promises.
json Body(const HttpResponse& r, Schema schema) {
    const json body = json::parse(r.body);
    CHECK_NOTHROW(Validate(body, schema));
    return body;
}
size_t Intersections(const HttpResponse& r) { return Body(r, Schema::State)["intersections"].get<size_t>(); }
bool IsError(const HttpResponse& r) { return r.status >= 400 && Body(r, Schema::Error).contains("error"); }

}  // namespace

TEST_CASE("the GUI serves its page and the scene") {
    GuiApp app(LoadGLB(kFixtures / "hierarchy.glb"));
    const HttpResponse page = app.Handle("GET", "/", "");
    CHECK(page.status == 200);
    CHECK(page.body.find("TransformControls") != std::string::npos);
    CHECK(page.body.find("cdn.jsdelivr.net/npm/three@") != std::string::npos);  // (no local three.js given)

    const json scene = Body(app.Handle("GET", "/api/scene", ""), Schema::Scene);
    REQUIRE(scene["components"].size() == 5);
    CHECK(scene["components"][3]["parent"] == 2);
    CHECK(scene["meshes"].size() == 1);
    CHECK(scene["state"]["intersections"] == 0);
    Body(app.Handle("GET", "/api/state", ""), Schema::State);
    Body(app.Handle("GET", "/api/view", ""), Schema::View);
    Body(app.Handle("GET", "/api/intersections", ""), Schema::Intersections);
    CHECK(app.Handle("GET", "/api/nothing", "").status == 404);
}

TEST_CASE("edits through the API are verified again, and can be undone and reset") {
    GuiApp app(LoadGLB(kFixtures / "hierarchy.glb"));
    HttpResponse r = app.Handle("POST", "/api/transform", R"({"node":2,"space":"world","translation":[0.5,0,0]})");
    REQUIRE(r.status == 200);
    CHECK(Intersections(r) > 0);
    const json hand = Body(r, Schema::State)["nodes"][3];
    CHECK(hand["world"]["translation"][0] == 0.5);  // the child came along
    CHECK(Body(app.Handle("GET", "/api/intersections", ""), Schema::Intersections)["intersections"].size() == Intersections(r));

    // invalid requests change nothing, and say why
    r = app.Handle("POST", "/api/transform", R"({"transforms":[{"node":2,"translation":[9,9,9]},{"node":42}]})");
    CHECK(IsError(r));
    CHECK(app.CurrentScene().LocalTransform(2).trs.translation == Vec3{0.5, 0, 0});
    r = app.Handle("POST", "/api/transform", R"({"node":2,"space":"sideways"})");
    CHECK(IsError(r));
    CHECK(r.body.find("/space") != std::string::npos);
    CHECK(IsError(app.Handle("POST", "/api/transform", R"({"node":2,"translation":[1,2]})")));
    CHECK(IsError(app.Handle("POST", "/api/transform", R"({"node":2,"colour":"red"})")));
    CHECK(IsError(app.Handle("POST", "/api/transform", R"({"node":"2"})")));
    CHECK(IsError(app.Handle("POST", "/api/transform", "{oops")));
    CHECK(IsError(app.Handle("POST", "/api/reset", R"({"node":99})")));
    CHECK(IsError(app.Handle("POST", "/api/reset", R"({"everything":true})")));

    r = app.Handle("POST", "/api/undo", "");
    CHECK(Intersections(r) == 0);
    r = app.Handle("POST", "/api/redo", "");
    CHECK(Intersections(r) > 0);
    r = app.Handle("POST", "/api/reset", R"({"node":2})");
    CHECK(Intersections(r) == 0);
    CHECK_FALSE(app.CurrentScene().IsModified(2));

    // a drag: many edits in one group are one undo step
    app.Handle("POST", "/api/transform?group=g1", R"({"node":1,"translation":[1,0,0]})");
    app.Handle("POST", "/api/transform?group=g1", R"({"node":1,"translation":[2,0,0]})");
    app.Handle("POST", "/api/undo", "");
    CHECK(app.CurrentScene().LocalTransform(1).trs.translation == Vec3{0, 0, 0});

    app.Handle("POST", "/api/transform", R"({"node":1,"scale":[3,3,3]})");
    app.Handle("POST", "/api/reset", "{}");
    for (size_t i = 0; i < app.CurrentScene().NodeCount(); ++i) CHECK_FALSE(app.CurrentScene().IsModified(i));
}

TEST_CASE("every state carries the view: the engine's highlight layers") {
    GuiApp app(LoadGLB(kFixtures / "hierarchy.glb"));
    const json before = Body(app.Handle("GET", "/api/view", ""), Schema::View);
    CHECK(before["up"] == "y");
    CHECK(before["overlays"][0]["items"].empty());

    const json state = Body(app.Handle("POST", "/api/transform", R"({"node":2,"space":"world","translation":[0.5,0,0]})"), Schema::State);
    const json& view = state["view"];
    CHECK(view["revision"] == state["revision"]);
    const json& red = view["overlays"][0];
    CHECK(red["id"] == "intersections");
    CHECK(red["kind"] == "triangles");
    CHECK(red["role"] == "error");
    REQUIRE_FALSE(red["items"].empty());
    CHECK(red["positions"].size() == red["items"].size() * 9);  // 3 points of 3 numbers per triangle
    const json& lines = view["overlays"][1];
    CHECK(lines["kind"] == "lines");
    CHECK(lines["positions"].size() % 6 == 0);
    CHECK_FALSE(lines["positions"].empty());
}

TEST_CASE("models can be loaded through the API") {
    GuiApp app(LoadGLB(kFixtures / "hierarchy.glb"));
    HttpResponse r = app.Handle("POST", "/api/load?format=obj&name=tri.obj&up=z", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    REQUIRE(r.status == 200);
    const json scene = Body(r, Schema::Scene);
    CHECK(scene["name"] == "tri");
    CHECK(scene["state"]["view"]["up"] == "z");
    CHECK(app.CurrentScene().NodeCount() == 1);
    CHECK(IsError(app.Handle("POST", "/api/load?format=fbx", "x")));
    CHECK(IsError(app.Handle("POST", "/api/load?format=glb", "not a glb")));
    CHECK(IsError(app.Handle("POST", "/api/load?format=obj&up=x", "v 0 0 0\n")));
    CHECK(app.CurrentScene().NodeCount() == 1);  // a failed load keeps the scene
}
