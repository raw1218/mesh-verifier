#include "mesh_verifier/schema.h"

#include <array>
#include <memory>

#define VALIJSON_USE_EXCEPTIONS 1
#include <valijson/adapters/nlohmann_json_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validation_results.hpp>
#include <valijson/validator.hpp>

#include "util/embedded.h"

namespace mesh_verifier {
namespace {

constexpr size_t kSchemaCount = static_cast<size_t>(Schema::Error) + 1;

// The embedded file a "$ref" names: "common.schema.json#/definitions/vec3" and ".../common.schema.json" are both
// common.schema.json (the part after the last '/', before any '#').
const std::string& FileText(const std::string& uri) {
    std::string name = uri.substr(0, uri.find('#'));
    if (const size_t slash = name.rfind('/'); slash != std::string::npos) name = name.substr(slash + 1);
    const auto& files = SchemaFiles();
    const auto it = files.find(name);
    if (it == files.end()) throw std::logic_error("no embedded schema '" + name + "'");
    return it->second;
}

// Every schema, parsed once (with the files it refers to).
const std::array<std::unique_ptr<valijson::Schema>, kSchemaCount>& Parsed() {
    static const auto parsed = [] {
        std::array<std::unique_ptr<valijson::Schema>, kSchemaCount> out;
        for (size_t i = 0; i < kSchemaCount; ++i) {
            const nlohmann::json document = nlohmann::json::parse(SchemaText(static_cast<Schema>(i)));
            out[i] = std::make_unique<valijson::Schema>();
            valijson::SchemaParser parser;
            parser.populateSchema(
                valijson::adapters::NlohmannJsonAdapter(document), *out[i],
                [](const std::string& uri) -> const nlohmann::json* { return new nlohmann::json(nlohmann::json::parse(FileText(uri))); },
                [](const nlohmann::json* doc) { delete doc; });
        }
        return out;
    }();
    return parsed;
}

}  // namespace

std::string SchemaFileName(Schema schema) {
    switch (schema) {
        case Schema::TransformEdit: return "transform-edit.schema.json";
        case Schema::Transforms: return "transforms.schema.json";
        case Schema::Reset: return "reset.schema.json";
        case Schema::Intersections: return "intersections.schema.json";
        case Schema::View: return "view.schema.json";
        case Schema::State: return "state.schema.json";
        case Schema::Scene: return "scene.schema.json";
        case Schema::Rig: return "rig.schema.json";
        case Schema::RigReport: return "rig-report.schema.json";
        case Schema::Error: return "error.schema.json";
    }
    throw std::logic_error("unknown schema");
}

const std::string& SchemaText(Schema schema) { return FileText(SchemaFileName(schema)); }

void Validate(const nlohmann::json& document, Schema schema) {
    valijson::Validator validator;  // (strict types: 2.0 isn't an integer, "2" isn't a number)
    valijson::ValidationResults results;
    if (validator.validate(*Parsed()[static_cast<size_t>(schema)], valijson::adapters::NlohmannJsonAdapter(document), &results)) return;
    // The deepest error says the most ("/transforms/0/space: not one of the values") rather than its parents'
    // ("item 0 failed").
    valijson::ValidationResults::Error deepest, error;
    while (results.popError(error))
        if (deepest.description.empty() || error.jsonPointer.size() > deepest.jsonPointer.size()) deepest = error;
    throw SchemaError((deepest.jsonPointer.empty() ? std::string("the document") : deepest.jsonPointer) + ": " + deepest.description);
}

}  // namespace mesh_verifier
