// Layer 1: geometry import. Wavefront OBJ only.
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#include "mesh_verifier/geometry.h"

namespace mesh_verifier {

// Thrown for a missing file, a malformed line, a bad index or a non-triangular face. The message names the line.
class ObjImportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Reads an OBJ file into one mesh (named after the file's stem).
//  - `v x y z [w]`: a vertex position (w is ignored).
//  - `f i j k`: a triangle. Each index may be written `i`, `i/t`, `i//n` or `i/t/n`; only the position index is used.
//    Indices are 1-based; negative indices count back from the last vertex read so far.
//  - Faces with other than three vertices are an error.
//  - Every other line (o, g, vt, vn, usemtl, mtllib, s, comments, ...) is ignored.
// Positions are kept exactly as written: nothing is transformed, welded or reordered. Zero-area triangles and
// repeated indices are kept as they are. An empty file gives one empty mesh.
GeometryData LoadOBJ(const std::filesystem::path& path);

// The same, from OBJ text in memory (`name` becomes the mesh's name).
GeometryData ParseOBJ(const std::string& text, const std::string& name = "mesh");

}  // namespace mesh_verifier
