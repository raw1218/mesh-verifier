#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "mesh_verifier/gltf_importer.h"
#include "mesh_verifier/intersection_verifier.h"
#include "mesh_verifier/skinned_model.h"

using namespace mesh_verifier;

namespace {

const std::filesystem::path kCircle = std::filesystem::path(EXAMPLES_DIR) / "rig" / "circle" / "circle.glb";

double MaxDistance(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i) d = std::max(d, Length(a[i] - b[i]));
    return d;
}

size_t Part(const SkinnedModel& m, const std::string& name) {
    for (size_t i = 0; i < m.parts.size(); ++i)
        if (m.parts[i].name == name) return i;
    FAIL("no part " << name);
    return 0;
}

}  // namespace

TEST_CASE("a skinned GLB: nodes, skinned parts and named morph targets") {
    const SkinnedModel m = LoadSkinnedGLB(kCircle);
    CHECK(m.nodes.size() == 7);
    REQUIRE(m.parts.size() == 3);
    CHECK(m.morphNames == std::vector<std::string>{"squash", "stretch"});
    const SkinnedPart& head = m.parts[Part(m, "head")];
    CHECK(head.Skinned());
    CHECK(head.morphs.size() == 2);
    CHECK(head.influenceStart.size() == head.positions.size() + 1);
    CHECK(m.parts[Part(m, "eyebrow_L")].morphs.empty());
    CHECK(m.FindNode("brow_L") >= 0);
}

TEST_CASE("the rest pose gives the bind positions, and nothing intersects") {
    const SkinnedModel m = LoadSkinnedGLB(kCircle);
    const GeometryData rest = Deform(m, RestPose(m));
    for (size_t p = 0; p < m.parts.size(); ++p) CHECK(MaxDistance(rest.meshes[p].vertices, m.parts[p].positions) < 1e-5);
    CHECK(VerifyIntersections(rest).intersections.empty());
}

TEST_CASE("joints move their skinned vertices; morphs move theirs") {
    const SkinnedModel m = LoadSkinnedGLB(kCircle);
    ModelPose pose = RestPose(m);
    pose.local[static_cast<size_t>(m.FindNode("brow_L"))].translation.y += 0.1;
    pose.morphWeights[static_cast<size_t>(m.FindMorph("squash"))] = 1.0;
    const GeometryData posed = Deform(m, pose);

    const size_t brow = Part(m, "eyebrow_L"), other = Part(m, "eyebrow_R"), head = Part(m, "head");
    for (size_t v = 0; v < m.parts[brow].positions.size(); ++v)
        CHECK(std::abs(posed.meshes[brow].vertices[v].y - (m.parts[brow].positions[v].y + 0.1)) < 1e-5);
    CHECK(MaxDistance(posed.meshes[other].vertices, m.parts[other].positions) < 1e-5);
    double top = 0;
    for (const Vec3& v : posed.meshes[head].vertices) top = std::max(top, v.y);
    CHECK(std::abs(top - 0.75) < 1e-4);  // squash: the head is 0.75 as tall
}
