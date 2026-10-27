// mesh-verify --format <obj|glb> <model> [--headless] [--port <port>] [--transforms <file>] [--json]
//
// Loads a model as a scene of components, applies the optional component edits, and checks it for intersecting
// triangles. With --headless it prints the result and exits; otherwise it serves the interactive GUI on
// http://127.0.0.1:<port>/ (default 8080). --json prints the result in the contract's JSON form (contracts.md).
// Exit code: 0 when verification completed (with or without intersections), 1 for any error (on stderr).
#include <charconv>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

#include "mesh_verifier/gui_server.h"
#include "mesh_verifier/scene.h"
#include "mesh_verifier/scene_io.h"

using namespace mesh_verifier;

namespace {

constexpr const char* kUsage =
    "usage: mesh-verify --format <obj|glb> <model file> [--headless] [--port <port>] [--transforms <file>] [--json]\n";

const char* TypeName(IntersectionType t) { return t == IntersectionType::Crossing ? "crossing" : "coplanar_overlap"; }

std::string Num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

struct Arguments {
    std::optional<ModelFormat> format;
    std::string model;
    std::string transforms;
    bool headless = false;
    bool json = false;
    int port = 8080;
};

// nullopt (after printing why) when the arguments are invalid.
std::optional<Arguments> Parse(int argc, char** argv) {
    Arguments a;
    bool haveFormat = false;
    std::optional<std::string> portText;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&](const char* name) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::cerr << "error: " << name << " needs a value\n";
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };
        if (arg == "--format") {
            const auto v = value("--format");
            if (!v) return std::nullopt;
            a.format = ParseModelFormat(*v);
            haveFormat = true;
            if (!a.format) {
                std::cerr << "error: unknown format '" << *v << "' (use obj or glb)\n";
                return std::nullopt;
            }
        } else if (arg == "--transforms") {
            const auto v = value("--transforms");
            if (!v) return std::nullopt;
            a.transforms = *v;
        } else if (arg == "--port") {
            const auto v = value("--port");
            if (!v) return std::nullopt;
            portText = *v;
        } else if (arg == "--headless") a.headless = true;
        else if (arg == "--json") a.json = true;
        else if (arg.size() > 1 && arg[0] == '-' && arg != "-") {
            std::cerr << "error: unknown option '" << arg << "'\n";
            return std::nullopt;
        } else if (a.model.empty()) a.model = arg;
        else {
            std::cerr << "error: more than one model file given ('" << a.model << "', '" << arg << "')\n";
            return std::nullopt;
        }
    }
    if (!haveFormat) {
        std::cerr << "error: --format is required\n";
        return std::nullopt;
    }
    if (a.model.empty()) {
        std::cerr << "error: no model file given\n";
        return std::nullopt;
    }
    if (portText && !a.headless) {  // (ignored in headless mode)
        int port = -1;
        const auto [ptr, ec] = std::from_chars(portText->data(), portText->data() + portText->size(), port);
        if (ec != std::errc() || ptr != portText->data() + portText->size() || port < 0 || port > 65535) {
            std::cerr << "error: invalid port '" << *portText << "'\n";
            return std::nullopt;
        }
        a.port = port;
    }
    return a;
}

void PrintReport(const Scene& scene, const VerificationReport& report) {
    for (const Intersection& h : report.intersections)
        std::cout << "node " << h.a.meshIndex << " triangle " << h.a.triangleIndex << "  x  node " << h.b.meshIndex << " triangle " << h.b.triangleIndex << "  "
                  << TypeName(h.type) << "  weight " << Num(h.weight) << "\n";
    size_t components = 0, triangles = 0;
    for (const SceneNode& n : scene.Nodes())
        if (n.mesh >= 0 && n.inScene) ++components, triangles += scene.Meshes()[n.mesh].triangles.size();
    std::cout << components << " component" << (components == 1 ? "" : "s") << " with geometry, " << triangles << " triangle" << (triangles == 1 ? "" : "s") << ": "
              << report.intersections.size() << " intersecting pair" << (report.intersections.size() == 1 ? "" : "s") << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            std::cout << kUsage;
            return 0;
        }
    const std::optional<Arguments> args = Parse(argc, argv);
    if (!args) {
        std::cerr << kUsage;
        return 1;
    }
    try {
        Scene scene = LoadScene(args->model, *args->format);
        if (!args->transforms.empty()) scene.Apply(LoadTransformEdits(args->transforms));  // (all or nothing)
        if (args->headless || args->json) {
            const VerificationReport report = VerifyScene(scene);
            if (args->json) std::cout << IntersectionsJson(scene, report).dump() << "\n";
            else PrintReport(scene, report);
            std::cout.flush();
            if (args->headless) return 0;
        }
        GuiApp app(std::move(scene), FindThreeJs());
        GuiServerOptions options;
        options.port = args->port;
        options.onReady = [](const std::string& url) { std::cout << url << std::endl; };
        RunGuiServer(app, options);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
