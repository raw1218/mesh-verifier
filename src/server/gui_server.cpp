#include "mesh_verifier/gui_server.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <httplib.h>

#include "mesh_verifier/scene_io.h"
#include "mesh_verifier/schema.h"
#include "mesh_verifier/view.h"
#include "util/embedded.h"

#ifndef MESH_VERIFIER_THREE_DIR
#define MESH_VERIFIER_THREE_DIR ""
#endif

namespace mesh_verifier {
namespace {

using nlohmann::json;

constexpr size_t kMaxBody = size_t{1} << 30;
constexpr const char* kThreeCdn = "https://cdn.jsdelivr.net/npm/three@0.174.0/";

json Vec(const Vec3& v) { return json::array({v.x, v.y, v.z}); }

json TransformJson(const NodeTransform& t) {
    return {{"translation", Vec(t.trs.translation)},
            {"rotationEuler", Vec(t.EulerDegrees())},
            {"scale", Vec(t.trs.scale)},
            {"shear", t.matrix.has_value()},
            {"matrix", MatrixJson(t.Matrix())}};
}

json WorldJson(const Mat4& world) {
    bool exact = false;
    const TRS trs = Decompose(world, &exact);
    return {{"translation", Vec(trs.translation)},
            {"rotationEuler", Vec(EulerFromQuat(trs.rotation))},
            {"scale", Vec(trs.scale)},
            {"shear", !exact},
            {"matrix", MatrixJson(world)}};
}

// (Names come from model files: invalid UTF-8 in them is replaced, not an error.)
HttpResponse Json(const json& v, int status = 200) { return {status, "application/json", v.dump(-1, ' ', false, json::error_handler_t::replace)}; }
HttpResponse Error(const std::string& message, int status = 400) { return Json({{"error", message}}, status); }

std::string UrlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}

std::map<std::string, std::string> ParseQuery(const std::string& q) {
    std::map<std::string, std::string> out;
    size_t at = 0;
    while (at <= q.size()) {
        size_t amp = q.find('&', at);
        if (amp == std::string::npos) amp = q.size();
        const std::string part = q.substr(at, amp - at);
        if (!part.empty()) {
            const size_t eq = part.find('=');
            out[UrlDecode(part.substr(0, eq))] = eq == std::string::npos ? "" : UrlDecode(part.substr(eq + 1));
        }
        at = amp + 1;
    }
    return out;
}

// A request body: JSON matching `schema` (an empty body is {}).
json ReadBody(const std::string& body, Schema schema) {
    json request;
    try {
        request = body.empty() ? json::object() : json::parse(body);
    } catch (const json::parse_error& e) {
        throw std::invalid_argument(std::string("malformed JSON: ") + e.what());
    }
    Validate(request, schema);
    return request;
}

bool IsThreeDir(const std::filesystem::path& dir) {
    std::error_code ec;
    return !dir.empty() && std::filesystem::exists(dir / "build" / "three.module.js", ec) &&
           std::filesystem::exists(dir / "examples" / "jsm" / "controls" / "TransformControls.js", ec);
}

std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}

std::string HtmlEscape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

}  // namespace

std::filesystem::path FindThreeJs() {
    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("MESH_VERIFIER_THREE_DIR")) candidates.emplace_back(env);
    candidates.emplace_back(MESH_VERIFIER_THREE_DIR);
    candidates.emplace_back("node_modules/three");
    candidates.emplace_back("/usr/local/lib/node_modules/three");
    candidates.emplace_back("/usr/lib/node_modules/three");
    for (const auto& c : candidates)
        if (IsThreeDir(c)) return c;
    return {};
}

GuiApp::GuiApp(Scene scene, std::filesystem::path threeDir) : scene_(std::move(scene)), threeDir_(std::move(threeDir)) { Reverify(); }

void GuiApp::Reverify() {
    report_ = VerifyScene(scene_);
    ++revision_;
}

std::vector<NodeTransform> GuiApp::Snapshot() const {
    std::vector<NodeTransform> snapshot;
    for (const SceneNode& n : scene_.Nodes()) snapshot.push_back(n.local);
    return snapshot;
}

void GuiApp::Remember(std::vector<NodeTransform> before, const std::string& group) {
    if (!group.empty() && group == lastGroup_ && !undo_.empty()) return;  // (one undo step for a whole drag)
    lastGroup_ = group;
    undo_.push_back(std::move(before));
    if (undo_.size() > 500) undo_.erase(undo_.begin());
    redo_.clear();
}

json GuiApp::StateJson() const {
    json nodes = json::array();
    for (size_t i = 0; i < scene_.NodeCount(); ++i)
        nodes.push_back({{"local", TransformJson(scene_.Node(i).local)}, {"world", WorldJson(scene_.WorldMatrix(i))}, {"modified", scene_.IsModified(i)}});
    return {{"revision", revision_},
            {"nodes", nodes},
            {"intersections", report_.intersections.size()},
            {"canUndo", !undo_.empty()},
            {"canRedo", !redo_.empty()},
            {"view", ViewJson(BuildView(scene_, report_, revision_))}};
}

json GuiApp::SceneJson() const {
    json meshes = json::array();
    for (const Mesh& m : scene_.Meshes()) {
        json positions = json::array(), triangles = json::array();
        for (const Vec3& v : m.vertices) {
            positions.push_back(v.x);
            positions.push_back(v.y);
            positions.push_back(v.z);
        }
        for (const Triangle& t : m.triangles) {
            triangles.push_back(t.a);
            triangles.push_back(t.b);
            triangles.push_back(t.c);
        }
        meshes.push_back({{"name", m.name}, {"positions", positions}, {"triangles", triangles}});
    }
    json components = json::array();
    for (size_t i = 0; i < scene_.NodeCount(); ++i) {
        const SceneNode& n = scene_.Node(i);
        components.push_back({{"name", n.name},
                              {"parent", n.parent},
                              {"children", n.children},
                              {"mesh", n.mesh},
                              {"inScene", n.inScene},
                              {"triangles", n.mesh >= 0 ? scene_.Meshes()[static_cast<size_t>(n.mesh)].triangles.size() : size_t{0}},
                              {"imported", TransformJson(scene_.ImportedTransform(i))}});
    }
    return {{"name", scene_.Name()}, {"meshes", meshes}, {"components", components}, {"state", StateJson()}};
}

HttpResponse GuiApp::Handle(const std::string& method, const std::string& target, const std::string& body) {
    const size_t qm = target.find('?');
    const std::string path = UrlDecode(target.substr(0, qm));
    const auto query = ParseQuery(qm == std::string::npos ? "" : target.substr(qm + 1));

    if (method == "GET" && (path == "/" || path == "/index.html")) {
        std::string page = PageFiles().at("gui_page.html");
        page = ReplaceAll(page, "__THREE_BASE__", threeDir_.empty() ? kThreeCdn : "/vendor/three/");
        std::lock_guard lock(mutex_);
        page = ReplaceAll(page, "__TITLE__", HtmlEscape(scene_.Name() + " - Mesh Verifier"));
        return {200, "text/html; charset=utf-8", page};
    }
    if (method == "GET" && path.rfind("/vendor/three/", 0) == 0) {
        const std::string rel = path.substr(14);
        if (threeDir_.empty() || rel.find("..") != std::string::npos || (rel.rfind("build/", 0) != 0 && rel.rfind("examples/jsm/", 0) != 0) ||
            rel.size() < 3 || rel.substr(rel.size() - 3) != ".js")
            return {404, "text/plain", "not found"};
        std::ifstream file(threeDir_ / rel, std::ios::binary);
        if (!file) return {404, "text/plain", "not found"};
        std::ostringstream s;
        s << file.rdbuf();
        return {200, "text/javascript; charset=utf-8", s.str()};
    }
    if (method == "GET" && path == "/favicon.ico") return {204, "text/plain", ""};
    if (path.rfind("/api/", 0) == 0) {
        std::lock_guard lock(mutex_);
        try {
            return Api(method, path, query, body);
        } catch (const std::exception& e) {
            return Error(e.what());
        }
    }
    return {404, "text/plain", "not found"};
}

HttpResponse GuiApp::Api(const std::string& method, const std::string& path, const std::map<std::string, std::string>& query, const std::string& body) {
    if (method == "GET") {
        if (path == "/api/scene") return Json(SceneJson());
        if (path == "/api/state") return Json(StateJson());
        if (path == "/api/view") return Json(ViewJson(BuildView(scene_, report_, revision_)));
        if (path == "/api/intersections") return Json(IntersectionsJson(report_));
        return Error("unknown endpoint " + path, 404);
    }
    if (method != "POST") return Error("method not allowed", 405);

    if (path == "/api/load") {
        const auto f = query.find("format");
        const std::optional<ModelFormat> format = f == query.end() ? std::nullopt : ParseModelFormat(f->second);
        if (!format) return Error("load needs format=obj or format=glb");
        std::optional<UpAxis> up = UpAxis::Y;
        if (const auto u = query.find("up"); u != query.end()) up = ParseUpAxis(u->second);
        if (!up) return Error("up must be y or z");
        const std::string name = query.count("name") ? query.at("name") : "model";
        const std::string stem = std::filesystem::path(name).stem().string();
        Scene loaded = ParseScene(body, *format, stem.empty() ? "model" : stem);  // (throws, keeping the scene)
        loaded.SetUp(*up);
        scene_ = std::move(loaded);
        undo_.clear();
        redo_.clear();
        lastGroup_.clear();
        Reverify();
        return Json(SceneJson());
    }
    if (path == "/api/transform") {
        json request;
        try {
            request = body.empty() ? json::object() : json::parse(body);
        } catch (const json::parse_error& e) {
            return Error(std::string("malformed JSON: ") + e.what());
        }
        // A transforms document or one edit (each checked against its schema).
        const std::vector<TransformEdit> edits = request.is_object() && request.contains("transforms")
                                                     ? TransformEditsFromJson(request)
                                                     : std::vector<TransformEdit>{TransformEditFromJson(request)};
        std::vector<NodeTransform> before = Snapshot();
        scene_.Apply(edits);  // (throws, changing nothing, if any edit is invalid)
        const auto group = query.find("group");
        Remember(std::move(before), group == query.end() ? "" : group->second);
        Reverify();
        return Json(StateJson());
    }
    if (path == "/api/reset") {
        const json request = ReadBody(body, Schema::Reset);
        const auto node = request.find("node");
        if (node != request.end() && node->get<size_t>() >= scene_.NodeCount()) return Error("node " + node->dump() + " doesn't exist");
        Remember(Snapshot(), "");
        lastGroup_.clear();
        if (node != request.end()) scene_.Reset(node->get<size_t>(), request.value("subtree", false));
        else scene_.ResetAll();
        Reverify();
        return Json(StateJson());
    }
    if (path == "/api/undo" || path == "/api/redo") {
        auto& from = path == "/api/undo" ? undo_ : redo_;
        auto& to = path == "/api/undo" ? redo_ : undo_;
        lastGroup_.clear();
        if (!from.empty()) {
            to.push_back(Snapshot());
            const std::vector<NodeTransform> target = std::move(from.back());
            from.pop_back();
            for (size_t i = 0; i < target.size() && i < scene_.NodeCount(); ++i) scene_.SetLocalTransform(i, target[i]);
            Reverify();
        }
        return Json(StateJson());
    }
    return Error("unknown endpoint " + path, 404);
}

void RunGuiServer(GuiApp& app, const GuiServerOptions& options) {
    httplib::Server server;
    server.set_payload_max_length(kMaxBody);
    const auto handle = [&app](const httplib::Request& request, httplib::Response& response) {
        HttpResponse r;
        try {
            r = app.Handle(request.method, request.target, request.body);
        } catch (const std::exception& e) {
            r = {500, "text/plain", e.what()};
        }
        response.status = r.status;
        response.set_header("Cache-Control", "no-store");
        response.set_content(r.body, r.contentType);
    };
    server.Get(".*", handle);
    server.Post(".*", handle);
    int port = options.port;
    if (port == 0) port = server.bind_to_any_port(options.host);
    else if (!server.bind_to_port(options.host, port)) port = -1;
    if (port <= 0) throw std::runtime_error("cannot listen on " + options.host + ":" + std::to_string(options.port));
    if (options.onReady) options.onReady("http://" + options.host + ":" + std::to_string(port) + "/");
    if (!server.listen_after_bind()) throw std::runtime_error("the server stopped");
}

}  // namespace mesh_verifier
