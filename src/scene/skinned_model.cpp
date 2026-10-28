#include "mesh_verifier/skinned_model.h"

#include <stdexcept>

namespace mesh_verifier {

int SkinnedModel::FindNode(const std::string& n) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].name == n) return static_cast<int>(i);
    return -1;
}

int SkinnedModel::FindMorph(const std::string& n) const {
    for (size_t i = 0; i < morphNames.size(); ++i)
        if (morphNames[i] == n) return static_cast<int>(i);
    return -1;
}

std::vector<size_t> SkinJoints(const SkinnedModel& model) {
    std::vector<char> used(model.nodes.size(), 0);
    for (const SkinnedPart& part : model.parts)
        for (const uint32_t j : part.joints) used[j] = 1;
    std::vector<size_t> out;
    for (size_t i = 0; i < used.size(); ++i)
        if (used[i]) out.push_back(i);
    return out;
}

std::vector<double> JointWeights(const SkinnedModel& model, const SkinnedPart& part, size_t node) {
    std::vector<double> w(part.positions.size(), 0.0);
    if (!part.Skinned()) {
        for (int n = static_cast<int>(part.node); n >= 0; n = model.nodes[static_cast<size_t>(n)].parent)
            if (static_cast<size_t>(n) == node) return std::vector<double>(part.positions.size(), 1.0);
        return w;
    }
    for (size_t v = 0; v < w.size(); ++v)
        for (uint32_t i = part.influenceStart[v]; i < part.influenceStart[v + 1]; ++i)
            if (part.influences[i].node == node) w[v] += part.influences[i].weight;
    return w;
}

ModelPose RestPose(const SkinnedModel& model) {
    ModelPose pose;
    for (const SkinNode& n : model.nodes) pose.local.push_back(n.rest);
    pose.morphWeights.assign(model.morphNames.size(), 0.0);
    return pose;
}

std::vector<Mat4> WorldMatrices(const SkinnedModel& model, const ModelPose& pose) {
    if (pose.local.size() != model.nodes.size()) throw std::invalid_argument("the pose has a transform for " + std::to_string(pose.local.size()) + " nodes, the model has " + std::to_string(model.nodes.size()));
    std::vector<Mat4> world(model.nodes.size());
    std::vector<char> done(model.nodes.size(), 0);
    // (parents may come after their children in the file)
    const auto compute = [&](auto&& self, size_t i) -> const Mat4& {
        if (!done[i]) {
            const int p = model.nodes[i].parent;
            world[i] = p < 0 ? Compose(pose.local[i]) : self(self, static_cast<size_t>(p)) * Compose(pose.local[i]);
            done[i] = 1;
        }
        return world[i];
    };
    for (size_t i = 0; i < model.nodes.size(); ++i) compute(compute, i);
    return world;
}

GeometryData Deform(const SkinnedModel& model, const ModelPose& pose) {
    if (pose.morphWeights.size() != model.morphNames.size()) throw std::invalid_argument("the pose has " + std::to_string(pose.morphWeights.size()) + " morph weights, the model has " + std::to_string(model.morphNames.size()) + " morphs");
    const std::vector<Mat4> world = WorldMatrices(model, pose);
    GeometryData out;
    for (const SkinnedPart& part : model.parts) {
        Mesh mesh;
        mesh.name = part.name;
        mesh.triangles = part.triangles;
        mesh.vertices.reserve(part.positions.size());
        std::vector<Mat4> skin;  // per skin joint: world * inverse bind
        for (size_t j = 0; j < part.joints.size(); ++j) skin.push_back(world[part.joints[j]] * part.inverseBind[j]);
        for (size_t v = 0; v < part.positions.size(); ++v) {
            Vec3 p = part.positions[v];
            for (size_t k = 0; k < part.morphs.size(); ++k) {
                const double w = pose.morphWeights[part.morphs[k]];
                if (w != 0.0) p = p + part.morphDeltas[k][v] * w;
            }
            if (!part.Skinned()) {
                mesh.vertices.push_back(TransformPoint(world[part.node], p));
                continue;
            }
            Vec3 q{0, 0, 0};
            for (uint32_t i = part.influenceStart[v]; i < part.influenceStart[v + 1]; ++i) {
                const SkinInfluence& in = part.influences[i];
                q = q + TransformPoint(skin[part.jointSlot[in.node]], p) * in.weight;
            }
            mesh.vertices.push_back(q);
        }
        out.meshes.push_back(std::move(mesh));
    }
    return out;
}

}  // namespace mesh_verifier
