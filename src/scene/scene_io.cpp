#include "mesh_verifier/scene_io.h"

#include <fstream>
#include <sstream>

#include "mesh_verifier/gltf_importer.h"
#include "mesh_verifier/obj_importer.h"
#include "mesh_verifier/schema.h"

namespace mesh_verifier {
namespace {

using nlohmann::json;

std::string Lower(const std::string& s) {
    std::string out;
    for (const char c : s) out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out;
}

Vec3 ToVec3(const json& v) { return {v[0].get<double>(), v[1].get<double>(), v[2].get<double>()}; }

void Check(const json& document, Schema schema) {
    try {
        Validate(document, schema);
    } catch (const SchemaError& e) {
        throw TransformFileError(e.what());
    }
}

// An edit already checked against transform-edit.schema.json.
TransformEdit Convert(const json& entry) {
    TransformEdit edit;
    edit.node = entry.at("node").get<size_t>();
    if (const auto it = entry.find("space"); it != entry.end()) edit.space = *it == "world" ? Space::World : Space::Local;
    if (const auto it = entry.find("translation"); it != entry.end()) edit.translation = ToVec3(*it);
    if (const auto it = entry.find("rotationEuler"); it != entry.end()) edit.rotationEuler = ToVec3(*it);
    if (const auto it = entry.find("scale"); it != entry.end()) edit.scale = ToVec3(*it);
    return edit;
}

}  // namespace

std::optional<ModelFormat> ParseModelFormat(const std::string& name) {
    const std::string lower = Lower(name);
    if (lower == "obj") return ModelFormat::Obj;
    if (lower == "glb") return ModelFormat::Glb;
    return std::nullopt;
}

Scene LoadScene(const std::filesystem::path& path, ModelFormat format) {
    return format == ModelFormat::Obj ? LoadOBJScene(path) : LoadGLB(path);
}

Scene ParseScene(const std::string& bytes, ModelFormat format, const std::string& name) {
    return format == ModelFormat::Obj ? ParseOBJScene(bytes, name) : ParseGLB(bytes, name);
}

std::optional<UpAxis> ParseUpAxis(const std::string& name) {
    const std::string lower = Lower(name);
    if (lower == "y") return UpAxis::Y;
    if (lower == "z") return UpAxis::Z;
    return std::nullopt;
}

const char* UpAxisName(UpAxis up) { return up == UpAxis::Z ? "z" : "y"; }

TransformEdit TransformEditFromJson(const json& edit) {
    Check(edit, Schema::TransformEdit);
    return Convert(edit);
}

std::vector<TransformEdit> TransformEditsFromJson(const json& document) {
    Check(document, Schema::Transforms);
    std::vector<TransformEdit> edits;
    for (const json& entry : document.at("transforms")) edits.push_back(Convert(entry));
    return edits;
}

std::vector<TransformEdit> ParseTransformEdits(const std::string& text) {
    json document;
    try {
        document = json::parse(text);
    } catch (const json::parse_error& e) {
        throw TransformFileError(std::string("malformed JSON: ") + e.what());
    }
    return TransformEditsFromJson(document);
}

std::vector<TransformEdit> LoadTransformEdits(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw TransformFileError("cannot open transforms file '" + path.string() + "'");
    std::ostringstream text;
    text << file.rdbuf();
    try {
        return ParseTransformEdits(text.str());
    } catch (const TransformFileError& e) {
        throw TransformFileError(path.string() + ": " + e.what());
    }
}

json IntersectionsJson(const Scene& scene, const VerificationReport& report) {
    json list = json::array();
    const auto ref = [&](const TriangleRef& r) {
        return json{{"node", r.meshIndex}, {"name", scene.Node(r.meshIndex).name}, {"triangle", r.triangleIndex}};
    };
    for (const Intersection& hit : report.intersections) list.push_back({{"a", ref(hit.a)}, {"b", ref(hit.b)}});
    return {{"intersections", list}};
}

}  // namespace mesh_verifier
