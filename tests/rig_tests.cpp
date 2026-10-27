#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "mesh_rig/rig.h"
#include "mesh_verifier/gltf_importer.h"
#include "mesh_verifier/intersection_verifier.h"

using namespace mesh_rig;
using namespace mesh_verifier;

namespace {

const std::filesystem::path kDir = std::filesystem::path(EXAMPLES_DIR) / "rig" / "circle";

std::string Text(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

size_t ControlIndex(const RigDefinition& d, const std::string& name) {
    for (size_t i = 0; i < d.controls.size(); ++i)
        if (d.controls[i].name == name) return i;
    FAIL("no control " << name);
    return 0;
}

size_t Index(const std::vector<std::string>& names, const std::string& name) {
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return i;
    FAIL("no " << name);
    return 0;
}

// A rig file's text (by default the circle's) with one more entry at the start of one of its lists.
std::string With(const std::string& list, const std::string& entry, std::string text = Text(kDir / "circle.rig.json")) {
    const size_t at = text.find("\"" + list + "\": [");
    if (at == std::string::npos) return text;
    const size_t open = text.find('[', at);
    return text.substr(0, open + 1) + entry + "," + text.substr(open + 1);
}

std::string Failure(const std::string& rigText, const SkinnedModel& model) {
    try {
        OurRigEvaluator(ParseRigFile(rigText), model);
    } catch (const RigError& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST_CASE("the circle's rig: controls to joints, morphs and curves") {
    const SkinnedModel model = LoadSkinnedGLB(kDir / "circle.glb");
    const OurRigEvaluator rig(LoadRigFile(kDir / "circle.rig.json"), model);
    const RigDefinition& d = rig.Definition();
    REQUIRE(d.controls.size() == 4);
    CHECK(d.joints.size() == model.nodes.size());
    CHECK(d.morphs == model.morphNames);
    CHECK(d.curves == std::vector<std::string>{"wrinkle_brow_L", "wrinkle_brow_R"});

    std::vector<double> c = DefaultControls(d);
    DeformerFrame f;
    rig.Evaluate(c, f);
    const size_t browL = static_cast<size_t>(model.FindNode("brow_L"));
    CHECK(f.jointLocal[browL].translation == model.nodes[browL].rest.translation);
    CHECK(f.morphWeights == std::vector<double>{0, 0});

    c[ControlIndex(d, "brow_raise_L")] = 0.5;
    c[ControlIndex(d, "squash_stretch")] = -1;
    rig.Evaluate(c, f);
    const Vec3 rest = model.nodes[browL].rest.translation;
    const Vec3 moved = f.jointLocal[browL].translation - rest;
    CHECK(std::abs(moved.x - 0.0525) < 1e-12);
    CHECK(std::abs(moved.y - (0.06 - 0.1125)) < 1e-12);  // half a raise, plus following the squash
    CHECK(std::abs(moved.z - (-0.01 + 0.0745)) < 1e-12);
    CHECK(f.morphWeights[Index(d.morphs, "squash")] == 1.0);
    CHECK(f.morphWeights[Index(d.morphs, "stretch")] == 0.0);
    CHECK(f.curves[Index(d.curves, "wrinkle_brow_L")] == 0.5);
    CHECK(f.curves[Index(d.curves, "wrinkle_brow_R")] == 0.0);

    // out of range: clamped
    c[ControlIndex(d, "brow_raise_L")] = 7;
    rig.Evaluate(c, f);
    CHECK(f.curves[Index(d.curves, "wrinkle_brow_L")] == 1.0);
}

TEST_CASE("a frame poses the model by name; the circle at rest doesn't intersect") {
    const SkinnedModel model = LoadSkinnedGLB(kDir / "circle.glb");
    const OurRigEvaluator rig(LoadRigFile(kDir / "circle.rig.json"), model);
    DeformerFrame f;
    rig.Evaluate(DefaultControls(rig.Definition()), f);
    CHECK(VerifyIntersections(Deform(model, PoseFromFrame(model, rig.Definition(), f))).intersections.empty());
}

TEST_CASE("combinations are products of earlier values") {
    const SkinnedModel model = LoadSkinnedGLB(kDir / "circle.glb");
    std::string text = Text(kDir / "circle.rig.json");  // (it has no combinations: add the list)
    text.insert(text.find("\"joints\""), R"("combinations": [{"name": "up_and_squash", "of": ["brow_up_L", "squash"]}],
  )");
    text = With("curves", R"({"curve": "both", "from": "up_and_squash", "weight": 2})", text);
    const OurRigEvaluator rig(ParseRigFile(text), model);
    std::vector<double> c = DefaultControls(rig.Definition());
    c[ControlIndex(rig.Definition(), "brow_raise_L")] = 0.5;
    c[ControlIndex(rig.Definition(), "squash_stretch")] = -0.5;
    DeformerFrame f;
    rig.Evaluate(c, f);
    CHECK(f.curves[Index(rig.Definition().curves, "both")] == 2 * 0.5 * 0.5);
}

TEST_CASE("an invalid rig, or one that doesn't fit its model, fails and says where") {
    const SkinnedModel model = LoadSkinnedGLB(kDir / "circle.glb");
    CHECK(Failure(Text(kDir / "circle.rig.json"), model).empty());
    CHECK(Failure("{", model).find("malformed JSON") != std::string::npos);
    CHECK(Failure(R"({"controls": []})", model) != "");  // no name
    CHECK(Failure(With("joints", R"({"joint": "nose", "from": "brow_up_L"})"), model).find("no joint 'nose'") != std::string::npos);
    CHECK(Failure(With("joints", R"({"joint": "brow_L", "from": "frown"})"), model).find("'frown'") != std::string::npos);
    CHECK(Failure(With("morphs", R"({"morph": "smile", "from": "squash"})"), model).find("no morph 'smile'") != std::string::npos);
    CHECK(Failure(With("controls", R"({"name": "squash", "min": 0, "max": 1})"), model).find("already used") != std::string::npos);
    CHECK(Failure(With("controls", R"({"name": "wide", "min": 1, "max": 0})"), model).find("min must be less than max") != std::string::npos);
    CHECK(Failure(With("controls", R"({"name": "wide", "min": 0, "max": 1, "default": 2})"), model).find("default") != std::string::npos);
    CHECK(Failure(With("values", R"({"name": "x", "control": "brows_angry", "map": [[1, 0], [0, 1]]})"), model).find("must increase") != std::string::npos);
    CHECK(Failure(With("values", R"({"name": "x", "control": "brow_up_L", "map": [[0, 0], [1, 1]]})"), model).find("'brow_up_L'") != std::string::npos);
    CHECK(Failure(With("joints", R"({"joint": "brow_L", "from": "squash", "colour": 1})"), model) != "");  // unknown field
    CHECK(Failure(With("controls", R"({"name": "2fast", "min": 0, "max": 1})"), model) != "");             // not a name
}

TEST_CASE("a joint name that matches two nodes is ambiguous") {
    SkinnedModel model = LoadSkinnedGLB(kDir / "circle.glb");
    model.nodes[static_cast<size_t>(model.FindNode("head"))].name = "brow_L";
    CHECK(Failure(Text(kDir / "circle.rig.json"), model).find("more than one node named 'brow_L'") != std::string::npos);
}
