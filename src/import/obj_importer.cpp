#include "mesh_verifier/obj_importer.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

namespace mesh_verifier {
namespace {

std::string_view Trim(std::string_view s) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> Split(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

[[noreturn]] void Fail(const std::string& name, size_t line, const std::string& what) {
    std::ostringstream m;
    m << name << ":" << line << ": " << what;
    throw ObjImportError(m.str());
}

double ParseDouble(std::string_view token, const std::string& name, size_t line) {
    double value = 0.0;
    const char* first = token.data();
    const char* last = token.data() + token.size();
    if (!token.empty() && token.front() == '+') ++first;  // from_chars doesn't take a leading '+'
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc() || ptr != last) Fail(name, line, "malformed vertex coordinate '" + std::string(token) + "'");
    return value;
}

// One face corner: the position index before any '/', 1-based or negative (relative), resolved to 0-based.
int64_t ParseIndex(std::string_view token, int64_t vertexCount, const std::string& name, size_t line) {
    const std::string_view position = token.substr(0, token.find('/'));
    int64_t index = 0;
    const auto [ptr, ec] = std::from_chars(position.data(), position.data() + position.size(), index);
    if (position.empty() || ec != std::errc() || ptr != position.data() + position.size())
        Fail(name, line, "malformed face index '" + std::string(token) + "'");
    if (index == 0) Fail(name, line, "face index 0 (OBJ indices start at 1)");
    if (index < 0) {
        const int64_t resolved = vertexCount + index;
        if (resolved < 0) Fail(name, line, "face index " + std::to_string(index) + " is before the first vertex");
        return resolved;
    }
    return index - 1;
}

}  // namespace

GeometryData ParseOBJ(const std::string& text, const std::string& name) {
    Mesh mesh;
    mesh.name = name;
    // positive indices may refer to vertices written later in the file: checked once everything is read
    std::vector<size_t> faceLines;
    std::istringstream in(text);
    std::string raw;
    size_t line = 0;
    while (std::getline(in, raw)) {
        ++line;
        std::string_view s = Trim(raw);
        if (const size_t hash = s.find('#'); hash != std::string_view::npos) s = Trim(s.substr(0, hash));
        if (s.empty()) continue;
        const std::vector<std::string_view> tokens = Split(s);
        if (tokens[0] == "v") {
            if (tokens.size() < 4 || tokens.size() > 5) Fail(name, line, "a vertex needs 3 coordinates (and an optional w)");
            mesh.vertices.push_back({ParseDouble(tokens[1], name, line), ParseDouble(tokens[2], name, line), ParseDouble(tokens[3], name, line)});
        } else if (tokens[0] == "f") {
            if (tokens.size() != 4)
                Fail(name, line, "only triangles are supported: this face has " + std::to_string(tokens.size() - 1) + " vertices");
            const auto count = static_cast<int64_t>(mesh.vertices.size());
            int64_t idx[3];
            for (int k = 0; k < 3; ++k) {
                idx[k] = ParseIndex(tokens[k + 1], count, name, line);
                if (idx[k] > std::numeric_limits<uint32_t>::max()) Fail(name, line, "face index too large");
            }
            mesh.triangles.push_back({static_cast<uint32_t>(idx[0]), static_cast<uint32_t>(idx[1]), static_cast<uint32_t>(idx[2])});
            faceLines.push_back(line);
        }
        // everything else is ignored
    }
    for (size_t t = 0; t < mesh.triangles.size(); ++t) {
        const Triangle& tri = mesh.triangles[t];
        for (const uint32_t i : {tri.a, tri.b, tri.c})
            if (i >= mesh.vertices.size())
                Fail(name, faceLines[t], "face index " + std::to_string(i + 1) + " but the file has only " + std::to_string(mesh.vertices.size()) + " vertices");
    }
    GeometryData geometry;
    geometry.meshes.push_back(std::move(mesh));
    return geometry;
}

GeometryData LoadOBJ(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw ObjImportError("cannot open '" + path.string() + "'");
    std::ostringstream text;
    text << file.rdbuf();
    return ParseOBJ(text.str(), path.stem().string());
}

}  // namespace mesh_verifier
