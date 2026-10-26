#include "mesh_verifier/web_viewer.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mesh_verifier {
namespace {

// Numbers written so that reading them back gives exactly the same double.
std::string Num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

std::string JsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (c == '<') out += "\\u003c";  // (never close the page's <script> early)
        else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else out += c;
    }
    return out + "\"";
}

// The geometry and the report as JSON: per mesh its name, flat positions, flat triangle indices and the indices
// of its flagged triangles; and the intersection segments.
std::string SceneJson(const GeometryData& geometry, const VerificationReport& report) {
    std::vector<std::set<uint32_t>> flagged(geometry.meshes.size());
    for (const Intersection& hit : report.intersections) {
        if (hit.a.meshIndex < flagged.size()) flagged[hit.a.meshIndex].insert(hit.a.triangleIndex);
        if (hit.b.meshIndex < flagged.size()) flagged[hit.b.meshIndex].insert(hit.b.triangleIndex);
    }
    std::ostringstream j;
    j << "{\"meshes\":[";
    for (size_t m = 0; m < geometry.meshes.size(); ++m) {
        const Mesh& mesh = geometry.meshes[m];
        j << (m ? "," : "") << "{\"name\":" << JsonString(mesh.name) << ",\"positions\":[";
        for (size_t v = 0; v < mesh.vertices.size(); ++v)
            j << (v ? "," : "") << Num(mesh.vertices[v].x) << "," << Num(mesh.vertices[v].y) << "," << Num(mesh.vertices[v].z);
        j << "],\"triangles\":[";
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
            j << (t ? "," : "") << mesh.triangles[t].a << "," << mesh.triangles[t].b << "," << mesh.triangles[t].c;
        j << "],\"flagged\":[";
        bool first = true;
        for (const uint32_t t : flagged[m]) j << (first ? "" : ",") << t, first = false;
        j << "]}";
    }
    j << "],\"intersections\":" << report.intersections.size() << ",\"segments\":[";
    bool first = true;
    for (const Intersection& hit : report.intersections) {
        if (hit.type != IntersectionType::Crossing) continue;
        j << (first ? "" : ",") << "[" << Num(hit.segmentStart.x) << "," << Num(hit.segmentStart.y) << "," << Num(hit.segmentStart.z) << ","
          << Num(hit.segmentEnd.x) << "," << Num(hit.segmentEnd.y) << "," << Num(hit.segmentEnd.z) << "]";
        first = false;
    }
    j << "]}";
    return j.str();
}

const char* kPage = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>@TITLE@</title>
<style>
  html, body { margin: 0; height: 100%; background: #16171a; color: #e8e6e1; font: 13px/1.4 system-ui, sans-serif; overflow: hidden; }
  #info { position: fixed; left: 12px; top: 12px; background: rgba(30,31,35,.85); padding: 8px 10px; border-radius: 6px; max-width: calc(100% - 48px); }
  #info b { color: #ff5a4f; }
  .muted { color: #9b978f; }
</style>
</head>
<body>
<div id="info"></div>
<script type="importmap">
{ "imports": { "three": "https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js",
               "three/addons/": "https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/" } }
</script>
<script type="module">
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

const scene = @SCENE@;

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(2, devicePixelRatio));
document.body.appendChild(renderer.domElement);
const world = new THREE.Scene();
world.background = new THREE.Color(0x16171a);
world.add(new THREE.HemisphereLight(0xffffff, 0x404050, 1.6));
const sun = new THREE.DirectionalLight(0xffffff, 1.6); sun.position.set(2, 3, 4); world.add(sun);
const camera = new THREE.PerspectiveCamera(40, 1, 0.001, 1e7);
const controls = new OrbitControls(camera, renderer.domElement);

// The layers: base geometry (neutral), intersection highlight (the report's triangles, red), wireframe, and the
// intersection segments. Each is its own object, so it can be shown or hidden on its own later.
const layers = { base: new THREE.Group(), highlight: new THREE.Group(), wireframe: new THREE.Group(), segments: new THREE.Group() };
for (const g of Object.values(layers)) world.add(g);

const box = new THREE.Box3();
const subset = (mesh, pick) => {
  const idx = [];
  for (let t = 0; t < mesh.triangles.length / 3; t++) if (pick(t)) idx.push(mesh.triangles[3 * t], mesh.triangles[3 * t + 1], mesh.triangles[3 * t + 2]);
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(mesh.positions, 3));
  g.setIndex(idx);
  return g.toNonIndexed();   // (flat shading: each triangle shown as it is)
};
let flaggedTotal = 0;
for (const mesh of scene.meshes) {
  const red = new Set(mesh.flagged); flaggedTotal += red.size;
  const base = subset(mesh, t => !red.has(t)); base.computeVertexNormals();
  layers.base.add(new THREE.Mesh(base, new THREE.MeshStandardMaterial({ color: 0xb8b4ac, roughness: 0.7, side: THREE.DoubleSide, flatShading: true })));
  if (red.size) {
    const hi = subset(mesh, t => red.has(t)); hi.computeVertexNormals();
    layers.highlight.add(new THREE.Mesh(hi, new THREE.MeshStandardMaterial({ color: 0xff3b30, roughness: 0.6, side: THREE.DoubleSide, flatShading: true, polygonOffset: true, polygonOffsetFactor: -1 })));
  }
  const all = subset(mesh, () => true);
  layers.wireframe.add(new THREE.LineSegments(new THREE.WireframeGeometry(all), new THREE.LineBasicMaterial({ color: 0x1d1d22, transparent: true, opacity: 0.35 })));
  all.computeBoundingBox(); box.union(all.boundingBox);
}
if (scene.segments.length) {
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(scene.segments.flat(), 3));
  layers.segments.add(new THREE.LineSegments(g, new THREE.LineBasicMaterial({ color: 0xffe14d, depthTest: false })));
}

const triangles = scene.meshes.reduce((s, m) => s + m.triangles.length / 3, 0);
document.getElementById("info").innerHTML =
  `${scene.meshes.length} mesh${scene.meshes.length === 1 ? "" : "es"}, ${triangles} triangles<br>` +
  `<b>${scene.intersections} intersecting pair${scene.intersections === 1 ? "" : "s"}</b>, ${flaggedTotal} triangles in red` +
  `<br><span class="muted">yellow: where they cross. Drag to orbit, right-drag to pan, wheel to zoom.</span>`;

// frame the geometry
const size = box.getSize(new THREE.Vector3()).length() || 1, centre = box.getCenter(new THREE.Vector3());
controls.target.copy(centre);
camera.position.copy(centre).add(new THREE.Vector3(0.6, 0.45, 1).normalize().multiplyScalar(size * 1.3));
camera.near = size / 1000; camera.far = size * 100; camera.updateProjectionMatrix();
function resize() { renderer.setSize(innerWidth, innerHeight); camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix(); }
addEventListener("resize", resize); resize();
renderer.setAnimationLoop(() => { controls.update(); renderer.render(world, camera); });
</script>
</body>
</html>
)PAGE";

std::string HtmlEscape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

}  // namespace

std::string WebViewer::Render(const GeometryData& geometry, const VerificationReport& report) const {
    std::string page = kPage;
    ReplaceAll(page, "@TITLE@", HtmlEscape(title));
    ReplaceAll(page, "@SCENE@", SceneJson(geometry, report));
    return page;
}

void WebViewer::Write(const std::filesystem::path& path, const GeometryData& geometry, const VerificationReport& report) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path.string() + "'");
    out << Render(geometry, report);
}

}  // namespace mesh_verifier
