"""Serves the stylized character editor and saves what it makes.

    python editor/serve.py [port] [--verifier path/to/mesh-verify]       then open http://localhost:<port>/editor/

Saving writes editor/out/<name>.glb (the mesh at rest, skinned), <name>.rig.json (the rig), <name>.parts.json (every
vertex's part, per mesh) and <name>.params.json (the sliders), then runs mesh-verify on the GLB with the rig: the
rig must load against the model, and nothing may pass through anything: at rest, in each of the page's poses, and
with each control alone at its maximum. The results come back to the page.
"""
import base64
import json
import os
import subprocess
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "out")

args = sys.argv[1:]
port = int(args[0]) if args and args[0].isdigit() else 8790
verifier = os.environ.get("MESH_VERIFY")
if "--verifier" in args:
    verifier = args[args.index("--verifier") + 1]
if not verifier:
    for rel in ("build/Release/mesh-verify.exe", "build/mesh-verify", "build/Debug/mesh-verify.exe"):
        if os.path.exists(os.path.join(ROOT, rel)):
            verifier = os.path.join(ROOT, rel)
            break


def verify(glb, rig):
    if not verifier:
        return "saved (no mesh-verify found: pass --verifier to check it)"
    r = subprocess.run([verifier, "--format", "glb", glb, "--rig", rig, "--headless", "--json"],
                       capture_output=True, text=True, timeout=300)
    if r.returncode not in (0, 1):
        return "saved; mesh-verify failed: " + (r.stderr or r.stdout).strip()[:400]
    try:
        res = json.loads(r.stdout)
    except ValueError:
        return "saved; mesh-verify said: " + (r.stdout + r.stderr).strip()[:400]
    n = len(res.get("intersections", res.get("pairs", [])))
    return f"saved and verified: the rig loads; {n} intersecting triangle pairs at rest"


def verify_poses(glb, rig, poses):
    """Runs mesh-verify's server with the rig, sets each pose (the others' controls at 0) and each control alone at
    its maximum, and reports the poses where triangles pass through each other, with which meshes."""
    import socket
    import time
    import urllib.request
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        vport = s.getsockname()[1]
    proc = subprocess.Popen([verifier, "--format", "glb", glb, "--rig", rig, "--port", str(vport)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f"http://127.0.0.1:{vport}"

    def call(path, body=None):
        req = urllib.request.Request(base + path, data=None if body is None else json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"})
        return json.loads(urllib.request.urlopen(req, timeout=120).read())
    try:
        for _ in range(100):
            try:
                controls = call("/api/controls")["controls"]
                break
            except OSError:
                time.sleep(0.1)
        else:
            return ["the verifier's server didn't start"]
        names = [c["name"] for c in controls]
        cases = list(poses.items()) + [(c["name"] + " at max", {c["name"]: c["max"]}) for c in controls]
        bad = []
        for label, values in cases:
            state = call("/api/controls", {"values": {n: values.get(n, 0) for n in names}})["state"]
            if state["intersections"]:
                pairs = {}
                for p in call("/api/intersections")["intersections"]:
                    k = " x ".join(sorted({p["a"]["name"], p["b"]["name"]}))
                    pairs[k] = pairs.get(k, 0) + 1
                bad.append(f"{label}: {state['intersections']} ({', '.join(f'{k} {v}' for k, v in pairs.items())})")
        return bad or [f"all {len(cases)} poses clean"]
    finally:
        proc.kill()


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=ROOT, **k)

    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")     # (edits show on reload)
        super().end_headers()

    def do_POST(self):
        if self.path != "/api/save":
            self.send_error(404)
            return
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        name = "".join(c for c in body["name"] if c.isalnum() or c in "_-")
        os.makedirs(OUT, exist_ok=True)
        glb = os.path.join(OUT, name + ".glb")
        rig = os.path.join(OUT, name + ".rig.json")
        open(glb, "wb").write(base64.b64decode(body["glb"]))
        json.dump(body["rig"], open(rig, "w"), indent=1)
        json.dump(body["partition"], open(os.path.join(OUT, name + ".parts.json"), "w"))
        json.dump(body["params"], open(os.path.join(OUT, name + ".params.json"), "w"), indent=1)
        posed = []
        try:
            msg = verify(glb, rig)
            if verifier and msg.startswith("saved and verified"):
                posed = verify_poses(glb, rig, body.get("poses", {}))
        except Exception as e:  # (the files are saved either way)
            msg = f"saved; couldn't run mesh-verify: {e}"
        print(msg, *posed, sep="\n  ", flush=True)
        data = json.dumps({"message": msg, "posed": posed, "glb": glb}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


print(f"http://localhost:{port}/editor/  (mesh-verify: {verifier or 'none'})")
ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
