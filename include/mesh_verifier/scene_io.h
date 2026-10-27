// Loading a scene in either format, the transforms file, and the JSON result (the formats are in contracts.md; the
// JSON documents are described by schema/).
#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mesh_verifier/scene.h"
#include "mesh_verifier/verification_report.h"

namespace mesh_verifier {

enum class ModelFormat { Obj, Glb };

// "obj" or "glb" (any letter case); nullopt otherwise.
std::optional<ModelFormat> ParseModelFormat(const std::string& name);

// The format chosen, not the file's extension, selects the loader.
Scene LoadScene(const std::filesystem::path& path, ModelFormat format);
Scene ParseScene(const std::string& bytes, ModelFormat format, const std::string& name);

// "y" or "z"; nullopt otherwise.
std::optional<UpAxis> ParseUpAxis(const std::string& name);
const char* UpAxisName(UpAxis up);

// Thrown for an invalid transforms document or edit. The message says where in the document and why.
class TransformFileError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The edits of a transforms document (schema/transforms.schema.json). Only the document's form is checked here:
// node indices are checked when the edits are applied (Scene::Apply).
std::vector<TransformEdit> ParseTransformEdits(const std::string& text);
std::vector<TransformEdit> TransformEditsFromJson(const nlohmann::json& document);
// One edit (schema/transform-edit.schema.json).
TransformEdit TransformEditFromJson(const nlohmann::json& edit);
std::vector<TransformEdit> LoadTransformEdits(const std::filesystem::path& path);

// {"intersections": [{"a": {"node": n, "triangle": t}, "b": {...}}, ...]} (schema/intersections.schema.json), for
// a report from VerifyScene.
nlohmann::json IntersectionsJson(const VerificationReport& report);

}  // namespace mesh_verifier
