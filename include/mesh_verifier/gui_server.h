// The engine's HTTP API, served on localhost, and the web GUI that uses it. Every request and response body is JSON
// described by a schema in schema/; a renderer (the web page, the Blender add-on, ...) only draws what the API
// sends and sends edits back:
//   GET  /api/scene          the scene: components, source meshes and the state     (scene.schema.json)
//   GET  /api/state          transforms, the number of intersections and the view    (state.schema.json)
//   GET  /api/view           what to draw: placed parts and highlight overlays       (view.schema.json)
//   GET  /api/intersections  the verification result in the contract's form          (intersections.schema.json)
//   POST /api/transform[?group=<id>]   one edit (transform-edit.schema.json) or a transforms document
//                            (transforms.schema.json, all or nothing); returns the state. Edits with the same group
//                            (one drag) are one undo step.
//   POST /api/reset          (reset.schema.json) returns the state
//   POST /api/undo, /api/redo                                                         returns the state
//   POST /api/load?format=obj|glb&name=<file name>[&up=y|z]   the model file as the body; returns the scene
//   GET  /api/controls       the rig's controls and their values; none without a rig     (controls.schema.json)
//   POST /api/controls       control values by name (pose.schema.json): poses the meshes, verifies again; returns
//                            the new mesh positions and the state                        (posed.schema.json)
// Errors are HTTP 4xx with {"error": "..."} (error.schema.json).
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mesh_verifier/scene.h"
#include "mesh_verifier/verification_report.h"

namespace mesh_verifier {

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
};

// Something that poses the scene's meshes from named control values: a rig. (The GUI knows nothing about rigs; the
// rig library provides one of these.) Pose returns new vertex positions for every source mesh of the scene, in the
// scene's mesh order, the counts unchanged.
class PoseSource {
public:
    struct ControlInfo {
        std::string name;
        double min = 0, max = 1, defaultValue = 0;
    };
    virtual ~PoseSource() = default;
    virtual std::vector<ControlInfo> Controls() const = 0;
    virtual std::vector<std::vector<Vec3>> Pose(const std::vector<double>& values) const = 0;
};

// The engine's state and its HTTP API, independent of any socket (so it can be driven directly, e.g. in tests).
class GuiApp {
public:
    // `threeDir`: a local three.js package (the directory with build/ and examples/) to serve the page's scripts
    // from; if empty, the page loads three.js from a CDN. `rig`: optional; with one, the controls endpoints pose the
    // scene's meshes (and loading another model drops it).
    GuiApp(Scene scene, std::filesystem::path threeDir = {}, std::shared_ptr<const PoseSource> rig = nullptr);

    // Handles one request; `target` is the path with an optional query string. Thread-safe.
    HttpResponse Handle(const std::string& method, const std::string& target, const std::string& body);

    // The current scene and its verification (call only while no request is being handled).
    const Scene& CurrentScene() const { return scene_; }
    const VerificationReport& CurrentReport() const { return report_; }

private:
    nlohmann::json StateJson() const;
    nlohmann::json SceneJson() const;
    void Reverify();
    std::vector<NodeTransform> Snapshot() const;
    void Remember(std::vector<NodeTransform> before, const std::string& group);
    HttpResponse Api(const std::string& method, const std::string& path, const std::map<std::string, std::string>& query, const std::string& body);

    std::mutex mutex_;
    Scene scene_;
    VerificationReport report_;
    std::filesystem::path threeDir_;
    uint64_t revision_ = 0;
    std::vector<std::vector<NodeTransform>> undo_, redo_;
    std::string lastGroup_;
    std::shared_ptr<const PoseSource> rig_;
    std::vector<double> controls_;  // the rig's current control values

    nlohmann::json ControlsJson() const;
};

// A local three.js package to serve: $MESH_VERIFIER_THREE_DIR, the one found at build time, or a global npm
// install. Empty if there's none.
std::filesystem::path FindThreeJs();

struct GuiServerOptions {
    std::string host = "127.0.0.1";
    int port = 8080;  // 0: any free port
    // Called once the server is listening, with its URL.
    std::function<void(const std::string& url)> onReady;
};

// Serves `app` until the process ends. Throws std::runtime_error if the port can't be opened.
void RunGuiServer(GuiApp& app, const GuiServerOptions& options);

}  // namespace mesh_verifier
