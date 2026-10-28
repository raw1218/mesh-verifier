#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

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

TEST_CASE("verification can be turned off, and on again") {
    GuiApp app(LoadGLB(kFixtures / "hierarchy.glb"));
    HttpResponse r = app.Handle("POST", "/api/verify", R"({"on":false})");
    CHECK(Body(r, Schema::State)["verifying"] == false);
    r = app.Handle("POST", "/api/transform", R"({"node":2,"space":"world","translation":[0.5,0,0]})");
    CHECK(Intersections(r) == 0);  // (not checked while off)
    CHECK(Body(r, Schema::State)["view"]["nodePairs"].empty());
    CHECK(Body(app.Handle("GET", "/api/intersections", ""), Schema::Intersections)["intersections"].empty());

    r = app.Handle("POST", "/api/verify", R"({"on":true})");  // verifies the scene as it is now
    CHECK(Body(r, Schema::State)["verifying"] == true);
    CHECK(Intersections(r) > 0);
    CHECK(IsError(app.Handle("POST", "/api/verify", "{}")));
    CHECK(IsError(app.Handle("POST", "/api/verify", R"({"on":"no"})")));
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
    CHECK(lines["positions"].size() % 6 == 0);  // (these cubes only overlap face to face: no crossing lines here)

    // rotated, the cube crosses the other: crossing lines too
    const json turned = Body(app.Handle("POST", "/api/transform", R"({"node":2,"space":"world","rotationEuler":[0,0,30]})"), Schema::State);
    CHECK_FALSE(turned["view"]["overlays"][1]["positions"].empty());
}

namespace {

// A stand-in rig: one control, "lift", raising every vertex of every mesh by its value.
class Lift : public PoseSource {
public:
    explicit Lift(const Scene& scene) {
        for (const Mesh& m : scene.Meshes()) rest_.push_back(m.vertices);
    }
    std::vector<ControlInfo> Controls() const override { return {{"lift", 0, 2, 0}}; }
    // one joint, "top": weight 1 for each mesh's first vertex, 0 for the rest
    std::vector<std::string> Joints() const override { return {"top"}; }
    std::vector<std::vector<double>> Weights(size_t) const override {
        std::vector<std::vector<double>> w;
        for (const auto& mesh : rest_) {
            w.emplace_back(mesh.size(), 0.0);
            if (!mesh.empty()) w.back()[0] = 1.0;
        }
        return w;
    }
    std::vector<std::vector<Vec3>> Pose(const std::vector<double>& values) const override {
        std::vector<std::vector<Vec3>> out = rest_;
        for (auto& mesh : out)
            for (Vec3& v : mesh) v.y += values[0];
        return out;
    }

private:
    std::vector<std::vector<Vec3>> rest_;
};

}  // namespace

TEST_CASE("with a rig, the controls pose the meshes and the result is verified again") {
    Scene scene = LoadGLB(kFixtures / "hierarchy.glb");
    auto rig = std::make_shared<const Lift>(scene);
    GuiApp app(std::move(scene), {}, rig);
    const json controls = Body(app.Handle("GET", "/api/controls", ""), Schema::Controls);
    REQUIRE(controls["controls"].size() == 1);
    CHECK(controls["controls"][0]["name"] == "lift");
    CHECK(controls["controls"][0]["value"] == 0);

    const double before = app.CurrentScene().Meshes()[0].vertices[0].y;
    const json posed = Body(app.Handle("POST", "/api/controls", R"({"values": {"lift": 1.5}})"), Schema::Posed);
    CHECK(posed["meshes"].size() == app.CurrentScene().Meshes().size());
    CHECK(posed["meshes"][0]["positions"][1] == before + 1.5);
    CHECK(app.CurrentScene().Meshes()[0].vertices[0].y == before + 1.5);
    CHECK(Body(app.Handle("GET", "/api/controls", ""), Schema::Controls)["controls"][0]["value"] == 1.5);

    CHECK(IsError(app.Handle("POST", "/api/controls", R"({"values": {"twist": 1}})")));
    CHECK(IsError(app.Handle("POST", "/api/controls", R"({"values": {"lift": 3}})")));
    CHECK(IsError(app.Handle("POST", "/api/controls", R"({"values": {"lift": "high"}})")));
    CHECK(app.CurrentScene().Meshes()[0].vertices[0].y == before + 1.5);  // (failed requests change nothing)
}

TEST_CASE("a control's influence: a heat map in the view, kept up to date") {
    Scene scene = LoadGLB(kFixtures / "hierarchy.glb");
    auto rig = std::make_shared<const Lift>(scene);
    GuiApp app(std::move(scene), {}, rig);
    const auto overlay = [](const json& state, const std::string& id) -> json {
        for (const json& o : state["view"]["overlays"])
            if (o["id"] == id) return o;
        return nullptr;
    };
    json state = Body(app.Handle("POST", "/api/influence", R"({"control": "lift"})"), Schema::State);
    const json heat = overlay(state, "influence");
    REQUIRE(heat.is_object());
    CHECK(heat["role"] == "influence");
    CHECK_FALSE(heat["items"].empty());
    CHECK(heat["colors"].size() == heat["positions"].size() / 3 * 4);
    CHECK(heat["colors"][0] == 1.0);  // lift moves everything equally: all of it the most, red
    CHECK(heat["label"].get<std::string>().find("to 2") != std::string::npos);  // (from 0 to its far end, 2)
    CHECK(Body(app.Handle("GET", "/api/view", ""), Schema::View)["overlays"].size() == 3);

    // posing keeps it shown, now from the new pose (at the max: towards the min)
    state = Body(app.Handle("POST", "/api/controls", R"({"values": {"lift": 2}})"), Schema::Posed)["state"];
    CHECK(overlay(state, "influence")["label"].get<std::string>().find("to 0") != std::string::npos);
    CHECK(overlay(state, "influence_moves").is_null());  // (no arrows)

    CHECK(IsError(app.Handle("POST", "/api/influence", R"({"control": "twist"})")));
    CHECK(IsError(app.Handle("POST", "/api/influence", R"({"control": "lift", "value": 9})")));
    CHECK(IsError(app.Handle("POST", "/api/influence", R"({"value": 1})")));
    state = Body(app.Handle("POST", "/api/influence", "{}"), Schema::State);
    CHECK(overlay(state, "influence").is_null());
}

TEST_CASE("a joint's skin weights painted over the meshes") {
    Scene scene = LoadGLB(kFixtures / "hierarchy.glb");
    auto rig = std::make_shared<const Lift>(scene);
    GuiApp app(std::move(scene), {}, rig);
    CHECK(Body(app.Handle("GET", "/api/controls", ""), Schema::Controls)["joints"] == json::array({"top"}));
    const json state = Body(app.Handle("POST", "/api/weights", R"({"joint": "top"})"), Schema::State);
    json paint;
    for (const json& o : state["view"]["overlays"])
        if (o["id"] == "weights") paint = o;
    REQUIRE(paint.is_object());
    size_t triangles = 0;
    for (const SceneNode& n : app.CurrentScene().Nodes())
        if (n.mesh >= 0 && n.inScene) triangles += app.CurrentScene().Meshes()[static_cast<size_t>(n.mesh)].triangles.size();
    CHECK(paint["items"].size() == triangles);  // every triangle, weight 0 included
    bool red = false, blue = false;
    for (size_t k = 0; k + 3 < paint["colors"].size(); k += 4) {
        red = red || (paint["colors"][k] == 1.0 && paint["colors"][k + 2] == 0.0);
        blue = blue || (paint["colors"][k] == 0.0 && paint["colors"][k + 2] == 1.0);
    }
    CHECK(red);
    CHECK(blue);
    CHECK(IsError(app.Handle("POST", "/api/weights", R"({"joint": "elbow"})")));
    for (const json& o : Body(app.Handle("POST", "/api/weights", "{}"), Schema::State)["view"]["overlays"]) CHECK(o["id"] != "weights");
}

TEST_CASE("without a rig there are no controls") {
    GuiApp app(LoadGLB(kFixtures / "hierarchy.glb"));
    CHECK(Body(app.Handle("GET", "/api/controls", ""), Schema::Controls)["controls"].empty());
    CHECK(IsError(app.Handle("POST", "/api/controls", R"({"values": {}})")));
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
