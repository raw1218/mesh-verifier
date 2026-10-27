// Layer 1: geometry import: glTF 2.0 binary (GLB). Only the geometry and the node hierarchy are read; materials,
// textures, animations, skins and cameras are ignored.
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#include "mesh_verifier/scene.h"

namespace mesh_verifier {

// Thrown for a missing file or a malformed or unsupported glTF / GLB. The message says what's wrong.
class GltfImportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Reads a GLB file into a scene.
//  - Components are the glTF nodes, with the same indices (node i of the file is component i), names, hierarchy and
//    transforms (translation / rotation / scale, or a matrix, split into TRS when it has no shear).
//  - Each glTF mesh is one source mesh: the triangles of all its primitives, in order (modes TRIANGLES,
//    TRIANGLE_STRIP and TRIANGLE_FAN; point and line primitives have no triangles and are skipped). A node that uses
//    a mesh has that mesh's triangles, numbered from 0 in that order; meshes used by several nodes are shared.
//  - Positions may be float or (KHR_mesh_quantization) integer, normalized or not; sparse accessors are supported.
//  - The displayed scene is `scene` (or the first scene); nodes outside it are kept but not drawn or verified. A file
//    with no scenes shows every node.
//  - Buffers: the GLB's binary chunk, base64 `data:` URIs, or files next to the model.
//  - Compressed geometry (KHR_draco_mesh_compression, EXT_meshopt_compression) is not supported and is an error.
// A file that starts with '{' is read as a .gltf JSON document instead.
Scene LoadGLB(const std::filesystem::path& path);

// The same, from the file's bytes in memory. External buffer files are looked up in `baseDir` (none if empty).
Scene ParseGLB(const std::string& bytes, const std::string& name = "model", const std::filesystem::path& baseDir = {});

}  // namespace mesh_verifier
