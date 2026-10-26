// mesh-verifier-web <file.obj> [-o page.html]
// Loads an OBJ, checks it and writes the web viewer page: the geometry with the intersecting triangles in red.
// Without -o the page goes next to the input, as <name>.html.
#include <filesystem>
#include <iostream>
#include <string>

#include "mesh_verifier/intersection_verifier.h"
#include "mesh_verifier/obj_importer.h"
#include "mesh_verifier/web_viewer.h"

using namespace mesh_verifier;

int main(int argc, char** argv) {
    std::filesystem::path input, output;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (input.empty()) input = a;
        else { std::cerr << "usage: mesh-verifier-web <file.obj> [-o page.html]\n"; return 2; }
    }
    if (input.empty()) { std::cerr << "usage: mesh-verifier-web <file.obj> [-o page.html]\n"; return 2; }
    if (output.empty()) output = std::filesystem::path(input).replace_extension(".html");
    try {
        const GeometryData geometry = LoadOBJ(input);
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
