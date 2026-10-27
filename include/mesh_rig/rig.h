// The rig layer: control values in, deformer values out (joint transforms, morph weights, curves). Everything below
// it (Deform, the verifier, Blender, Unreal) sees only the output, so rigs are swappable: OurRigEvaluator runs a rig
// file (schema/rig.schema.json); another evaluator (a MetaHuman DNA through RigLogic, say) fills the same frame.
#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "mesh_verifier/skinned_model.h"

namespace mesh_rig {

using mesh_verifier::TRS;
using mesh_verifier::Vec3;

struct Control {
    std::string name;
    double min = 0.0, max = 1.0;
    double defaultValue = 0.0;
};

// What a rig takes and produces; fixed once the rig is loaded.
struct RigDefinition {
    std::string name;
    std::vector<Control> controls;
    std::vector<std::string> joints;  // the joints the frame poses, by name
    std::vector<TRS> jointRest;       // their rest transforms
    std::vector<std::string> morphs;  // morph target names
    std::vector<std::string> curves;  // other outputs
};

// One evaluation: per joint its local transform, per morph its weight, per curve its value (in definition order).
struct DeformerFrame {
    std::vector<TRS> jointLocal;
    std::vector<double> morphWeights;
    std::vector<double> curves;
};

class RigEvaluator {
public:
    virtual ~RigEvaluator() = default;
    virtual const RigDefinition& Definition() const = 0;
    // `controls` in definition order; values out of range are clamped.
    virtual void Evaluate(std::span<const double> controls, DeformerFrame& out) const = 0;
};

// The controls at their defaults.
std::vector<double> DefaultControls(const RigDefinition& definition);

// A frame as a pose of `model`: joints and morphs matched by name (any joint or morph the frame doesn't name keeps its
// rest value). Throws std::invalid_argument when the frame names a joint or morph the model doesn't have.
mesh_verifier::ModelPose PoseFromFrame(const mesh_verifier::SkinnedModel& model, const RigDefinition& definition, const DeformerFrame& frame);

// ---- our rig file (schema/rig.schema.json)

struct ValueMap {
    std::string name, control;
    std::vector<std::pair<double, double>> points;  // increasing inputs
};
struct Combination {
    std::string name;
    std::vector<std::string> of;
};
struct JointOutput {
    std::string joint, from;
    Vec3 translation{0, 0, 0}, rotationEuler{0, 0, 0}, scale{0, 0, 0};
};
struct WeightOutput {
    std::string target, from;
    double weight = 1.0;
};
struct RigFile {
    std::string name;
    std::vector<Control> controls;
    std::vector<ValueMap> values;
    std::vector<Combination> combinations;
    std::vector<JointOutput> joints;
    std::vector<WeightOutput> morphs, curves;
};

// Thrown for an invalid rig file, or one that doesn't fit its model. The message says where and why.
class RigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

RigFile ParseRigFile(const std::string& text);
RigFile LoadRigFile(const std::filesystem::path& path);

// A rig file as JSON (schema/rig.schema.json).
nlohmann::json RigFileJson(const RigFile& file);

// The rules one control drives: the part of the rig file that depends on it. That control alone, the values made
// from it, the combinations using any of those (or earlier such combinations), and every joint, morph and curve
// output fed by any of them. Throws RigError for an unknown control.
RigFile ControlRules(const RigFile& file, const std::string& control);

// Runs a rig file on a model: the definition's joints are all the model's nodes, its morphs all the model's morphs,
// its curves the file's. Throws RigError if the file refers to anything undefined, or to a joint or morph the model
// doesn't have.
class OurRigEvaluator : public RigEvaluator {
public:
    OurRigEvaluator(RigFile file, const mesh_verifier::SkinnedModel& model);
    const RigDefinition& Definition() const override { return definition_; }
    void Evaluate(std::span<const double> controls, DeformerFrame& out) const override;

private:
    struct Map {
        size_t control;
        std::vector<std::pair<double, double>> points;
    };
    struct Joint {
        size_t joint, value;
        Vec3 translation, rotationEuler, scale;
    };
    struct Weight {
        size_t target, value;
        double weight;
    };
    RigDefinition definition_;
    std::vector<Map> maps_;
    std::vector<std::vector<size_t>> combinations_;
    std::vector<Joint> joints_;
    std::vector<Weight> morphs_, curves_;
};

}  // namespace mesh_rig
