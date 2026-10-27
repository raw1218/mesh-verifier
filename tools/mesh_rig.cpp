// mesh-rig <model.glb> <rig.json> [--set <control>=<value>]... [--sweep <steps>] [--obj <file>] [--json]
//
// Poses a skinned model (skins and morph targets from the GLB) with a rig, and checks the posed model for
// intersecting triangles. Without --sweep it checks one pose: every control at its default, except the --set ones.
// With --sweep it checks every control alone at <steps> values across its range, then every combination of the
// controls at their min, default and max, and lists the poses where parts intersect. --obj writes the (single)
// pose as an OBJ with one object per part. --json prints the report (schema/rig-report.schema.json).
// Exit code: 0 when the checks ran (whatever they found), 1 for any error (on stderr).
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mesh_rig/rig.h"
#include "mesh_verifier/gltf_importer.h"
#include "mesh_verifier/intersection_verifier.h"

using namespace mesh_verifier;
using namespace mesh_rig;
using nlohmann::json;

namespace {

constexpr const char* kUsage = "usage: mesh-rig <model.glb> <rig.json> [--set <control>=<value>]... [--sweep <steps>] [--obj <file>] [--json]\n";

struct PoseResult {
    std::vector<double> controls;
    size_t intersections = 0;
    std::map<std::pair<uint32_t, uint32_t>, size_t> partPairs;
};

PoseResult Check(const SkinnedModel& model, const RigEvaluator& rig, const std::vector<double>& controls, GeometryData* posed = nullptr) {
    DeformerFrame frame;
    rig.Evaluate(controls, frame);
    GeometryData geometry = Deform(model, PoseFromFrame(model, rig.Definition(), frame));
    const VerificationReport report = VerifyIntersections(geometry);
    PoseResult r{controls, report.intersections.size(), {}};
    for (const Intersection& hit : report.intersections) ++r.partPairs[{hit.a.meshIndex, hit.b.meshIndex}];
    if (posed) *posed = std::move(geometry);
    return r;
}

std::string Describe(const RigDefinition& def, const std::vector<double>& controls, const std::vector<double>& defaults) {
    std::string out;
    for (size_t i = 0; i < controls.size(); ++i)
        if (controls[i] != defaults[i]) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s%s=%g", out.empty() ? "" : " ", def.controls[i].name.c_str(), controls[i]);
            out += buf;
        }
    return out.empty() ? "default pose" : out;
}

void WriteObj(const std::string& path, const GeometryData& geometry) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out.precision(9);
    size_t base = 1;
    for (const Mesh& m : geometry.meshes) {
        out << "o " << m.name << "\n";
        for (const Vec3& v : m.vertices) out << "v " << v.x << " " << v.y << " " << v.z << "\n";
        for (const Triangle& t : m.triangles) out << "f " << base + t.a << " " << base + t.b << " " << base + t.c << "\n";
        base += m.vertices.size();
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> sets;
    std::optional<int> sweep;
    std::string obj;
    bool asJson = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&]() -> std::optional<std::string> { return i + 1 < argc ? std::optional<std::string>(argv[++i]) : std::nullopt; };
        if (a == "--help" || a == "-h") {
            std::cout << kUsage;
            return 0;
        } else if (a == "--set") {
            const auto v = value();
            const size_t eq = v ? v->find('=') : std::string::npos;
            if (eq == std::string::npos) { std::cerr << "error: --set needs <control>=<value>\n" << kUsage; return 1; }
            sets.emplace_back(v->substr(0, eq), v->substr(eq + 1));
        } else if (a == "--sweep") {
            const auto v = value();
            try {
                size_t used = 0;
                sweep = v ? std::stoi(*v, &used) : -1;
                if (!v || used != v->size() || *sweep < 2) throw std::invalid_argument("");
            } catch (const std::exception&) { std::cerr << "error: --sweep needs a number of steps, 2 or more\n"; return 1; }
        } else if (a == "--obj") {
            const auto v = value();
            if (!v) { std::cerr << "error: --obj needs a file\n"; return 1; }
            obj = *v;
        } else if (a == "--json") asJson = true;
        else if (a.size() > 1 && a[0] == '-') { std::cerr << "error: unknown option '" << a << "'\n" << kUsage; return 1; }
        else positional.push_back(a);
    }
    if (positional.size() != 2) { std::cerr << kUsage; return 1; }
    if (sweep && !obj.empty()) { std::cerr << "error: --obj writes one pose; it can't be used with --sweep\n"; return 1; }

    try {
        const SkinnedModel model = LoadSkinnedGLB(positional[0]);
        const OurRigEvaluator rig(LoadRigFile(positional[1]), model);
        const RigDefinition& def = rig.Definition();
        const std::vector<double> defaults = DefaultControls(def);
        std::vector<double> controls = defaults;
        for (const auto& [name, text] : sets) {
            size_t c = 0;
            while (c < def.controls.size() && def.controls[c].name != name) ++c;
            if (c == def.controls.size()) throw std::runtime_error("the rig has no control '" + name + "'");
            size_t used = 0;
            double x = 0;
            try { x = std::stod(text, &used); } catch (const std::exception&) { used = 0; }
            if (used != text.size() || !std::isfinite(x)) throw std::runtime_error("'" + text + "' isn't a number");
            if (x < def.controls[c].min || x > def.controls[c].max) throw std::runtime_error(name + " = " + text + " is outside its range");
            controls[c] = x;
        }

        std::vector<std::vector<double>> poses{controls};
        if (sweep) {
            for (size_t c = 0; c < def.controls.size(); ++c)
                for (int s = 0; s < *sweep; ++s) {
                    std::vector<double> p = controls;
                    p[c] = def.controls[c].min + (def.controls[c].max - def.controls[c].min) * s / (*sweep - 1);
                    if (p != controls) poses.push_back(p);
                }
            if (def.controls.size() <= 8) {  // every combination of min / default / max (3^n poses)
                size_t total = 1;
                for (size_t c = 0; c < def.controls.size(); ++c) total *= 3;
                for (size_t k = 0; k < total; ++k) {
                    std::vector<double> p = controls;
                    size_t code = k;
                    int moved = 0;
                    for (size_t c = 0; c < def.controls.size(); ++c, code /= 3) {
                        const Control& ctl = def.controls[c];
                        p[c] = code % 3 == 0 ? controls[c] : code % 3 == 1 ? ctl.min : ctl.max;
                        if (p[c] != controls[c]) ++moved;
                    }
                    if (moved >= 2) poses.push_back(p);
                }
            }
        }

        std::vector<PoseResult> results;
        GeometryData posed;
        for (const auto& p : poses) results.push_back(Check(model, rig, p, results.empty() ? &posed : nullptr));
        if (!obj.empty()) WriteObj(obj, posed);

        if (asJson) {
            json out{{"rig", def.name}, {"model", model.name}, {"poses", json::array()}};
            for (const PoseResult& r : results) {
                json c = json::object(), pairs = json::array();
                for (size_t i = 0; i < r.controls.size(); ++i) c[def.controls[i].name] = r.controls[i];
                for (const auto& [ab, n] : r.partPairs) pairs.push_back({{"a", model.parts[ab.first].name}, {"b", model.parts[ab.second].name}, {"pairs", n}});
                out["poses"].push_back({{"controls", c}, {"intersections", r.intersections}, {"partPairs", pairs}});
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }
        size_t bad = 0;
        for (const PoseResult& r : results) {
            if (!r.intersections) {  // (a sweep lists only the starting pose and the poses with intersections)
                if (!sweep || &r == &results.front()) std::cout << Describe(def, r.controls, defaults) << ": no intersections\n";
                continue;
            }
            ++bad;
            std::cout << Describe(def, r.controls, defaults) << ": " << r.intersections << " intersecting pair" << (r.intersections == 1 ? "" : "s") << "\n";
            for (const auto& [ab, n] : r.partPairs) {
                const std::string a = model.parts[ab.first].name, b = model.parts[ab.second].name;
                std::cout << "    " << (ab.first == ab.second ? a + " (itself)" : a + " x " + b) << ": " << n << "\n";
            }
        }
        if (sweep) std::cout << results.size() << " poses checked, " << bad << " with intersections\n";
        if (!obj.empty()) std::cout << "wrote " << obj << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
