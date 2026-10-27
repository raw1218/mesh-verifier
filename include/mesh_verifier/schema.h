// The JSON documents read and written (the transforms file, the HTTP API's requests and responses) are described
// by the JSON Schema files in schema/, embedded in the library. Every document read is checked against its schema
// before it is used; the tests check every document written.
#pragma once

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace mesh_verifier {

enum class Schema {
    TransformEdit,  // transform-edit.schema.json
    Transforms,     // transforms.schema.json
    Reset,          // reset.schema.json
    Intersections,  // intersections.schema.json
    View,           // view.schema.json
    State,          // state.schema.json
    Scene,          // scene.schema.json
    Controls,       // controls.schema.json
    Pose,           // pose.schema.json
    Posed,          // posed.schema.json
    Rig,            // rig.schema.json
    RigReport,      // rig-report.schema.json
    Error,          // error.schema.json (keep last)
};

// Thrown when a document doesn't match its schema. The message says where in the document and what's wrong.
class SchemaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The schema's file name, e.g. "view.schema.json", and its text.
std::string SchemaFileName(Schema schema);
const std::string& SchemaText(Schema schema);

// Throws SchemaError unless `document` matches `schema`.
void Validate(const nlohmann::json& document, Schema schema);

}  // namespace mesh_verifier
