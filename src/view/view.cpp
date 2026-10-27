#include "mesh_verifier/view.h"

#include <set>

#include "mesh_verifier/scene_io.h"

namespace mesh_verifier {
namespace {

using nlohmann::json;

constexpr std::array<double, 4> kRed{1.0, 0.23, 0.19, 1.0};
constexpr std::array<double, 4> kYellow{1.0, 0.88, 0.30, 1.0};

const char* KindName(OverlayKind k) { return k == OverlayKind::Lines ? "lines" : "triangles"; }

const char* RoleName(OverlayRole r) {
    switch (r) {
        case OverlayRole::Error: return "error";
        case OverlayRole::Warning: return "warning";
        case OverlayRole::Selection: return "selection";
        case OverlayRole::Info: break;
    }
    return "info";
}

}  // namespace

View BuildView(const Scene& scene, const VerificationReport& report, uint64_t revision) {
    View view;
    view.revision = revision;
    view.up = scene.Up();
    for (size_t i = 0; i < scene.NodeCount(); ++i) {
        const SceneNode& n = scene.Node(i);
        if (n.mesh >= 0 && n.inScene) view.parts.push_back({i, static_cast<size_t>(n.mesh), scene.WorldMatrix(i)});
    }

    Overlay intersecting{"intersections", "Intersecting triangles", OverlayKind::Triangles, OverlayRole::Error, kRed, {}, {}};
    std::set<TriangleRef> flagged;
    for (const Intersection& hit : report.intersections) {
        flagged.insert(hit.a);
        flagged.insert(hit.b);
    }
    for (const TriangleRef& t : flagged) {
        const Mesh& mesh = scene.Meshes()[static_cast<size_t>(scene.Node(t.meshIndex).mesh)];
        const Triangle& tri = mesh.triangles[t.triangleIndex];
        const Mat4 world = scene.WorldMatrix(t.meshIndex);
        intersecting.items.push_back(t);
        for (const uint32_t v : {tri.a, tri.b, tri.c}) intersecting.positions.push_back(TransformPoint(world, mesh.vertices[v]));
    }
    view.overlays.push_back(std::move(intersecting));

    Overlay crossings{"crossings", "Crossing lines", OverlayKind::Lines, OverlayRole::Warning, kYellow, {}, {}};
    for (const Intersection& hit : report.intersections) {
        if (hit.type != IntersectionType::Crossing) continue;  // (a coplanar overlap is an area, not a line)
        crossings.positions.push_back(hit.segmentStart);
        crossings.positions.push_back(hit.segmentEnd);
    }
    view.overlays.push_back(std::move(crossings));
    return view;
}

json MatrixJson(const Mat4& m) {
    json out = json::array();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) out.push_back(m.m[r][c]);
    return out;
}

json ViewJson(const View& view) {
    json parts = json::array();
    for (const ViewPart& p : view.parts) parts.push_back({{"node", p.node}, {"mesh", p.mesh}, {"world", MatrixJson(p.world)}});
    json overlays = json::array();
    for (const Overlay& o : view.overlays) {
        json items = json::array(), positions = json::array();
        for (const TriangleRef& t : o.items) items.push_back({{"node", t.meshIndex}, {"triangle", t.triangleIndex}});
        for (const Vec3& p : o.positions) {
            positions.push_back(p.x);
            positions.push_back(p.y);
            positions.push_back(p.z);
        }
        overlays.push_back({{"id", o.id},
                            {"label", o.label},
                            {"kind", KindName(o.kind)},
                            {"role", RoleName(o.role)},
                            {"color", o.color},
                            {"items", items},
                            {"positions", positions}});
    }
    return {{"revision", view.revision}, {"up", UpAxisName(view.up)}, {"parts", parts}, {"overlays", overlays}};
}

}  // namespace mesh_verifier
