// Layer 4: visualization, as a web page. Separate from the verifier: it only draws what it's given.
#pragma once

#include <filesystem>
#include <string>

#include "mesh_verifier/geometry.h"
#include "mesh_verifier/verification_report.h"

namespace mesh_verifier {

// A renderer for a geometry and its verification report: a self-contained HTML page (three.js, loaded from a
// CDN) that shows every mesh, the triangles the report flags in red and everything else in a neutral colour,
// the mesh's edges as a wireframe, and the reported intersection segments. Orbit with the left mouse button, pan
// with the right, zoom with the wheel. It never re-checks the geometry: what's red is exactly what the report
// says. Its parts are drawn as separate layers (base geometry, wireframe, intersection highlight, intersection
// segments), so more overlays can be added later.
struct WebViewer {
    std::string title = "Mesh Verifier";

    // The whole page.
    std::string Render(const GeometryData& geometry, const VerificationReport& report) const;

    // Render() written to a file.
    void Write(const std::filesystem::path& path, const GeometryData& geometry, const VerificationReport& report) const;
};

}  // namespace mesh_verifier
