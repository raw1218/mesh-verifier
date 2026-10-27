#include "mesh_rig/rig.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

#include "mesh_verifier/schema.h"

namespace mesh_rig {
namespace {

using nlohmann::json;
using mesh_verifier::ModelPose;
using mesh_verifier::SkinnedModel;

Vec3 ToVec3(const json& v) { return {v[0].get<double>(), v[1].get<double>(), v[2].get<double>()}; }

}  // namespace

// The node with this name: -1 if none, -2 if more than one.
int UniqueNode(const SkinnedModel& model, const std::string& name);

std::vector<double> DefaultControls(const RigDefinition& definition) {
    std::vector<double> out;
    for (const Control& c : definition.controls) out.push_back(c.defaultValue);
    return out;
}

int UniqueNode(const SkinnedModel& model, const std::string& name) {
    int found = -1;
    for (size_t i = 0; i < model.nodes.size(); ++i)
        if (model.nodes[i].name == name) {
            if (found >= 0) return -2;
            found = static_cast<int>(i);
        }
    return found;
}

ModelPose PoseFromFrame(const SkinnedModel& model, const RigDefinition& definition, const DeformerFrame& frame) {
    ModelPose pose = mesh_verifier::RestPose(model);
    // The model's own node list (as OurRigEvaluator makes it): matched one to one; otherwise by name.
    bool same = definition.joints.size() == model.nodes.size();
    for (size_t j = 0; same && j < definition.joints.size(); ++j) same = definition.joints[j] == model.nodes[j].name;
    for (size_t j = 0; j < definition.joints.size(); ++j) {
        const int node = same ? static_cast<int>(j) : UniqueNode(model, definition.joints[j]);
        if (node == -1) throw std::invalid_argument("the model has no joint '" + definition.joints[j] + "'");
        if (node == -2) throw std::invalid_argument("the model has more than one node named '" + definition.joints[j] + "'");
        pose.local[static_cast<size_t>(node)] = frame.jointLocal[j];
    }
    for (size_t m = 0; m < definition.morphs.size(); ++m) {
        const int morph = model.FindMorph(definition.morphs[m]);
        if (morph < 0) throw std::invalid_argument("the model has no morph '" + definition.morphs[m] + "'");
        pose.morphWeights[static_cast<size_t>(morph)] = frame.morphWeights[m];
    }
    return pose;
}

RigFile ParseRigFile(const std::string& text) {
    json doc;
    try {
        doc = json::parse(text);
    } catch (const json::parse_error& e) {
        throw RigError(std::string("malformed JSON: ") + e.what());
    }
    try {
        mesh_verifier::Validate(doc, mesh_verifier::Schema::Rig);
    } catch (const mesh_verifier::SchemaError& e) {
        throw RigError(e.what());
    }
    RigFile file;
    file.name = doc.at("name").get<std::string>();
    for (const json& c : doc.at("controls")) {
        Control control{c.at("name").get<std::string>(), c.at("min").get<double>(), c.at("max").get<double>(), 0.0};
        control.defaultValue = c.contains("default") ? c.at("default").get<double>() : std::clamp(0.0, control.min, control.max);
        file.controls.push_back(control);
    }
    for (const json& v : doc.value("values", json::array())) {
        ValueMap map{v.at("name").get<std::string>(), v.at("control").get<std::string>(), {}};
        for (const json& p : v.at("map")) map.points.emplace_back(p[0].get<double>(), p[1].get<double>());
        file.values.push_back(std::move(map));
    }
    for (const json& c : doc.value("combinations", json::array())) file.combinations.push_back({c.at("name").get<std::string>(), c.at("of").get<std::vector<std::string>>()});
    for (const json& j : doc.value("joints", json::array())) {
        JointOutput out{j.at("joint").get<std::string>(), j.at("from").get<std::string>()};
        if (j.contains("translation")) out.translation = ToVec3(j.at("translation"));
        if (j.contains("rotationEuler")) out.rotationEuler = ToVec3(j.at("rotationEuler"));
        if (j.contains("scale")) out.scale = ToVec3(j.at("scale"));
        file.joints.push_back(out);
    }
    for (const json& m : doc.value("morphs", json::array())) file.morphs.push_back({m.at("morph").get<std::string>(), m.at("from").get<std::string>(), m.value("weight", 1.0)});
    for (const json& c : doc.value("curves", json::array())) file.curves.push_back({c.at("curve").get<std::string>(), c.at("from").get<std::string>(), c.value("weight", 1.0)});
    return file;
}

RigFile LoadRigFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw RigError("cannot open rig file '" + path.string() + "'");
    std::ostringstream text;
    text << in.rdbuf();
    try {
        return ParseRigFile(text.str());
    } catch (const RigError& e) {
        throw RigError(path.string() + ": " + e.what());
    }
}

OurRigEvaluator::OurRigEvaluator(RigFile file, const SkinnedModel& model) {
    definition_.name = file.name;
    definition_.controls = file.controls;
    for (const mesh_verifier::SkinNode& n : model.nodes) {
        definition_.joints.push_back(n.name);
        definition_.jointRest.push_back(n.rest);
    }
    definition_.morphs = model.morphNames;

    // Every value by name: the controls, then the values, then the combinations, each defined before it's used.
    std::map<std::string, size_t> values;
    const auto define = [&](const std::string& name, const std::string& where) {
        if (!values.emplace(name, values.size()).second) throw RigError(where + ": the name '" + name + "' is already used");
    };
    const auto use = [&](const std::string& name, const std::string& where) {
        const auto it = values.find(name);
        if (it == values.end()) throw RigError(where + ": '" + name + "' isn't a control, value or combination defined before it");
        return it->second;
    };
    for (size_t i = 0; i < file.controls.size(); ++i) {
        const Control& c = file.controls[i];
        const std::string where = "/controls/" + std::to_string(i);
        if (!(c.min < c.max)) throw RigError(where + ": min must be less than max");
        if (c.defaultValue < c.min || c.defaultValue > c.max) throw RigError(where + ": the default is outside min..max");
        define(c.name, where);
    }
    for (size_t i = 0; i < file.values.size(); ++i) {
        const ValueMap& v = file.values[i];
        const std::string where = "/values/" + std::to_string(i);
        const size_t control = use(v.control, where);
        if (control >= file.controls.size()) throw RigError(where + ": '" + v.control + "' isn't a control");
        for (size_t k = 1; k < v.points.size(); ++k)
            if (!(v.points[k].first > v.points[k - 1].first)) throw RigError(where + "/map: the inputs must increase");
        define(v.name, where);
        maps_.push_back({control, v.points});
    }
    for (size_t i = 0; i < file.combinations.size(); ++i) {
        const Combination& c = file.combinations[i];
        const std::string where = "/combinations/" + std::to_string(i);
        std::vector<size_t> of;
        for (const std::string& n : c.of) of.push_back(use(n, where));
        define(c.name, where);
        combinations_.push_back(std::move(of));
    }
    for (size_t i = 0; i < file.joints.size(); ++i) {
        const JointOutput& j = file.joints[i];
        const std::string where = "/joints/" + std::to_string(i);
        const int node = UniqueNode(model, j.joint);
        if (node == -1) throw RigError(where + ": the model has no joint '" + j.joint + "'");
        if (node == -2) throw RigError(where + ": the model has more than one node named '" + j.joint + "'");
        joints_.push_back({static_cast<size_t>(node), use(j.from, where), j.translation, j.rotationEuler, j.scale});
    }
    for (size_t i = 0; i < file.morphs.size(); ++i) {
        const WeightOutput& m = file.morphs[i];
        const std::string where = "/morphs/" + std::to_string(i);
        const int morph = model.FindMorph(m.target);
        if (morph < 0) throw RigError(where + ": the model has no morph '" + m.target + "'");
        morphs_.push_back({static_cast<size_t>(morph), use(m.from, where), m.weight});
    }
    for (size_t i = 0; i < file.curves.size(); ++i) {
        const WeightOutput& c = file.curves[i];
        auto it = std::find(definition_.curves.begin(), definition_.curves.end(), c.target);
        if (it == definition_.curves.end()) {
            definition_.curves.push_back(c.target);
            it = definition_.curves.end() - 1;
        }
        curves_.push_back({static_cast<size_t>(it - definition_.curves.begin()), use(c.from, "/curves/" + std::to_string(i)), c.weight});
    }
}

namespace {

double Piecewise(const std::vector<std::pair<double, double>>& points, double x) {
    if (x <= points.front().first) return points.front().second;
    if (x >= points.back().first) return points.back().second;
    for (size_t k = 1; k < points.size(); ++k)
        if (x <= points[k].first) {
            const auto& [x0, y0] = points[k - 1];
            const auto& [x1, y1] = points[k];
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        }
    return points.back().second;
}

}  // namespace

void OurRigEvaluator::Evaluate(std::span<const double> controls, DeformerFrame& out) const {
    const size_t nc = definition_.controls.size();
    if (controls.size() != nc) throw std::invalid_argument("the rig has " + std::to_string(nc) + " controls, given " + std::to_string(controls.size()));
    std::vector<double> v;
    v.reserve(nc + maps_.size() + combinations_.size());
    for (size_t i = 0; i < nc; ++i) v.push_back(std::clamp(controls[i], definition_.controls[i].min, definition_.controls[i].max));
    for (const Map& m : maps_) v.push_back(Piecewise(m.points, v[m.control]));
    for (const std::vector<size_t>& of : combinations_) {
        double p = 1.0;
        for (const size_t i : of) p *= v[i];
        v.push_back(p);
    }

    const size_t nj = definition_.joints.size();
    std::vector<Vec3> dt(nj, {0, 0, 0}), dr(nj, {0, 0, 0}), ds(nj, {0, 0, 0});
    for (const Joint& j : joints_) {
        const double x = v[j.value];
        if (x == 0.0) continue;
        dt[j.joint] = dt[j.joint] + j.translation * x;
        dr[j.joint] = dr[j.joint] + j.rotationEuler * x;
        ds[j.joint] = ds[j.joint] + j.scale * x;
    }
    out.jointLocal = definition_.jointRest;
    for (size_t j = 0; j < nj; ++j) {
        TRS& t = out.jointLocal[j];
        t.translation = t.translation + dt[j];
        if (dr[j].x != 0 || dr[j].y != 0 || dr[j].z != 0) t.rotation = mesh_verifier::Normalized(t.rotation * mesh_verifier::QuatFromEuler(dr[j]));
        t.scale = t.scale + ds[j];
    }
    out.morphWeights.assign(definition_.morphs.size(), 0.0);
    for (const Weight& m : morphs_) out.morphWeights[m.target] += m.weight * v[m.value];
    out.curves.assign(definition_.curves.size(), 0.0);
    for (const Weight& c : curves_) out.curves[c.target] += c.weight * v[c.value];
}

}  // namespace mesh_rig
