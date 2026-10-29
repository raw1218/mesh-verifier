// Crossings in every pose: pose the built head with its own rig (linear blend skinning: each joint's turn about its
// rest place in its own frame, X then Y then Z, plus its move in the head's frame; the joints here all hang off the
// head, which stays put) and count triangle pairs that cross (not sharing a vertex), by pair of parts.
//   node editor/checks/poses.mjs [match|defaults] [pose,pose,...]     (WHERE=1: where each kind of crossing is)
// The Tripo match on 2026-10-29: neutral 19, beak open 83, smile 166, caw 91 (the old match: 144, 229, 482, 262).
import { build, DEFAULTS } from "../raven.js";
import { readFileSync } from "fs";
const html = readFileSync(new URL("../index.html", import.meta.url), "utf8");
const src = html.slice(html.indexOf("const TRIPO_MATCH = {") + "const TRIPO_MATCH = ".length);
const MATCH = Function("return " + src.slice(0, src.indexOf("};") + 1))();
const ch = build({ ...DEFAULTS, ...(process.argv[2] === "defaults" ? {} : MATCH), detail: 0.04 });
const POSES = {
  neutral: {}, "beak open": { jawOpen: 1 }, blink: { eyeBlink_L: 1, eyeBlink_R: 1 },
  smile: { mouthSmile_L: 1, mouthSmile_R: 1, eyeSquint_L: 0.6, eyeSquint_R: 0.6, browOuterUp_L: 0.3, browOuterUp_R: 0.3 },
  angry: { browDown_L: 1, browDown_R: 1, mouthFrown_L: 0.7, mouthFrown_R: 0.7, eyeSquint_L: 0.4, eyeSquint_R: 0.4, jawOpen: 0.15 },
  surprised: { browInnerUp: 1, browOuterUp_L: 0.8, browOuterUp_R: 0.8, eyeWide_L: 1, eyeWide_R: 1, jawOpen: 0.55 },
  sad: { browInnerUp: 0.9, mouthFrown_L: 1, mouthFrown_R: 1, eyeLookDown_L: 0.5, eyeLookDown_R: 0.5 },
  caw: { jawOpen: 1, mouthStretch_L: 1, mouthStretch_R: 1, browDown_L: 0.5, browDown_R: 0.5, eyeSquint_L: 0.5, eyeSquint_R: 0.5 },
};
const only = process.argv[3] ? process.argv[3].split(",") : Object.keys(POSES);
const deg = Math.PI / 180;
const mm = (A, B) => A.map((r, i) => [0, 1, 2].map(j => r[0] * B[0][j] + r[1] * B[1][j] + r[2] * B[2][j]));
const rx = a => [[1, 0, 0], [0, Math.cos(a), -Math.sin(a)], [0, Math.sin(a), Math.cos(a)]];
const ry = a => [[Math.cos(a), 0, Math.sin(a)], [0, 1, 0], [-Math.sin(a), 0, Math.cos(a)]];
const rz = a => [[Math.cos(a), -Math.sin(a), 0], [Math.sin(a), Math.cos(a), 0], [0, 0, 1]];
const mv = (M, v) => [0, 1, 2].map(i => M[i][0] * v[0] + M[i][1] * v[1] + M[i][2] * v[2]);
function jointMaps(pose) {
  const acc = ch.joints.map(() => ({ r: [0, 0, 0], t: [0, 0, 0] }));
  const J = Object.fromEntries(ch.joints.map((j, i) => [j.name, i]));
  for (const o of ch.rig.joints) {
    const v = ch.rig.values.find(x => x.name === o.from), c = pose[v.control] || 0;
    if (!c) continue;
    const a = acc[J[o.joint]];
    if (o.rotationEuler) for (let k = 0; k < 3; k++) a.r[k] += o.rotationEuler[k] * c;
    if (o.translation) for (let k = 0; k < 3; k++) a.t[k] += o.translation[k] * c;
  }
  return ch.joints.map((j, i) => {
    const F = [0, 1, 2].map(r => [0, 1, 2].map(c2 => j.frame[c2][r]));          // columns: the frame's axes
    const Ft = j.frame;                                                          // rows: the axes (its inverse)
    const R = mm(F, mm(mm(rz(acc[i].r[2] * deg), mm(ry(acc[i].r[1] * deg), rx(acc[i].r[0] * deg))), Ft));
    const t = acc[i].t, at = j.at;
    return q => { const d = mv(R, [q[0] - at[0], q[1] - at[1], q[2] - at[2]]); return [at[0] + d[0] + t[0], at[1] + d[1] + t[1], at[2] + d[2] + t[2]]; };
  });
}
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const segTri = (p0, q0, a, b, c) => { const n = cross(sub(b, a), sub(c, a)), dp = dot(n, sub(p0, a)), dq = dot(n, sub(q0, a));
  if (dp * dq >= 0) return false; const t = dp / (dp - dq), x = [0, 1, 2].map(m => p0[m] + t * (q0[m] - p0[m]));
  const s1 = dot(cross(sub(b, a), sub(x, a)), n), s2 = dot(cross(sub(c, b), sub(x, b)), n), s3 = dot(cross(sub(a, c), sub(x, c)), n);
  return (s1 > 0 && s2 > 0 && s3 > 0) || (s1 < 0 && s2 < 0 && s3 < 0); };
const short = n => n.replace("_geo", "").replace(/_[LR]$/, "");
for (const name of only) {
  const maps = jointMaps(POSES[name]), T = [];
  for (const m of ch.parts) {
    const Q = [];
    for (let v = 0; v < m.positions.length / 3; v++) {
      const q = [m.positions[3 * v], m.positions[3 * v + 1], m.positions[3 * v + 2]], o = [0, 0, 0];
      for (let k = 0; k < 4; k++) { const w = m.weights[4 * v + k]; if (!w) continue; const r = maps[m.joints[4 * v + k]](q); for (let c = 0; c < 3; c++) o[c] += w * r[c]; }
      Q.push(o);
    }
    for (let t = 0; t < m.indices.length; t += 3) { const i = [m.indices[t], m.indices[t + 1], m.indices[t + 2]];
      T.push({ m: short(m.name), i: i.map(k => m.name + k), v: i.map(k => Q[k]) }); }
  }
  const cell = 0.06, grid = new Map();
  T.forEach((tr, n) => { const lo = [0, 1, 2].map(m => Math.floor(Math.min(...tr.v.map(q => q[m])) / cell)), hi = [0, 1, 2].map(m => Math.floor(Math.max(...tr.v.map(q => q[m])) / cell));
    for (let a = lo[0]; a <= hi[0]; a++) for (let b = lo[1]; b <= hi[1]; b++) for (let c = lo[2]; c <= hi[2]; c++) { const k = a + "," + b + "," + c; if (!grid.has(k)) grid.set(k, []); grid.get(k).push(n); } });
  const seen = new Set(), bins = {}, where = {};
  for (const list of grid.values()) for (let x = 0; x < list.length; x++) for (let y = x + 1; y < list.length; y++) {
    const A = T[list[x]], B = T[list[y]]; if (A.i.some(v => B.i.includes(v))) continue;
    const key = list[x] < list[y] ? list[x] * 1e7 + list[y] : list[y] * 1e7 + list[x]; if (seen.has(key)) continue; seen.add(key);
    const e = (U, W) => [[0, 1], [1, 2], [2, 0]].some(([a, b]) => segTri(U.v[a], U.v[b], ...W.v));
    if (!e(A, B) && !e(B, A)) continue;
    const k2 = [A.m, B.m].sort().join(" x "); bins[k2] = (bins[k2] || 0) + 1; (where[k2] ||= []).push(A.v[0]);
  }
  const total = Object.values(bins).reduce((s, n) => s + n, 0);
  if (process.env.WHERE) for (const [k, l] of Object.entries(where)) { const lo = [0, 1, 2].map(m => Math.min(...l.map(q => q[m]))), hi = [0, 1, 2].map(m => Math.max(...l.map(q => q[m]))); console.log(`    ${k}: x ${lo[0].toFixed(2)}..${hi[0].toFixed(2)} y ${lo[1].toFixed(2)}..${hi[1].toFixed(2)} z ${lo[2].toFixed(2)}..${hi[2].toFixed(2)}`); }
  console.log(`${name.padEnd(10)} ${String(total).padStart(5)}  ` + Object.entries(bins).sort((a, b) => b[1] - a[1]).map(([k, n]) => `${k} ${n}`).join(", "));
}
