// A scene: source meshes placed by a hierarchy of transformable components (nodes).
//
// The source geometry (the meshes' vertices and triangles) is never modified. Each node has a local transform
// relative to its parent; its world transform is parentWorld * local, so editing a node moves its whole subtree.
// Edits replace parts of a node's transform in its local space or in world space; the transforms the scene was
// imported with are kept, so any node (or all of them) can be reset.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "mesh_verifier/geometry.h"
#include "mesh_verifier/transform.h"
#include "mesh_verifier/verification_report.h"

namespace mesh_verifier {

// Thrown for an invalid hierarchy or an invalid edit. A failed edit changes nothing.
class SceneError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A node's local transform: translation, rotation and scale; or, when a transform isn't expressible as TRS (it has
// shear: possible when imported, or from a world-space edit under a non-uniformly scaled parent), a general
// affine matrix, with `trs` its nearest TRS for display.
struct NodeTransform {
    TRS trs;
    std::optional<Mat4> matrix;
    // The Euler angles (degrees) the rotation was last set from, if it was; reported back as written.
    std::optional<Vec3> euler;

    Mat4 Matrix() const { return matrix ? *matrix : Compose(trs); }
    Vec3 EulerDegrees() const { return euler ? *euler : EulerFromQuat(trs.rotation); }
};

struct SceneNode {
    std::string name;
    int parent = -1;            // -1 for a root
    std::vector<int> children;  // in order
    int mesh = -1;              // index into Scene::Meshes(), or -1 for a node with no geometry
    bool inScene = true;        // false for a node outside the displayed scene (not drawn, not verified)
    NodeTransform local;
};

enum class Space { Local, World };

// Which axis is up. The geometry is never rotated for it; renderers orient their camera by it (glTF is Y-up;
// Blender is Z-up).
enum class UpAxis { Y, Z };

// One edit of one node. Each supplied field replaces that part of the node's transform in `space`; omitted
// fields keep their current value (in that space).
struct TransformEdit {
    size_t node = 0;
    Space space = Space::Local;
    std::optional<Vec3> translation;
    std::optional<Vec3> rotationEuler;  // degrees, X, Y, Z order about fixed axes (see transform.h)
    std::optional<Vec3> scale;
};

class Scene {
public:
    Scene() = default;
    // `nodes` give parents and children consistently (each node at most one parent, no cycles); `mesh` indices must
    // be valid. The nodes' current transforms become the imported transforms.
    Scene(std::string name, std::vector<Mesh> meshes, std::vector<SceneNode> nodes);

    const std::string& Name() const { return name_; }
    UpAxis Up() const { return up_; }
    void SetUp(UpAxis up) { up_ = up; }
    const std::vector<Mesh>& Meshes() const { return meshes_; }
    const std::vector<SceneNode>& Nodes() const { return nodes_; }
    size_t NodeCount() const { return nodes_.size(); }
    const SceneNode& Node(size_t i) const;
    std::vector<int> Roots() const;

    const NodeTransform& LocalTransform(size_t i) const { return Node(i).local; }
    const NodeTransform& ImportedTransform(size_t i) const;
    Mat4 LocalMatrix(size_t i) const { return Node(i).local.Matrix(); }
    Mat4 WorldMatrix(size_t i) const;
    // The world transform split into TRS (exact unless something above or at the node has shear).
    TRS WorldTRS(size_t i) const { return Decompose(WorldMatrix(i)); }
    bool IsModified(size_t i) const;

    // Applies the edits in order, all or nothing: if any is invalid (unknown node, a non-finite value, a world edit
    // under a parent that collapses space), none is applied and SceneError is thrown.
    void Apply(const std::vector<TransformEdit>& edits);
    void Apply(const TransformEdit& edit) { Apply(std::vector<TransformEdit>{edit}); }
    // Replaces a node's whole local transform.
    void SetLocalTransform(size_t i, const NodeTransform& transform);

    // Back to the imported transform: one node, optionally with its whole subtree, or every node.
    void Reset(size_t i, bool subtree = false);
    void ResetAll();

    // Every in-scene node with a mesh, its vertices placed in world space: one mesh per node, in node order.
    // `meshNodes`, if given, receives each output mesh's node index.
    GeometryData WorldGeometry(std::vector<uint32_t>* meshNodes = nullptr) const;

private:
    void ApplyOne(const TransformEdit& edit);

    std::string name_;
    UpAxis up_ = UpAxis::Y;
    std::vector<Mesh> meshes_;
    std::vector<SceneNode> nodes_;
    std::vector<NodeTransform> imported_;
};

// Verifies the scene as placed: VerifyIntersections on WorldGeometry(), with each TriangleRef's meshIndex being the
// node index (and triangleIndex the triangle within that node's mesh).
VerificationReport VerifyScene(const Scene& scene);

}  // namespace mesh_verifier
