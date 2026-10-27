#include "mesh_verifier/scene.h"

#include <cmath>

#include "mesh_verifier/intersection_verifier.h"

namespace mesh_verifier {
namespace {

std::string NodeLabel(size_t i) { return "node " + std::to_string(i); }

void RequireFinite(const std::optional<Vec3>& v, const char* what, size_t node) {
    if (v && !IsFinite(*v)) throw SceneError(NodeLabel(node) + ": " + what + " has a value that is not finite");
}

// The TRS of a transform and whether it's exact (no shear).
NodeTransform FromMatrix(const Mat4& m) {
    NodeTransform t;
    bool exact = false;
    t.trs = Decompose(m, &exact);
    if (!exact) t.matrix = m;
    return t;
}

}  // namespace

Scene::Scene(std::string name, std::vector<Mesh> meshes, std::vector<SceneNode> nodes)
    : name_(std::move(name)), meshes_(std::move(meshes)), nodes_(std::move(nodes)) {
    const int n = static_cast<int>(nodes_.size());
    std::vector<int> parentFromChildren(n, -1);
    for (int i = 0; i < n; ++i) {
        const SceneNode& node = nodes_[i];
        if (node.mesh < -1 || node.mesh >= static_cast<int>(meshes_.size())) throw SceneError(NodeLabel(i) + " uses mesh " + std::to_string(node.mesh) + ", which doesn't exist");
        if (node.parent < -1 || node.parent >= n || node.parent == i) throw SceneError(NodeLabel(i) + " has an invalid parent");
        for (const int c : node.children) {
            if (c < 0 || c >= n || c == i) throw SceneError(NodeLabel(i) + " has an invalid child " + std::to_string(c));
            if (parentFromChildren[c] != -1) throw SceneError(NodeLabel(c) + " has more than one parent");
            parentFromChildren[c] = i;
        }
    }
    for (int i = 0; i < n; ++i)
        if (nodes_[i].parent != parentFromChildren[i]) throw SceneError(NodeLabel(i) + ": its parent and its parent's children disagree");
    for (int i = 0; i < n; ++i) {
        int steps = 0;
        for (int p = nodes_[i].parent; p != -1; p = nodes_[p].parent)
            if (++steps > n) throw SceneError("the node hierarchy has a cycle through " + NodeLabel(i));
        const NodeTransform& t = nodes_[i].local;
        if (!IsFinite(t.trs.translation) || !IsFinite(t.trs.scale) || !std::isfinite(t.trs.rotation.x) || !std::isfinite(t.trs.rotation.y) ||
            !std::isfinite(t.trs.rotation.z) || !std::isfinite(t.trs.rotation.w))
            throw SceneError(NodeLabel(i) + " has a transform that is not finite");
    }
    imported_.reserve(nodes_.size());
    for (const SceneNode& node : nodes_) imported_.push_back(node.local);
}

const SceneNode& Scene::Node(size_t i) const {
    if (i >= nodes_.size()) throw SceneError(NodeLabel(i) + " doesn't exist (the scene has " + std::to_string(nodes_.size()) + " nodes)");
    return nodes_[i];
}

std::vector<int> Scene::Roots() const {
    std::vector<int> roots;
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].parent == -1) roots.push_back(static_cast<int>(i));
    return roots;
}

const NodeTransform& Scene::ImportedTransform(size_t i) const {
    Node(i);
    return imported_[i];
}

Mat4 Scene::WorldMatrix(size_t i) const {
    Mat4 m = Node(i).local.Matrix();
    for (int p = nodes_[i].parent; p != -1; p = nodes_[p].parent) m = nodes_[p].local.Matrix() * m;
    return m;
}

bool Scene::IsModified(size_t i) const {
    const NodeTransform& a = Node(i).local;
    const NodeTransform& b = imported_[i];
    return !(a.Matrix() == b.Matrix());
}

void Scene::ApplyOne(const TransformEdit& e) {
    SceneNode& node = nodes_[e.node];
    const Mat4 parentWorld = node.parent == -1 ? Mat4::Identity() : WorldMatrix(static_cast<size_t>(node.parent));
    // under identity parents (a root, typically) world space is local space: edit the local values as given
    if (e.space == Space::Local || parentWorld.IsIdentity()) {
        NodeTransform& t = node.local;
        if (t.matrix && !e.rotationEuler && !e.scale) {
            if (e.translation) {
                t.matrix->m[0][3] = e.translation->x;
                t.matrix->m[1][3] = e.translation->y;
                t.matrix->m[2][3] = e.translation->z;
                t.trs.translation = *e.translation;
            }
            return;
        }
        if (e.translation) t.trs.translation = *e.translation;
        if (e.rotationEuler) {
            t.trs.rotation = QuatFromEuler(*e.rotationEuler);
            t.euler = *e.rotationEuler;
        }
        if (e.scale) t.trs.scale = *e.scale;
        t.matrix.reset();
        return;
    }

    const std::optional<Mat4> inverse = InverseAffine(parentWorld);
    if (!inverse) throw SceneError(NodeLabel(e.node) + ": a world-space edit is impossible because a parent's transform has zero scale");
    if (!e.rotationEuler && !e.scale) {
        // only the position: the local rotation and scale stay as they are
        if (e.translation) {
            const Vec3 t = TransformPoint(*inverse, *e.translation);
            node.local.trs.translation = t;
            if (node.local.matrix) {
                node.local.matrix->m[0][3] = t.x;
                node.local.matrix->m[1][3] = t.y;
                node.local.matrix->m[2][3] = t.z;
            }
        }
        return;
    }
    // the node's world transform with the supplied parts replaced, brought back into the parent's space
    TRS trs = Decompose(parentWorld * node.local.Matrix());
    if (e.translation) trs.translation = *e.translation;
    if (e.rotationEuler) trs.rotation = QuatFromEuler(*e.rotationEuler);
    if (e.scale) trs.scale = *e.scale;
    node.local = FromMatrix(*inverse * Compose(trs));
}

void Scene::Apply(const std::vector<TransformEdit>& edits) {
    for (const TransformEdit& e : edits) {
        if (e.node >= nodes_.size()) throw SceneError(NodeLabel(e.node) + " doesn't exist (the scene has " + std::to_string(nodes_.size()) + " nodes)");
        RequireFinite(e.translation, "translation", e.node);
        RequireFinite(e.rotationEuler, "rotationEuler", e.node);
        RequireFinite(e.scale, "scale", e.node);
    }
    std::vector<SceneNode> backup = nodes_;
    try {
        for (const TransformEdit& e : edits) {
            ApplyOne(e);
            const NodeTransform& t = nodes_[e.node].local;
            const Mat4 m = t.Matrix();
            for (const auto& row : m.m)
                for (const double v : row)
                    if (!std::isfinite(v)) throw SceneError(NodeLabel(e.node) + ": the edit gives a transform that is not finite");
        }
    } catch (...) {
        nodes_ = std::move(backup);
        throw;
    }
}

void Scene::SetLocalTransform(size_t i, const NodeTransform& transform) {
    Node(i);
    const Mat4 m = transform.Matrix();
    for (const auto& row : m.m)
        for (const double v : row)
            if (!std::isfinite(v)) throw SceneError(NodeLabel(i) + ": the transform is not finite");
    nodes_[i].local = transform;
}

void Scene::Reset(size_t i, bool subtree) {
    Node(i);
    nodes_[i].local = imported_[i];
    if (subtree)
        for (const int c : nodes_[i].children) Reset(static_cast<size_t>(c), true);
}

void Scene::ResetAll() {
    for (size_t i = 0; i < nodes_.size(); ++i) nodes_[i].local = imported_[i];
}

GeometryData Scene::WorldGeometry(std::vector<uint32_t>* meshNodes) const {
    GeometryData out;
    if (meshNodes) meshNodes->clear();
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const SceneNode& node = nodes_[i];
        if (node.mesh < 0 || !node.inScene) continue;
        const Mesh& source = meshes_[node.mesh];
        Mesh placed;
        placed.name = node.name.empty() ? source.name : node.name;
        placed.triangles = source.triangles;
        const Mat4 world = WorldMatrix(i);
        if (world.IsIdentity()) placed.vertices = source.vertices;  // (bit for bit as imported)
        else {
            placed.vertices.reserve(source.vertices.size());
            for (const Vec3& v : source.vertices) placed.vertices.push_back(TransformPoint(world, v));
        }
        out.meshes.push_back(std::move(placed));
        if (meshNodes) meshNodes->push_back(static_cast<uint32_t>(i));
    }
    return out;
}

VerificationReport VerifyScene(const Scene& scene) {
    std::vector<uint32_t> meshNodes;
    const GeometryData geometry = scene.WorldGeometry(&meshNodes);
    VerificationReport report = VerifyIntersections(geometry);
    for (Intersection& hit : report.intersections) {
        hit.a.meshIndex = meshNodes[hit.a.meshIndex];
        hit.b.meshIndex = meshNodes[hit.b.meshIndex];
    }
    return report;
}

}  // namespace mesh_verifier
