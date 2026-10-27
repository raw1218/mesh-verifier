#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "mesh_verifier/obj_importer.h"
#include "mesh_verifier/scene_io.h"
#include "mesh_verifier/schema.h"

using namespace mesh_verifier;
using nlohmann::json;

namespace {

// The message a transforms document fails with ("" if it doesn't).
std::string Failure(const std::string& text) {
    try {
        ParseTransformEdits(text);
    } catch (const TransformFileError& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST_CASE("every schema is embedded and parses") {
    for (const Schema s : {Schema::TransformEdit, Schema::Transforms, Schema::Reset, Schema::Intersections, Schema::View, Schema::State, Schema::Scene, Schema::Error}) {
        INFO(SchemaFileName(s));
        CHECK_NOTHROW(json::parse(SchemaText(s)));
        CHECK_NOTHROW(Validate(json::object(), Schema::Reset));  // (parses every schema, and the files they refer to)
    }
}

TEST_CASE("transforms documents are read through their schema") {
    const auto edits = ParseTransformEdits(R"({"transforms":[{"node":2,"space":"world","translation":[0,1,2],"rotationEuler":[0,0,45],"scale":[1,2,3]},{"node":0}]})");
    REQUIRE(edits.size() == 2);
    CHECK(edits[0].node == 2);
    CHECK(edits[0].space == Space::World);
    CHECK(edits[0].translation == Vec3{0, 1, 2});
    CHECK(edits[0].rotationEuler == Vec3{0, 0, 45});
    CHECK(edits[0].scale == Vec3{1, 2, 3});
    CHECK(edits[1].space == Space::Local);
    CHECK_FALSE(edits[1].translation.has_value());
    CHECK(ParseTransformEdits(R"({"transforms":[]})").empty());
}

TEST_CASE("an invalid transforms document fails, naming where") {
    CHECK(Failure("{") .find("malformed JSON") != std::string::npos);
    CHECK_FALSE(Failure("[]").empty());
    CHECK_FALSE(Failure("{}").empty());                                                  // no "transforms"
    CHECK(Failure(R"({"transforms":[{}]})").find("/transforms/0") != std::string::npos);  // no "node"
    CHECK(Failure(R"({"transforms":[{"node":-1}]})").find("/transforms/0/node") != std::string::npos);
    CHECK(Failure(R"({"transforms":[{"node":1.5}]})").find("/transforms/0/node") != std::string::npos);
    CHECK(Failure(R"({"transforms":[{"node":"1"}]})").find("/transforms/0/node") != std::string::npos);
    CHECK(Failure(R"({"transforms":[{"node":1,"space":"screen"}]})").find("/transforms/0/space") != std::string::npos);
    CHECK(Failure(R"({"transforms":[{"node":1,"space":"LOCAL"}]})").find("/transforms/0/space") != std::string::npos);
    CHECK(Failure(R"({"transforms":[{"node":1,"translation":[1,2]}]})").find("/transforms/0/translation") != std::string::npos);
    CHECK(Failure(R"({"transforms":[{"node":1,"scale":[1,2,"3"]}]})").find("/transforms/0/scale") != std::string::npos);
    CHECK_FALSE(Failure(R"({"transforms":[{"node":1,"colour":"red"}]})").empty());  // unknown field
    CHECK_FALSE(Failure(R"({"transforms":[],"extra":1})").empty());
}

TEST_CASE("the intersections output names both components, and matches its schema") {
    const Scene scene = ParseOBJScene("o skull\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no jaw\nv 0 0 1\nv 1 0 1\nv 0 1 1\nf 4 5 6\n", "head");
    VerificationReport report;
    report.intersections.push_back({{0, 0}, {1, 0}, IntersectionType::Crossing, 1.0, {}, {}});
    const json out = IntersectionsJson(scene, report);
    CHECK_NOTHROW(Validate(out, Schema::Intersections));
    CHECK(out == json::parse(R"({"intersections":[{"a":{"node":0,"name":"skull","triangle":0},"b":{"node":1,"name":"jaw","triangle":0}}]})"));
    CHECK_THROWS_AS(Validate(json::parse(R"({"intersections":[{"a":{"node":0,"triangle":0},"b":{"node":1,"triangle":0}}]})"), Schema::Intersections), SchemaError);
}
