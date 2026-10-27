// What a renderer draws: where each component is, and highlight layers (overlays). Which triangles are highlighted,
// in which colour, is decided here, not in any renderer: a renderer (the web page, the Blender add-on, ...) draws
// the overlays as given, as coloured triangles and lines in world space. The JSON form is schema/view.schema.json.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mesh_verifier/scene.h"
#include "mesh_verifier/verification_report.h"

namespace mesh_verifier {

enum class OverlayKind { Triangles, Lines };

// What an overlay means; a renderer may restyle by role (e.g. to suit its theme), and otherwise uses `color`.
enum class OverlayRole { Error, Warning, Info, Selection };

struct Overlay {
    std::string id;     // a stable name, e.g. "intersections"
    std::string label;  // for a renderer's list of layers
    OverlayKind kind = OverlayKind::Triangles;
    OverlayRole role = OverlayRole::Info;
    std::array<double, 4> color{1, 1, 1, 1};  // RGBA, 0 to 1
    // Triangles only: the triangles drawn, in the order of `positions` (for picking and selection).
    std::vector<TriangleRef> items;
    // World space: 3 points per triangle, or 2 per line segment.
    std::vector<Vec3> positions;
};

struct ViewPart {
    size_t node = 0;
    size_t mesh = 0;
    Mat4 world;
};

struct View {
    uint64_t revision = 0;
    UpAxis up = UpAxis::Y;
    std::vector<ViewPart> parts;  // every displayed component with geometry
    std::vector<Overlay> overlays;
};

// The view of a scene and its verification (a report from VerifyScene):
//  - "intersections": every triangle in an intersecting pair, red (role error);
//  - "crossings": the segments along which crossing triangles meet, yellow (role warning).
View BuildView(const Scene& scene, const VerificationReport& report, uint64_t revision = 0);

nlohmann::json ViewJson(const View& view);
// 16 numbers, column-major (as glTF writes them).
nlohmann::json MatrixJson(const Mat4& m);

}  // namespace mesh_verifier
