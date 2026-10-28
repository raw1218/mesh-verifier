// A skinned model: meshes deformed by a skeleton (skin weights) and by morph targets, as glTF and Unreal Engine
// define them. A pose sets each node's local transform and each morph's weight; Deform places the meshes for it,
// the way Unreal does: morph targets first, then linear blend skinning.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mesh_verifier/geometry.h"
#include "mesh_verifier/transform.h"

namespace mesh_verifier {

// A node of the model's hierarchy (the glTF nodes): skeleton joints and the nodes holding meshes alike.
struct SkinNode {
    std::string name;
    int parent = -1;
    TRS rest;  // its local transform in the file
};

struct SkinInfluence {
    uint32_t node = 0;  // the joint
    double weight = 0.0;
};

// One mesh as placed by one node.
struct SkinnedPart {
    std::string name;  // the node's name
    size_t node = 0;
    std::vector<Vec3> positions;  // as stored (bind pose)
    std::vector<Triangle> triangles;
    // Skinned: vertex v's influences are influences[influenceStart[v] .. influenceStart[v + 1]), weights summing to
    // 1, and its world position is sum(weight * world(joint) * inverseBind(joint) * p). Not skinned (no influences):
    // the part follows its node rigidly.
    std::vector<uint32_t> influenceStart;
    std::vector<SkinInfluence> influences;
    std::vector<uint32_t> joints;     // the skin's joints (node indices)
    std::vector<Mat4> inverseBind;    // per entry of `joints`
    std::vector<uint32_t> jointSlot;  // per node: its index in `joints` (or UINT32_MAX)
    // Morph targets: morphs[k] is the model's morph index of target k, morphDeltas[k] its offset per vertex.
    std::vector<size_t> morphs;
    std::vector<std::vector<Vec3>> morphDeltas;

    bool Skinned() const { return !influences.empty(); }
};

struct SkinnedModel {
    std::string name;
    std::vector<SkinNode> nodes;
    std::vector<std::string> morphNames;  // every morph target name, each once (parts' targets with one name move together)
    std::vector<SkinnedPart> parts;

    // The index of the node or morph with this name, or -1.
    int FindNode(const std::string& name) const;
    int FindMorph(const std::string& name) const;
};

struct ModelPose {
    std::vector<TRS> local;            // per node
    std::vector<double> morphWeights;  // per morph name
};

// The nodes the parts are skinned to (any part's skin joints), in node order.
std::vector<size_t> SkinJoints(const SkinnedModel& model);

// How much each of a part's vertices follows `node`: its skin weight for that joint (0 if none); for a part that
// isn't skinned, 1 when it hangs from that node (it is the node, or below it), else 0.
std::vector<double> JointWeights(const SkinnedModel& model, const SkinnedPart& part, size_t node);

// Every node at its rest transform, every morph at 0.
ModelPose RestPose(const SkinnedModel& model);

// Each node's world matrix in a pose.
std::vector<Mat4> WorldMatrices(const SkinnedModel& model, const ModelPose& pose);

// The model's parts placed for a pose, in world space: one mesh per part, named after it, in part order.
GeometryData Deform(const SkinnedModel& model, const ModelPose& pose);

}  // namespace mesh_verifier
