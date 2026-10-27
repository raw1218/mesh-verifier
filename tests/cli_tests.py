#!/usr/bin/env python3
"""End-to-end tests of the mesh-verify command (contracts.md). Usage: cli_tests.py <path to mesh-verify>"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.request

EXE = None
HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
EXAMPLE = os.path.join(HERE, "..", "examples", "sphere_through_torus.obj")


def run(*args):
    return subprocess.run([EXE, *args], capture_output=True, text=True, timeout=120)


def pairs(stdout):
    doc = json.loads(stdout)
    return {(p["a"]["node"], p["a"]["triangle"], p["b"]["node"], p["b"]["triangle"]) for p in doc["intersections"]}


class Headless(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def transforms(self, doc):
        path = os.path.join(self.tmp.name, "t.json")
        with open(path, "w") as f:
            f.write(doc if isinstance(doc, str) else json.dumps(doc))
        return path

    def test_obj_json(self):
        r = run("--format", "obj", EXAMPLE, "--headless", "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        p = pairs(r.stdout)
        self.assertEqual(len(p), 184)
        self.assertTrue(all(a == 0 and b == 0 for a, _, b, _ in p))
        entry = json.loads(r.stdout)["intersections"][0]
        self.assertEqual(set(entry), {"a", "b"})
        self.assertEqual(set(entry["a"]), {"node", "triangle"})

    def test_no_intersections_is_success(self):
        r = run("--format", "glb", os.path.join(FIXTURES, "hierarchy.glb"), "--headless", "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(json.loads(r.stdout), {"intersections": []})

    def test_glb_between_components(self):
        r = run("--format", "glb", os.path.join(FIXTURES, "overlapping.glb"), "--headless", "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        p = pairs(r.stdout)
        self.assertTrue(p)
        self.assertTrue(all(a == 0 and b == 1 for a, _, b, _ in p))

    def test_transforms_move_children(self):
        glb = os.path.join(FIXTURES, "hierarchy.glb")
        # move the arm onto cubeA: the arm intersects it, its child (the hand) stays clear
        t = self.transforms({"transforms": [{"node": 2, "space": "world", "translation": [0.5, 0, 0]}]})
        p = pairs(run("--format", "glb", glb, "--headless", "--json", "--transforms", t).stdout)
        self.assertTrue(p)
        self.assertEqual({(a, b) for a, _, b, _ in p}, {(1, 2)})
        # move the root: everything moves together, nothing intersects
        t = self.transforms({"transforms": [{"node": 0, "translation": [5, 5, 5], "rotationEuler": [10, 20, 30], "scale": [2, 2, 2]}]})
        r = run("--format", "glb", glb, "--headless", "--json", "--transforms", t)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(pairs(r.stdout), set())
        # the hand, in world space, onto cubeA
        t = self.transforms({"transforms": [{"node": 3, "space": "world", "translation": [0, 0.5, 0], "rotationEuler": [0, 0, 45]}]})
        p = pairs(run("--format", "glb", glb, "--headless", "--json", "--transforms", t).stdout)
        self.assertEqual({(a, b) for a, _, b, _ in p}, {(1, 3)})

    def test_scale(self):
        glb = os.path.join(FIXTURES, "hierarchy.glb")
        t = self.transforms({"transforms": [{"node": 1, "scale": [7, 1, 1]}]})
        p = pairs(run("--format", "glb", glb, "--headless", "--json", "--transforms", t).stdout)
        self.assertEqual({(a, b) for a, _, b, _ in p}, {(1, 2)})

    def test_obj_component_transform(self):
        t = self.transforms({"transforms": [{"node": 0, "translation": [0, 0, 0], "rotationEuler": [0, 0, 0], "scale": [1, 1, 1]}]})
        a = run("--format", "obj", EXAMPLE, "--headless", "--json", "--transforms", t)
        self.assertEqual(pairs(a.stdout), pairs(run("--format", "obj", EXAMPLE, "--headless", "--json").stdout))
        t = self.transforms({"transforms": [{"node": 0, "translation": [100, 0, 0], "rotationEuler": [0, 90, 0], "scale": [2, 2, 2]}]})
        b = run("--format", "obj", EXAMPLE, "--headless", "--json", "--transforms", t)
        self.assertEqual(b.returncode, 0, b.stderr)
        self.assertEqual(len(pairs(b.stdout)), 184)  # a whole-model similarity keeps its self-intersections

    def test_text_output(self):
        r = run("--format", "glb", os.path.join(FIXTURES, "overlapping.glb"), "--headless")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("intersecting pairs", r.stdout)

    def test_errors(self):
        glb = os.path.join(FIXTURES, "hierarchy.glb")
        cases = [
            ("--format", "glb", glb, "--headless", "--transforms", self.transforms({"transforms": [{"node": 99}]})),
            ("--format", "glb", glb, "--headless", "--transforms", self.transforms({"transforms": [{"node": 1, "space": "screen"}]})),
            ("--format", "glb", glb, "--headless", "--transforms", self.transforms({"transforms": [{"node": 1, "scale": [1, 1]}]})),
            ("--format", "glb", glb, "--headless", "--transforms", self.transforms({"transforms": [{"node": -1}]})),
            ("--format", "glb", glb, "--headless", "--transforms", self.transforms({"edits": []})),
            ("--format", "glb", glb, "--headless", "--transforms", self.transforms("{not json")),
            ("--format", "glb", glb, "--headless", "--transforms", os.path.join(self.tmp.name, "missing.json")),
            ("--format", "fbx", glb, "--headless"),
            (glb, "--headless"),
            ("--format", "glb", "--headless"),
            ("--format", "glb", os.path.join(FIXTURES, "missing.glb"), "--headless"),
            ("--format", "glb", os.path.join(FIXTURES, "triangle.obj"), "--headless"),
            ("--format", "obj", glb, "--headless"),
            ("--format", "glb", glb, "--headless", "--bogus"),
        ]
        for args in cases:
            r = run(*args)
            self.assertEqual(r.returncode, 1, args)
            self.assertTrue(r.stderr.strip(), args)
            self.assertEqual(r.stdout, "", args)

    def test_format_not_extension_selects_loader(self):
        path = os.path.join(self.tmp.name, "model.dat")
        with open(os.path.join(FIXTURES, "overlapping.glb"), "rb") as src, open(path, "wb") as dst:
            dst.write(src.read())
        r = run("--format", "glb", path, "--headless", "--json")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(pairs(r.stdout))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Gui(unittest.TestCase):
    def test_gui_serves_and_edits(self):
        port = free_port()
        proc = subprocess.Popen([EXE, "--format", "glb", os.path.join(FIXTURES, "hierarchy.glb"), "--port", str(port)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            line = proc.stdout.readline().strip()
            self.assertEqual(line, f"http://127.0.0.1:{port}/")
            base = line.rstrip("/")
            page = urllib.request.urlopen(base + "/", timeout=10).read().decode()
            self.assertIn("TransformControls", page)
            state = json.load(urllib.request.urlopen(base + "/api/state", timeout=10))
            self.assertEqual(state["intersections"], 0)
            req = urllib.request.Request(base + "/api/transform", method="POST",
                                         data=json.dumps({"node": 2, "space": "world", "translation": [0.5, 0, 0]}).encode())
            state = json.load(urllib.request.urlopen(req, timeout=10))
            self.assertGreater(state["intersections"], 0)
            result = json.load(urllib.request.urlopen(base + "/api/intersections", timeout=10))
            self.assertEqual({(p["a"]["node"], p["b"]["node"]) for p in result["intersections"]}, {(1, 2)})
            # the port is now taken: a second server fails cleanly
            second = run("--format", "glb", os.path.join(FIXTURES, "hierarchy.glb"), "--port", str(port))
            self.assertEqual(second.returncode, 1)
        finally:
            proc.terminate()
            proc.wait(timeout=10)


if __name__ == "__main__":
    EXE = os.path.abspath(sys.argv.pop(1))
    unittest.main(verbosity=1)
