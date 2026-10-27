#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "mesh_verifier/gltf_importer.h"

using namespace mesh_verifier;
using Catch::Approx;

namespace {

const std::filesystem::path kFixtures = FIXTURE_DIR;

void PutU32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}

template <class T>
void Put(std::string& s, T v) {
    char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    s.append(b, sizeof(T));
}

// A GLB from its JSON (without "buffers": one is added for `bin`) and binary chunk.
std::string Glb(std::string json, std::string bin) {
    while (bin.size() % 4) bin += '\0';
    if (!bin.empty()) json.insert(json.rfind('}'), ",\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}]");
    while (json.size() % 4) json += ' ';
    std::string out = "glTF";
    PutU32(out, 2);
    PutU32(out, static_cast<uint32_t>(12 + 8 + json.size() + (bin.empty() ? 0 : 8 + bin.size())));
    PutU32(out, static_cast<uint32_t>(json.size()));
    PutU32(out, 0x4E4F534A);
    out += json;
    if (!bin.empty()) {
        PutU32(out, static_cast<uint32_t>(bin.size()));
        PutU32(out, 0x004E4942);
        out += bin;
    }
    return out;
}

std::string Floats(const std::vector<float>& v) {
    std::string s;
    for (const float f : v) Put(s, f);
    return s;
}

// Four positions (a unit square at z = 0) at byte 0, then the given index bytes.
const std::vector<float> kSquare = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};

}  // namespace

TEST_CASE("a GLB's nodes are the components, with their hierarchy") {
    const Scene s = LoadGLB(kFixtures / "hierarchy.glb");
    REQUIRE(s.NodeCount() == 5);
    CHECK(s.Name() == "hierarchy");
    CHECK(s.Node(0).name == "root");
    CHECK(s.Node(0).mesh == -1);
    CHECK(s.Node(0).children == std::vector<int>{1, 2});
    CHECK(s.Node(3).parent == 2);
    CHECK(s.Node(1).mesh == s.Node(2).mesh);  // one shared source mesh
    REQUIRE(s.Meshes().size() == 1);
    CHECK(s.Meshes()[0].triangles.size() == 12);
    CHECK(s.Node(4).inScene == false);
    CHECK(TransformPoint(s.WorldMatrix(3), {0, 0, 0}) == Vec3{3, 3, 0});
    std::vector<uint32_t> meshNodes;
    s.WorldGeometry(&meshNodes);
    CHECK(meshNodes == std::vector<uint32_t>{1, 2, 3});
}

TEST_CASE("GLB transforms: rotation, scale and translation compose down the hierarchy") {
    const Scene s = LoadGLB(kFixtures / "transformed_parent.glb");
    const TRS world = s.WorldTRS(1);
    CHECK(world.translation.x == Approx(10));
    CHECK(world.translation.y == Approx(2));
    CHECK(world.scale.x == Approx(2));
    // the cube's corner (0.5, 0.5, 0.5): scaled by 2 and turned a quarter about Z around (10, 2, 0)
    const Vec3 p = TransformPoint(s.WorldMatrix(1), {0.5, 0.5, 0.5});
    CHECK(p.x == Approx(9));
    CHECK(p.y == Approx(3));
    CHECK(p.z == Approx(1));
}

TEST_CASE("primitives are merged, strips and fans unrolled, points and lines skipped") {
    std::string bin = Floats(kSquare);
    for (const uint16_t i : {0, 1, 2, 0, 2, 3}) Put(bin, i);  // bytes 48..60
    const std::string json = R"({"asset":{"version":"2.0"},
        "nodes":[{"mesh":0}],
        "meshes":[{"primitives":[
            {"attributes":{"POSITION":0},"indices":1},
            {"attributes":{"POSITION":0},"mode":5},
            {"attributes":{"POSITION":0},"mode":6},
            {"attributes":{"POSITION":0},"mode":1}]}],
        "bufferViews":[{"buffer":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":12}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},
                     {"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}]})";
    const Scene s = ParseGLB(Glb(json, bin));
    const Mesh& m = s.Meshes()[0];
    CHECK(m.vertices.size() == 12);  // three triangle primitives of 4 vertices each
    REQUIRE(m.triangles.size() == 2 + 2 + 2);
    CHECK(m.triangles[0] == Triangle{0, 1, 2});
    CHECK(m.triangles[1] == Triangle{0, 2, 3});
    CHECK(m.triangles[2] == Triangle{4, 5, 6});
    CHECK(m.triangles[3] == Triangle{5, 7, 6});
    CHECK(m.triangles[4] == Triangle{9, 10, 8});
    CHECK(m.triangles[5] == Triangle{10, 11, 8});
    CHECK(s.Node(0).inScene);  // no scenes: everything is shown
}

TEST_CASE("node matrices, strides, sparse accessors and data URIs") {
    // positions interleaved with padding (stride 16), a sparse override moving vertex 2
    std::string pos;
    for (int v = 0; v < 4; ++v) pos += Floats({kSquare[3 * v], kSquare[3 * v + 1], kSquare[3 * v + 2], 99});
    std::string bin = pos;  // 64 bytes
    Put(bin, uint8_t{2});
    bin += std::string(3, '\0');                 // sparse index at 64
    bin += Floats({5, 5, 5});                    // sparse value at 68
    for (const uint32_t i : {0u, 1u, 2u}) Put(bin, i);  // indices at 80
    const std::string json = R"({"asset":{"version":"2.0"},
        "scene":0,"scenes":[{"nodes":[0]}],
        "nodes":[{"mesh":0,"matrix":[2,0,0,0, 0,2,0,0, 0,0,2,0, 1,2,3,1]}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
        "bufferViews":[{"buffer":0,"byteLength":64,"byteStride":16},{"buffer":0,"byteOffset":80,"byteLength":12},
                       {"buffer":0,"byteOffset":64,"byteLength":4},{"buffer":0,"byteOffset":68,"byteLength":12}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3",
                      "sparse":{"count":1,"indices":{"bufferView":2,"componentType":5121},"values":{"bufferView":3}}},
                     {"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}]})";
    const Scene s = ParseGLB(Glb(json, bin));
    const Mesh& m = s.Meshes()[0];
    CHECK(m.vertices[1] == Vec3{1, 0, 0});
    CHECK(m.vertices[2] == Vec3{5, 5, 5});
    CHECK(s.LocalTransform(0).trs.translation == Vec3{1, 2, 3});
    CHECK(s.LocalTransform(0).trs.scale == Vec3{2, 2, 2});
    CHECK_FALSE(s.LocalTransform(0).matrix.has_value());

    // the same geometry as a .gltf with an embedded buffer
    std::string b64;
    const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string raw = Floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
    for (size_t i = 0; i < raw.size(); i += 3) {
        uint32_t n = static_cast<unsigned char>(raw[i]) << 16;
        if (i + 1 < raw.size()) n |= static_cast<unsigned char>(raw[i + 1]) << 8;
        if (i + 2 < raw.size()) n |= static_cast<unsigned char>(raw[i + 2]);
        b64 += table[(n >> 18) & 63];
        b64 += table[(n >> 12) & 63];
        b64 += i + 1 < raw.size() ? table[(n >> 6) & 63] : '=';
        b64 += i + 2 < raw.size() ? table[n & 63] : '=';
    }
    const std::string gltf = R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
        "buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)" + b64 + R"("}],
        "bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}]})";
    const Scene t = ParseGLB(gltf);
    REQUIRE(t.Meshes()[0].triangles.size() == 1);
    CHECK(t.Meshes()[0].vertices[2] == Vec3{0, 1, 0});
}

TEST_CASE("a sheared node matrix is kept as a matrix") {
    const std::string json = R"({"asset":{"version":"2.0"},"nodes":[{"matrix":[1,0,0,0, 0.5,1,0,0, 0,0,1,0, 0,0,0,1]}]})";
    const Scene s = ParseGLB(Glb(json, ""));
    REQUIRE(s.LocalTransform(0).matrix.has_value());
    CHECK(TransformPoint(s.WorldMatrix(0), {0, 1, 0}) == Vec3{0.5, 1, 0});
}

TEST_CASE("malformed or unsupported GLB files are errors") {
    const std::string okJson = R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
        "bufferViews":[{"buffer":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":6}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    std::string bin = Floats(kSquare);
    for (const uint16_t i : {0, 1, 2}) Put(bin, i);
    CHECK_NOTHROW(ParseGLB(Glb(okJson, bin)));

    std::string badIndex = Floats(kSquare);
    for (const uint16_t i : {0, 1, 4}) Put(badIndex, i);
    CHECK_THROWS_AS(ParseGLB(Glb(okJson, badIndex)), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(okJson, Floats(kSquare))), GltfImportError);  // the indices are past the end
    CHECK_THROWS_AS(ParseGLB("not a model"), GltfImportError);
    std::string v1 = Glb(okJson, bin);
    v1[4] = 1;
    CHECK_THROWS_AS(ParseGLB(v1), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(okJson, bin).substr(0, 40)), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_draco_mesh_compression"]})", "")), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(R"({"asset":{"version":"2.0"},"nodes":[{"children":[0]}]})", "")), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{"children":[0]}]})", "")), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(R"({"asset":{"version":"2.0"},"nodes":[{"mesh":3}]})", "")), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(R"({"asset":{"version":"2.0"},"nodes":[{"translation":[1,2]}]})", "")), GltfImportError);
    CHECK_THROWS_AS(ParseGLB(Glb(R"({"asset":)", "")), GltfImportError);
    CHECK_THROWS_AS(LoadGLB(kFixtures / "does_not_exist.glb"), GltfImportError);
    CHECK_THROWS_AS(LoadGLB(kFixtures / "triangle.obj"), GltfImportError);
}
