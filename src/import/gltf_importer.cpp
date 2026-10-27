#include "mesh_verifier/gltf_importer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace mesh_verifier {
namespace {

using nlohmann::json;

// The member `key` of an object, or nullptr (also when `obj` isn't an object).
const json* Find(const json& obj, const std::string& key) {
    if (!obj.is_object()) return nullptr;
    const auto it = obj.find(key);
    return it == obj.end() ? nullptr : &*it;
}

constexpr uint32_t kMagic = 0x46546C67;      // "glTF"
constexpr uint32_t kChunkJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kChunkBin = 0x004E4942;   // "BIN\0"

[[noreturn]] void Fail(const std::string& what) { throw GltfImportError(what); }

uint32_t ReadU32(const std::string& bytes, size_t at) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(bytes[at + i]);
    return v;
}

const json& Member(const json& obj, const char* key, const std::string& where) {
    const json* v = Find(obj, key);
    if (!v) Fail(where + ": '" + key + "' is missing");
    return *v;
}

// A non-negative integer member (or `fallback` when it's absent and fallback >= 0).
size_t Index(const json& obj, const std::string& key, const std::string& where, int64_t fallback = -1) {
    const json* v = Find(obj, key);
    if (!v) {
        if (fallback < 0) Fail(where + ": '" + key + "' is missing");
        return static_cast<size_t>(fallback);
    }
    if (!v->is_number() || v->get<double>() < 0 || v->get<double>() != std::floor(v->get<double>()) || v->get<double>() > 9007199254740992.0)
        Fail(where + ": '" + key + "' must be a non-negative integer");
    return static_cast<size_t>(v->get<double>());
}

const json& ArrayMember(const json& root, const std::string& key) {
    static const json kEmpty = json::array();
    const json* v = Find(root, key);
    if (!v) return kEmpty;
    if (!v->is_array()) Fail("'" + key + "' must be an array");
    return v;
}

std::vector<double> Numbers(const json& v, size_t count, const std::string& where) {
    if (!v.is_array() || v.size() != count) Fail(where + " must be an array of " + std::to_string(count) + " numbers");
    std::vector<double> out;
    for (const json& x : v) {
        if (!x.is_number()) Fail(where + " must be an array of " + std::to_string(count) + " numbers");
        out.push_back(x.get<double>());
    }
    return out;
}

std::string DecodeBase64(const std::string& s, const std::string& where) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else Fail(where + ": malformed base64 data");
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

std::string PercentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}

class Reader {
public:
    Reader(const json& root, std::string bin, std::filesystem::path baseDir) : root_(root), bin_(std::move(bin)), baseDir_(std::move(baseDir)) {}

    Scene Build(const std::string& name) {
        CheckExtensions();
        LoadBuffers();

        const json& gltfMeshes = ArrayMember(root_, "meshes");
        std::vector<Mesh> meshes;
        for (size_t m = 0; m < gltfMeshes.size(); ++m) meshes.push_back(ReadMesh(gltfMeshes[m], m));

        const json& gltfNodes = ArrayMember(root_, "nodes");
        std::vector<SceneNode> nodes(gltfNodes.size());
        for (size_t i = 0; i < gltfNodes.size(); ++i) {
            const json& n = gltfNodes[i];
            const std::string where = "node " + std::to_string(i);
            if (!n.is_object()) Fail(where + " must be an object");
            SceneNode& node = nodes[i];
            const json* nm = Find(n, "name");
            node.name = nm && nm->is_string() && !nm->get_ref<const std::string&>().empty() ? nm->get_ref<const std::string&>() : "node " + std::to_string(i);
            if (Find(n, "mesh")) {
                node.mesh = static_cast<int>(Index(n, "mesh", where));
                if (node.mesh >= static_cast<int>(meshes.size())) Fail(where + ": mesh " + std::to_string(node.mesh) + " doesn't exist");
            }
            if (const json* children = Find(n, "children")) {
                if (!children->is_array()) Fail(where + ": 'children' must be an array");
                for (const json& c : *children) {
                    if (!c.is_number() || c.get<double>() < 0 || c.get<double>() != std::floor(c.get<double>()) || c.get<double>() >= static_cast<double>(gltfNodes.size()))
                        Fail(where + ": invalid child");
                    const int child = static_cast<int>(c.get<double>());
                    if (child == static_cast<int>(i)) Fail(where + " is its own child");
                    if (nodes[child].parent != -1) Fail("node " + std::to_string(child) + " has more than one parent");
                    nodes[child].parent = static_cast<int>(i);
                    node.children.push_back(child);
                }
            }
            node.local = ReadTransform(n, where);
        }
        MarkScene(nodes);
        try {
            return Scene(name, std::move(meshes), std::move(nodes));
        } catch (const SceneError& e) {
            Fail(e.what());
        }
    }

private:
    const json& root_;
    std::string bin_;
    std::filesystem::path baseDir_;
    std::vector<std::string> buffers_;

    void CheckExtensions() {
        for (const json& e : ArrayMember(root_, "extensionsRequired")) {
            if (!e.is_string()) continue;
            const std::string& ext = e.get_ref<const std::string&>();
            if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression" || ext == "KHR_meshopt_compression")
                Fail("the file needs the extension " + ext + ", which isn't supported (compressed geometry)");
        }
    }

    void LoadBuffers() {
        const json& buffers = ArrayMember(root_, "buffers");
        for (size_t b = 0; b < buffers.size(); ++b) {
            const std::string where = "buffer " + std::to_string(b);
            const json& buf = buffers[b];
            const size_t length = Index(buf, "byteLength", where);
            std::string data;
            const json* uri = Find(buf, "uri");
            if (!uri) {
                if (b != 0) Fail(where + " has no uri (only the first buffer may be the GLB's binary chunk)");
                data = bin_;
            } else {
                if (!uri->is_string()) Fail(where + ": 'uri' must be a string");
                const std::string& u = uri->get_ref<const std::string&>();
                if (u.rfind("data:", 0) == 0) {
                    const size_t comma = u.find(',');
                    if (comma == std::string::npos || u.substr(0, comma).find(";base64") == std::string::npos) Fail(where + ": only base64 data URIs are supported");
                    data = DecodeBase64(u.substr(comma + 1), where);
                } else {
                    if (u.find("://") != std::string::npos) Fail(where + ": remote buffers aren't supported");
                    if (baseDir_.empty()) Fail(where + " refers to the external file '" + u + "', which can't be read here");
                    std::ifstream file(baseDir_ / PercentDecode(u), std::ios::binary);
                    if (!file) Fail(where + ": cannot open '" + u + "'");
                    std::ostringstream s;
                    s << file.rdbuf();
                    data = s.str();
                }
            }
            if (data.size() < length) Fail(where + " is shorter than its byteLength");
            data.resize(length);
            buffers_.push_back(std::move(data));
        }
    }

    static size_t ComponentSize(size_t type) {
        switch (type) {
            case 5120: case 5121: return 1;
            case 5122: case 5123: return 2;
            case 5125: case 5126: return 4;
            default: return 0;
        }
    }

    static double ReadComponent(const char* p, size_t type, bool normalized) {
        switch (type) {
            case 5120: { int8_t v; std::memcpy(&v, p, 1); return normalized ? std::max(v / 127.0, -1.0) : v; }
            case 5121: { uint8_t v; std::memcpy(&v, p, 1); return normalized ? v / 255.0 : v; }
            case 5122: { int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(v / 32767.0, -1.0) : v; }
            case 5123: { uint16_t v; std::memcpy(&v, p, 2); return normalized ? v / 65535.0 : v; }
            case 5125: { uint32_t v; std::memcpy(&v, p, 4); return normalized ? v / 4294967295.0 : v; }
            default: { float v; std::memcpy(&v, p, 4); return v; }
        }
    }

    // Reads `count` elements of `components` values, each of `type`, from a buffer view; `stride` 0 means packed.
    void ReadView(size_t viewIndex, size_t offset, size_t count, size_t components, size_t type, bool normalized, bool allowStride, double* out, const std::string& where) {
        const json& views = ArrayMember(root_, "bufferViews");
        if (viewIndex >= views.size()) Fail(where + ": buffer view " + std::to_string(viewIndex) + " doesn't exist");
        const json& view = views[viewIndex];
        const std::string vw = "buffer view " + std::to_string(viewIndex);
        const size_t buffer = Index(view, "buffer", vw);
        if (buffer >= buffers_.size()) Fail(vw + ": buffer " + std::to_string(buffer) + " doesn't exist");
        const size_t viewOffset = Index(view, "byteOffset", vw, 0);
        const size_t viewLength = Index(view, "byteLength", vw);
        const size_t elementSize = components * ComponentSize(type);
        size_t stride = allowStride ? Index(view, "byteStride", vw, 0) : 0;
        if (stride == 0) stride = elementSize;
        if (stride < elementSize) Fail(vw + ": byteStride is smaller than an element");
        const std::string& data = buffers_[buffer];
        if (viewOffset > data.size() || viewLength > data.size() - viewOffset) Fail(vw + " goes past the end of its buffer");
        if (count == 0) return;
        const size_t needed = offset + stride * (count - 1) + elementSize;
        if (offset > viewLength || needed > viewLength || needed < offset) Fail(where + " goes past the end of its buffer view");
        const char* base = data.data() + viewOffset + offset;
        const size_t size = ComponentSize(type);
        for (size_t i = 0; i < count; ++i)
            for (size_t c = 0; c < components; ++c) out[i * components + c] = ReadComponent(base + i * stride + c * size, type, normalized);
    }

    // An accessor's values, flat (count * components).
    std::vector<double> ReadAccessor(size_t index, size_t components, bool isIndices) {
        const json& accessors = ArrayMember(root_, "accessors");
        const std::string where = "accessor " + std::to_string(index);
        if (index >= accessors.size()) Fail(where + " doesn't exist");
        const json& a = accessors[index];
        const size_t count = Index(a, "count", where);
        const size_t type = Index(a, "componentType", where);
        if (ComponentSize(type) == 0) Fail(where + ": unknown componentType " + std::to_string(type));
        const json& typeName = Member(a, "type", where);
        const char* expected = components == 3 ? "VEC3" : "SCALAR";
        if (!typeName.is_string() || typeName.get_ref<const std::string&>() != expected) Fail(where + " must be of type " + expected);
        if (isIndices && type != 5121 && type != 5123 && type != 5125) Fail(where + ": indices must be unsigned integers");
        const json* norm = Find(a, "normalized");
        const bool normalized = norm && norm->is_boolean() && norm->get<bool>();
        if (count > (std::numeric_limits<size_t>::max() / 8) / components) Fail(where + " is too large");
        std::vector<double> out(count * components, 0.0);
        if (Find(a, "bufferView"))
            ReadView(Index(a, "bufferView", where), Index(a, "byteOffset", where, 0), count, components, type, normalized, true, out.data(), where);
        if (const json* sparse = Find(a, "sparse")) {
            const std::string sw = where + " (sparse)";
            const size_t n = Index(*sparse, "count", sw);
            const json& idx = Member(*sparse, "indices", sw);
            const json& vals = Member(*sparse, "values", sw);
            const size_t idxType = Index(idx, "componentType", sw);
            if (idxType != 5121 && idxType != 5123 && idxType != 5125) Fail(sw + ": indices must be unsigned integers");
            std::vector<double> where_(n), values(n * components);
            ReadView(Index(idx, "bufferView", sw), Index(idx, "byteOffset", sw, 0), n, 1, idxType, false, false, where_.data(), sw);
            ReadView(Index(vals, "bufferView", sw), Index(vals, "byteOffset", sw, 0), n, components, type, normalized, false, values.data(), sw);
            for (size_t k = 0; k < n; ++k) {
                const auto at = static_cast<size_t>(where_[k]);
                if (at >= count) Fail(sw + ": index out of range");
                for (size_t c = 0; c < components; ++c) out[at * components + c] = values[k * components + c];
            }
        }
        return out;
    }

    Mesh ReadMesh(const json& m, size_t index) {
        const std::string where = "mesh " + std::to_string(index);
        if (!m.is_object()) Fail(where + " must be an object");
        Mesh mesh;
        const json* nm = Find(m, "name");
        mesh.name = nm && nm->is_string() && !nm->get_ref<const std::string&>().empty() ? nm->get_ref<const std::string&>() : where;
        const json& primitives = Member(m, "primitives", where);
        if (!primitives.is_array()) Fail(where + ": 'primitives' must be an array");
        for (size_t p = 0; p < primitives.size(); ++p) {
            const json& prim = primitives[p];
            const std::string pw = where + " primitive " + std::to_string(p);
            const size_t mode = Index(prim, "mode", pw, 4);
            if (mode > 6) Fail(pw + ": unknown mode " + std::to_string(mode));
            if (mode < 4) continue;  // points and lines: no triangles
            const json& attributes = Member(prim, "attributes", pw);
            if (!Find(attributes, "POSITION")) continue;
            const std::vector<double> pos = ReadAccessor(Index(attributes, "POSITION", pw), 3, false);
            const size_t vertexCount = pos.size() / 3;
            const auto base = static_cast<uint64_t>(mesh.vertices.size());
            if (base + vertexCount > std::numeric_limits<uint32_t>::max()) Fail(pw + ": too many vertices");
            for (size_t v = 0; v < vertexCount; ++v) {
                const Vec3 p3{pos[3 * v], pos[3 * v + 1], pos[3 * v + 2]};
                if (!IsFinite(p3)) Fail(pw + ": vertex " + std::to_string(v) + " has a coordinate that is not finite");
                mesh.vertices.push_back(p3);
            }
            std::vector<uint32_t> order;
            if (Find(prim, "indices")) {
                const std::vector<double> idx = ReadAccessor(Index(prim, "indices", pw), 1, true);
                order.reserve(idx.size());
                for (const double i : idx) {
                    if (i >= static_cast<double>(vertexCount)) Fail(pw + ": index " + std::to_string(static_cast<uint64_t>(i)) + " but the primitive has only " + std::to_string(vertexCount) + " vertices");
                    order.push_back(static_cast<uint32_t>(i));
                }
            } else {
                for (size_t v = 0; v < vertexCount; ++v) order.push_back(static_cast<uint32_t>(v));
            }
            const auto b = static_cast<uint32_t>(base);
            if (mode == 4) {
                for (size_t t = 0; t + 2 < order.size(); t += 3) mesh.triangles.push_back({b + order[t], b + order[t + 1], b + order[t + 2]});
            } else if (mode == 5) {
                for (size_t t = 0; t + 2 < order.size(); ++t) {
                    if (t % 2 == 0) mesh.triangles.push_back({b + order[t], b + order[t + 1], b + order[t + 2]});
                    else mesh.triangles.push_back({b + order[t], b + order[t + 2], b + order[t + 1]});
                }
            } else {
                for (size_t t = 1; t + 1 < order.size(); ++t) mesh.triangles.push_back({b + order[t], b + order[t + 1], b + order[0]});
            }
        }
        return mesh;
    }

    static NodeTransform ReadTransform(const json& n, const std::string& where) {
        NodeTransform t;
        if (const json* matrix = Find(n, "matrix")) {
            const std::vector<double> v = Numbers(*matrix, 16, where + ": 'matrix'");
            const Mat4 m = Mat4::FromColumnMajor(v.data());
            for (const double x : v)
                if (!std::isfinite(x)) Fail(where + ": 'matrix' is not finite");
            if (m.m[3][0] != 0 || m.m[3][1] != 0 || m.m[3][2] != 0 || m.m[3][3] != 1) Fail(where + ": 'matrix' is not affine");
            if (!m.IsIdentity()) {
                bool exact = false;
                t.trs = Decompose(m, &exact);
                if (!exact) t.matrix = m;
            }
            return t;
        }
        if (const json* tr = Find(n, "translation")) {
            const auto v = Numbers(*tr, 3, where + ": 'translation'");
            t.trs.translation = {v[0], v[1], v[2]};
        }
        if (const json* r = Find(n, "rotation")) {
            const auto v = Numbers(*r, 4, where + ": 'rotation'");
            const Quat q{v[0], v[1], v[2], v[3]};
            const double len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            if (!(len > 0) || !std::isfinite(len)) Fail(where + ": 'rotation' is not a rotation");
            t.trs.rotation = Normalized(q);
        }
        if (const json* s = Find(n, "scale")) {
            const auto v = Numbers(*s, 3, where + ": 'scale'");
            t.trs.scale = {v[0], v[1], v[2]};
        }
        if (!IsFinite(t.trs.translation) || !IsFinite(t.trs.scale)) Fail(where + ": the transform is not finite");
        return t;
    }

    void MarkScene(std::vector<SceneNode>& nodes) const {
        const json& scenes = ArrayMember(root_, "scenes");
        if (scenes.empty()) return;  // every node is shown
        const size_t active = Index(root_, "scene", "the file", 0);
        if (active >= scenes.size()) Fail("scene " + std::to_string(active) + " doesn't exist");
        for (SceneNode& n : nodes) n.inScene = false;
        std::vector<int> stack;
        if (const json* list = Find(scenes[active], "nodes")) {
            if (!list->is_array()) Fail("scene " + std::to_string(active) + ": 'nodes' must be an array");
            for (const json& v : *list) {
                if (!v.is_number() || v.get<double>() < 0 || v.get<double>() != std::floor(v.get<double>()) || v.get<double>() >= static_cast<double>(nodes.size()))
                    Fail("scene " + std::to_string(active) + ": invalid node");
                stack.push_back(static_cast<int>(v.get<double>()));
            }
        }
        size_t visited = 0;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            if (nodes[i].inScene) continue;
            if (++visited > nodes.size()) break;
            nodes[i].inScene = true;
            for (const int c : nodes[i].children) stack.push_back(c);
        }
    }
};

}  // namespace

Scene ParseGLB(const std::string& bytes, const std::string& name, const std::filesystem::path& baseDir) {
    std::string jsonText, bin;
    size_t first = 0;
    while (first < bytes.size() && (bytes[first] == ' ' || bytes[first] == '\n' || bytes[first] == '\r' || bytes[first] == '\t')) ++first;
    if (bytes.size() >= 4 && ReadU32(bytes, 0) == kMagic) {
        if (bytes.size() < 12) Fail("truncated GLB header");
        const uint32_t version = ReadU32(bytes, 4);
        if (version != 2) Fail("unsupported glTF version " + std::to_string(version) + " (only 2 is supported)");
        const uint32_t length = ReadU32(bytes, 8);
        if (length > bytes.size() || length < 12) Fail("the GLB header's length doesn't match the file");
        size_t at = 12;
        bool haveJson = false, haveBin = false;
        while (at + 8 <= length) {
            const uint32_t chunkLength = ReadU32(bytes, at);
            const uint32_t chunkType = ReadU32(bytes, at + 4);
            at += 8;
            if (chunkLength > length - at) Fail("a GLB chunk goes past the end of the file");
            if (!haveJson) {
                if (chunkType != kChunkJson) Fail("the GLB's first chunk isn't JSON");
                jsonText = bytes.substr(at, chunkLength);
                haveJson = true;
            } else if (chunkType == kChunkBin && !haveBin) {
                bin = bytes.substr(at, chunkLength);
                haveBin = true;
            }
            at += chunkLength;
            at = (at + 3) & ~static_cast<size_t>(3);
        }
        if (!haveJson) Fail("the GLB has no JSON chunk");
    } else if (first < bytes.size() && bytes[first] == '{') {
        jsonText = bytes;
    } else {
        Fail("not a GLB file (bad magic number)");
    }
    json root;
    try {
        root = json::parse(jsonText);
    } catch (const json::exception& e) {
        Fail(std::string("malformed glTF JSON: ") + e.what());
    }
    if (!root.is_object()) Fail("the glTF JSON isn't an object");
    if (const json* asset = Find(root, "asset"); asset && asset->is_object())
        if (const json* version = Find(*asset, "version"); version && version->is_string() && version->get_ref<const std::string&>().rfind("2", 0) != 0)
            Fail("unsupported glTF version " + version->get_ref<const std::string&>() + " (only 2.x is supported)");
    return Reader(root, std::move(bin), baseDir).Build(name);
}

Scene LoadGLB(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw GltfImportError("cannot open '" + path.string() + "'");
    std::ostringstream bytes;
    bytes << file.rdbuf();
    try {
        return ParseGLB(bytes.str(), path.stem().string(), path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path());
    } catch (const GltfImportError& e) {
        throw GltfImportError(path.string() + ": " + e.what());
    }
}

}  // namespace mesh_verifier
