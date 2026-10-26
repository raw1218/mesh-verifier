// mesh-verify <file.obj> [--json]
// Loads an OBJ, checks it for intersecting triangles and prints the report (sorted, so the output for a file is
// always the same). Exit code: 0 no intersections, 1 intersections found, 2 an error.
#include <cstdio>
#include <iostream>
#include <string>

#include "mesh_verifier/intersection_verifier.h"
#include "mesh_verifier/obj_importer.h"

using namespace mesh_verifier;

namespace {

const char* TypeName(IntersectionType t) { return t == IntersectionType::Crossing ? "crossing" : "coplanar_overlap"; }

std::string Num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") json = true;
        else if (path.empty()) path = a;
        else { std::cerr << "usage: mesh-verify <file.obj> [--json]\n"; return 2; }
    }
    if (path.empty()) { std::cerr << "usage: mesh-verify <file.obj> [--json]\n"; return 2; }
    try {
        const GeometryData geometry = LoadOBJ(path);
        const VerificationReport report = VerifyIntersections(geometry);
        if (json) {
            std::cout << "{\"intersections\":[";
            for (size_t i = 0; i < report.intersections.size(); ++i) {
                const Intersection& h = report.intersections[i];
                std::cout << (i ? "," : "") << "\n  {\"a\":{\"mesh\":" << h.a.meshIndex << ",\"triangle\":" << h.a.triangleIndex << "},\"b\":{\"mesh\":" << h.b.meshIndex
                          << ",\"triangle\":" << h.b.triangleIndex << "},\"type\":\"" << TypeName(h.type) << "\",\"weight\":" << Num(h.weight) << "}";
            }
            std::cout << (report.intersections.empty() ? "" : "\n") << "]}\n";
        } else {
            for (const Intersection& h : report.intersections)
                std::cout << "mesh " << h.a.meshIndex << " triangle " << h.a.triangleIndex << "  x  mesh " << h.b.meshIndex << " triangle " << h.b.triangleIndex << "  "
                          << TypeName(h.type) << "  weight " << Num(h.weight) << "\n";
            std::cout << report.intersections.size() << " intersecting pair" << (report.intersections.size() == 1 ? "" : "s") << "\n";
        }
        return report.intersections.empty() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
