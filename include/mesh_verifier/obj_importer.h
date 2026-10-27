// Layer 1: geometry import: Wavefront OBJ.
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#include "mesh_verifier/geometry.h"
#include "mesh_verifier/scene.h"

namespace mesh_verifier {

// Thrown for a missing file, binary (non-text) content, a malformed line, a bad index or a non-triangular face. The message names the line.
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

// The same file as a scene of transformable components, all roots with identity transforms. Each `o` object that
// has faces is one component (faces before the first `o` form one more, first), holding the vertices its faces
// use, in file order. A file with at most one such object is a single component: exactly the mesh LoadOBJ reads.
Scene LoadOBJScene(const std::filesystem::path& path);
Scene ParseOBJScene(const std::string& text, const std::string& name = "mesh");

}  // namespace mesh_verifier
