// mesh-verifier-web <model> [--format obj|glb] [--transforms file] [-o page.html]
// Loads a model (the format defaults to the file's extension), applies the optional component edits, checks it and
// writes a static web page: the geometry with the intersecting triangles in red. Without -o the page goes next to
// the input, as <name>.html. For the interactive GUI use mesh-verify.
#include <filesystem>
#include <iostream>
#include <string>

#include "mesh_verifier/intersection_verifier.h"
#include "mesh_verifier/scene_io.h"
#include "mesh_verifier/web_viewer.h"

using namespace mesh_verifier;

int main(int argc, char** argv) {
    constexpr const char* kUsage = "usage: mesh-verifier-web <model> [--format obj|glb] [--transforms file] [-o page.html]\n";
    std::filesystem::path input, output, transforms;
    std::string format;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "--format" && i + 1 < argc) format = argv[++i];
        else if (a == "--transforms" && i + 1 < argc) transforms = argv[++i];
        else if (input.empty() && (a.empty() || a[0] != '-')) input = a;
        else { std::cerr << kUsage; return 2; }
    }
    if (input.empty()) { std::cerr << kUsage; return 2; }
    if (format.empty()) format = input.extension().string().substr(input.extension().empty() ? 0 : 1);
    const auto modelFormat = ParseModelFormat(format);
    if (!modelFormat) { std::cerr << "error: unknown format '" << format << "' (use --format obj or glb)\n"; return 2; }
    if (output.empty()) output = std::filesystem::path(input).replace_extension(".html");
    try {
        Scene scene = LoadScene(input, *modelFormat);
        if (!transforms.empty()) scene.Apply(LoadTransformEdits(transforms));
        const GeometryData geometry = scene.WorldGeometry();
        const VerificationReport report = VerifyIntersections(geometry);
        WebViewer viewer;
        viewer.title = input.filename().string() + " - Mesh Verifier";
        viewer.Write(output, geometry, report);
        std::cout << output.string() << ": " << report.intersections.size() << " intersecting pair" << (report.intersections.size() == 1 ? "" : "s") << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
