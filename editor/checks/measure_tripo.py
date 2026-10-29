"""Run: python editor/checks/measure_tripo.py (needs numpy, scipy, trimesh).
How the Tripo raven's beak meets its face, measured on the segmented model as placed on our head
(mesh-verifier/models/raven_segmented.glb, our axes: +Y up, +Z forward, +X the character's left).

Prints: the socket rim (the head's open edge where the beak was cut away): its centre, size, top/bottom/back;
the beak (tip, base, length, depth and width at the base); the left eye (centre, radius); the mouth corner (hand tag
on the original); and cross-sections of the face round the beak's base (half-width of the head at a few heights,
for z slices from the eye forward). Also writes junction_tripo.json for the comparison script."""
import json
import numpy as np
import trimesh

HERE = __import__("os").path.dirname(__import__("os").path.abspath(__file__))
S = trimesh.load(HERE + "/../../models/raven_segmented.glb")
P = {n: S.geometry[S.graph[n][1]].copy() for n in S.graph.nodes_geometry}
for n, g in P.items(): g.apply_transform(S.graph[n][0])
head, beak, eye = P["head"], P["beak"], P["eye_L"]
f3 = lambda v: "(" + ", ".join(f"{c:.3f}" for c in v) + ")"
out = {}

# the socket rim: the head's boundary edges (edges used by one face), the largest loop
E = head.edges_sorted
u, c = np.unique(E, axis=0, return_counts=True)
bnd = u[c == 1]
# (the loops: connected groups of open edges; the socket is the one nearest the beak's base)
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
n = len(head.vertices)
A = coo_matrix((np.ones(len(bnd)), (bnd[:, 0], bnd[:, 1])), shape=(n, n))
_, lab = connected_components(A, directed=False)
on = np.unique(bnd)
loops = [on[lab[on] == k] for k in np.unique(lab[on])]
bc0 = beak.vertices[beak.vertices[:, 2] < beak.vertices[:, 2].min() + 0.2].mean(0)
loop = min(loops, key=lambda L: np.linalg.norm(head.vertices[L].mean(0) - bc0))
front = head.vertices[loop]
print(f"open edges {len(bnd)} in {len(loops)} loops (sizes {sorted(len(L) for L in loops)}); socket loop {len(loop)} vertices")
out["rim_pts"] = front.tolist()
rc = front.mean(0)
out["rim"] = {"centre": rc.tolist(), "min": front.min(0).tolist(), "max": front.max(0).tolist()}
print("socket rim: centre", f3(rc), " x", f3([front[:, 0].min(), front[:, 0].max()]), " y", f3([front[:, 1].min(), front[:, 1].max()]), " z", f3([front[:, 2].min(), front[:, 2].max()]))

# the beak
bt = beak.vertices[beak.vertices[:, 2].argmax()]
bb = beak.vertices[beak.vertices[:, 2] < beak.vertices[:, 2].min() + 0.15 * np.ptp(beak.vertices[:, 2])]
out["beak"] = {"tip": bt.tolist(), "baseZ": float(beak.vertices[:, 2].min()), "len": float(np.ptp(beak.vertices[:, 2])),
               "baseHalfW": float(np.abs(bb[:, 0]).max()), "baseTop": float(bb[:, 1].max()), "baseBot": float(bb[:, 1].min())}
print("beak: tip", f3(bt), " length", f"{np.ptp(beak.vertices[:, 2]):.3f}", " base: half-width", f"{np.abs(bb[:, 0]).max():.3f}", " top", f"{bb[:, 1].max():.3f}", " bottom", f"{bb[:, 1].min():.3f}")

# the eye
ec, er = eye.vertices.mean(0), np.linalg.norm(eye.vertices - eye.vertices.mean(0), axis=1).mean()
out["eye"] = {"c": ec.tolist(), "r": float(er)}
print("eye_L: centre", f3(ec), " radius", f"{er:.3f}")

# the mouth corner (hand tag on the original Tripo raven, carried into our space as the editor lines it up)
AL = {"scale": 4.1875, "move": [-0.0439, -0.6008, 0.692]}
O = trimesh.load(HERE + "/../../models/raven_head.obj", force="mesh", process=False).vertices
O = np.c_[-O[:, 2], O[:, 1], O[:, 0]] * AL["scale"] + AL["move"]
tags = json.load(open(HERE + "/../../models/raven_head.tags.json"))["tags"]
mc = O[tags["mouth_corner.R"]]
mc[:, 0] = np.abs(mc[:, 0])
out["corner"] = {"c": mc.mean(0).tolist(), "min": mc.min(0).tolist(), "max": mc.max(0).tolist()}
print("mouth corner (mirrored to the left): centre", f3(mc.mean(0)), " z range", f3([mc[:, 2].min(), mc[:, 2].max()]), " y range", f3([mc[:, 1].min(), mc[:, 1].max()]))
for t in ["nostril.L", "nostril.R", "brow.L", "brow.R"]:
    q = O[tags[t]]; q[:, 0] = np.abs(q[:, 0]); out[t] = q.mean(0).tolist()
    print(f"{t}: centre", f3(q.mean(0)))

# cross-sections: the head's half-width (max |x| of the head surface) in z slices round the beak's base, by height
zs = np.linspace(ec[2] - 0.6, rc[2] + 0.2, 9)
ys = np.linspace(rc[1] - 0.8, ec[1] + 0.5, 9)
V = head.vertices
grid = []
print("half-width of the head (|x|), rows = height y, columns = depth z")
print("        " + " ".join(f"z{z:5.2f}" for z in zs))
for y in ys[::-1]:
    row = []
    for z in zs:
        m = (np.abs(V[:, 1] - y) < 0.06) & (np.abs(V[:, 2] - z) < 0.06)
        row.append(float(np.abs(V[m, 0]).max()) if m.any() else None)
    grid.append(row)
    print(f"y{y:6.2f} " + " ".join(f"{w:6.2f}" if w is not None else "   -  " for w in row))
out["slices"] = {"z": zs.tolist(), "y": ys.tolist(), "w": grid}
import os
out["_note"] = "The segmented Tripo raven (models/raven_segmented.glb, placed on our head) measured where its beak meets its face. Made by measure_tripo.py."
json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "tripo_junction.json"), "w"))
