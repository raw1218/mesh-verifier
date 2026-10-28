#include "mesh_verifier/gui_server.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
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

GuiApp::GuiApp(Scene scene, std::filesystem::path threeDir, std::shared_ptr<const PoseSource> rig)
    : scene_(std::move(scene)), threeDir_(std::move(threeDir)), rig_(std::move(rig)) {
    if (rig_)
        for (const PoseSource::ControlInfo& c : rig_->Controls()) controls_.push_back(c.defaultValue);
    Reverify();
}

json GuiApp::ControlsJson() const {
    json list = json::array();
    if (rig_) {
        const auto controls = rig_->Controls();
        for (size_t i = 0; i < controls.size(); ++i) {
            json entry{{"name", controls[i].name}, {"min", controls[i].min}, {"max", controls[i].max}, {"default", controls[i].defaultValue}, {"value", controls_[i]}};
            if (json rules = rig_->Rules(i); !rules.is_null()) entry["rules"] = std::move(rules);
            list.push_back(std::move(entry));
        }
    }
    return {{"controls", list}, {"joints", rig_ ? rig_->Joints() : std::vector<std::string>{}}};
}

namespace {

// Blue (still) through yellow to red (moves the most).
std::array<double, 4> Heat(double t) {
    const std::array<double, 3> cold{0.20, 0.35, 0.95}, mid{1.0, 0.85, 0.20}, hot{1.0, 0.20, 0.10};
    const auto& [a, b] = t < 0.5 ? std::pair{cold, mid} : std::pair{mid, hot};
    const double s = t < 0.5 ? t * 2 : t * 2 - 1;
    return {a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s, a[2] + (b[2] - a[2]) * s, 1.0};
}

}  // namespace

// Blender's weight paint colours: blue (0), cyan, green, yellow, red (1).
std::array<double, 4> WeightColor(double w) {
    static const std::array<std::array<double, 3>, 5> stops{{{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}};
    const double x = std::clamp(w, 0.0, 1.0) * 4;
    const size_t i = std::min<size_t>(static_cast<size_t>(x), 3);
    const double s = x - static_cast<double>(i);
    const auto& a = stops[i];
    const auto& b = stops[i + 1];
    return {a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s, a[2] + (b[2] - a[2]) * s, 1.0};
}

View GuiApp::CurrentView() const {
    View view = BuildView(scene_, report_, revision_);
    if (rig_ && weights_) {  // the chosen joint's skin weights over every triangle
        const std::vector<std::vector<double>> w = rig_->Weights(*weights_);
        Overlay paint{"weights", "Weights of " + rig_->Joints()[*weights_], OverlayKind::Triangles, OverlayRole::Info, {1, 1, 1, 1}, {}, {}, {}};
        for (size_t i = 0; i < scene_.NodeCount() && w.size() == scene_.Meshes().size(); ++i) {
            const SceneNode& n = scene_.Node(i);
            if (n.mesh < 0 || !n.inScene) continue;
            const size_t m = static_cast<size_t>(n.mesh);
            const Mesh& mesh = scene_.Meshes()[m];
            if (w[m].size() != mesh.vertices.size()) continue;
            const Mat4 world = scene_.WorldMatrix(i);
            for (size_t t = 0; t < mesh.triangles.size(); ++t) {
                paint.items.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(t)});
                for (const uint32_t v : {mesh.triangles[t].a, mesh.triangles[t].b, mesh.triangles[t].c}) {
                    paint.positions.push_back(TransformPoint(world, mesh.vertices[v]));
                    paint.colors.push_back(WeightColor(w[m][v]));
                }
            }
        }
        view.overlays.push_back(std::move(paint));
    }
    if (!rig_ || !influence_) return view;
    // Where the chosen control moves each vertex: from the current pose to the control at the shown value.
    const auto controls = rig_->Controls();
    const PoseSource::ControlInfo& control = controls[*influence_];
    std::vector<double> values = controls_;
    const double current = values[*influence_];
    values[*influence_] = influenceValue_ ? *influenceValue_ : (control.max - current >= current - control.min ? control.max : control.min);
    const std::vector<std::vector<Vec3>> target = rig_->Pose(values);
    std::vector<std::vector<double>> moved(scene_.Meshes().size());
    double most = 0;
    for (size_t m = 0; m < moved.size(); ++m)
        for (size_t v = 0; v < scene_.Meshes()[m].vertices.size(); ++v) {
            moved[m].push_back(Length(target[m][v] - scene_.Meshes()[m].vertices[v]));
            most = std::max(most, moved[m].back());
        }
    const double still = most * 1e-3;

    char at[64];
    std::snprintf(at, sizeof at, "%g", values[*influence_]);
    Overlay heat{"influence", "What " + control.name + " moves (to " + at + ")", OverlayKind::Triangles, OverlayRole::Influence, {1, 1, 1, 1}, {}, {}, {}};
    if (most > 0) {
        for (size_t i = 0; i < scene_.NodeCount(); ++i) {
            const SceneNode& n = scene_.Node(i);
            if (n.mesh < 0 || !n.inScene) continue;
            const size_t m = static_cast<size_t>(n.mesh);
            const Mesh& mesh = scene_.Meshes()[m];
            const Mat4 world = scene_.WorldMatrix(i);
            for (size_t t = 0; t < mesh.triangles.size(); ++t) {
                const Triangle& tri = mesh.triangles[t];
                if (moved[m][tri.a] <= still && moved[m][tri.b] <= still && moved[m][tri.c] <= still) continue;
                heat.items.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(t)});
                for (const uint32_t v : {tri.a, tri.b, tri.c}) {
                    heat.positions.push_back(TransformPoint(world, mesh.vertices[v]));
                    heat.colors.push_back(Heat(moved[m][v] / most));
                }
            }
        }
    }
    view.overlays.push_back(std::move(heat));
    return view;
}

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
            {"view", ViewJson(CurrentView())}};
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
        if (path == "/api/view") return Json(ViewJson(CurrentView()));
        if (path == "/api/intersections") return Json(IntersectionsJson(scene_, report_));
        if (path == "/api/controls") return Json(ControlsJson());
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
        rig_.reset();  // (the rig was for the old model)
        controls_.clear();
        influence_.reset();
        influenceValue_.reset();
        weights_.reset();
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
    if (path == "/api/controls") {
        if (!rig_) return Error("this model has no rig");
        const json request = ReadBody(body, Schema::Pose);
        const auto controls = rig_->Controls();
        std::vector<double> values = controls_;
        for (const auto& [name, value] : request.at("values").items()) {
            size_t c = 0;
            while (c < controls.size() && controls[c].name != name) ++c;
            if (c == controls.size()) return Error("the rig has no control '" + name + "'");
            const double x = value.get<double>();
            if (x < controls[c].min || x > controls[c].max) return Error(name + " = " + value.dump() + " is outside " + json(controls[c].min).dump() + ".." + json(controls[c].max).dump());
            values[c] = x;
        }
        std::vector<std::vector<Vec3>> posed = rig_->Pose(values);
        if (posed.size() != scene_.Meshes().size()) return Error("the rig posed " + std::to_string(posed.size()) + " meshes, the scene has " + std::to_string(scene_.Meshes().size()), 500);
        for (size_t m = 0; m < posed.size(); ++m)  // (counts checked first, so a bad pose changes nothing)
            if (posed[m].size() != scene_.Meshes()[m].vertices.size() || !std::all_of(posed[m].begin(), posed[m].end(), [](const Vec3& v) { return IsFinite(v); }))
                return Error("the rig posed mesh " + std::to_string(m) + " with the wrong number of vertices, or ones that aren't finite", 500);
        json meshes = json::array();
        for (size_t m = 0; m < posed.size(); ++m) {
            json positions = json::array();
            for (const Vec3& v : posed[m]) {
                positions.push_back(v.x);
                positions.push_back(v.y);
                positions.push_back(v.z);
            }
            meshes.push_back({{"positions", positions}});
            scene_.SetVertices(m, std::move(posed[m]));
        }
        controls_ = values;
        Reverify();
        return Json({{"meshes", meshes}, {"state", StateJson()}});
    }
    if (path == "/api/weights") {
        const json request = ReadBody(body, Schema::Weights);
        if (!request.contains("joint")) {
            weights_.reset();
            return Json(StateJson());
        }
        if (!rig_) return Error("this model has no rig");
        const std::vector<std::string> joints = rig_->Joints();
        const auto it = std::find(joints.begin(), joints.end(), request.at("joint").get<std::string>());
        if (it == joints.end()) return Error("the meshes aren't skinned to a joint '" + request.at("joint").get<std::string>() + "'");
        weights_ = static_cast<size_t>(it - joints.begin());
        return Json(StateJson());
    }
    if (path == "/api/influence") {
        const json request = ReadBody(body, Schema::Influence);
        if (!request.contains("control")) {
            influence_.reset();
            influenceValue_.reset();
            return Json(StateJson());
        }
        if (!rig_) return Error("this model has no rig");
        const auto controls = rig_->Controls();
        const std::string name = request.at("control").get<std::string>();
        size_t c = 0;
        while (c < controls.size() && controls[c].name != name) ++c;
        if (c == controls.size()) return Error("the rig has no control '" + name + "'");
        std::optional<double> value;
        if (request.contains("value")) {
            value = request.at("value").get<double>();
            if (*value < controls[c].min || *value > controls[c].max) return Error(name + " = " + request.at("value").dump() + " is outside its range");
        }
        influence_ = c;
        influenceValue_ = value;
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
    // The port is ours alone: a second server on it fails instead of sharing it (cpp-httplib's default allows
    // SO_REUSEPORT on POSIX; SO_REUSEADDR on Windows lets another socket take a port in use).
    server.set_socket_options([](socket_t sock) {
        int yes = 1;
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof yes);
#else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);  // (only for a port in TIME_WAIT)
#endif
    });
    int port = options.port;
    if (port == 0) port = server.bind_to_any_port(options.host);
    else if (!server.bind_to_port(options.host, port)) port = -1;
    if (port <= 0) throw std::runtime_error("cannot listen on " + options.host + ":" + std::to_string(options.port));
    if (options.onReady) options.onReady("http://" + options.host + ":" + std::to_string(port) + "/");
    if (!server.listen_after_bind()) throw std::runtime_error("the server stopped");
}

}  // namespace mesh_verifier
