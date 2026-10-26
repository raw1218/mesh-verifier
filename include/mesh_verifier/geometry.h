// Layer 2: geometry representation. Plain data, no dependencies on the importer, the verifier or the viewer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mesh_verifier {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

inline bool operator==(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// Zero-based indices into the mesh's own vertex array.
struct Triangle {
    uint32_t a = 0, b = 0, c = 0;
};

inline bool operator==(const Triangle& p, const Triangle& q) { return p.a == q.a && p.b == q.b && p.c == q.c; }

struct Mesh {
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
};

// One or more meshes, all in one shared coordinate space.
struct GeometryData {
    std::vector<Mesh> meshes;
};

}  // namespace mesh_verifier
