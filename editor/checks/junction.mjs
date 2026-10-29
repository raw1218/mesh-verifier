// Where the beak meets the face: explicit checks, each PASS or FAIL with its numbers.
//   node editor/checks/junction.mjs            (the editor's "Tripo match" sliders)
//   node editor/checks/junction.mjs defaults   (the default sliders: only the rig checks apply)
//
// Against the segmented Tripo raven (tripo_junction.json, from measure_tripo.py):
//   socket rim      where our skin meets the beak (the skin line) against the Tripo's socket: half-width, top, bottom,
//                   how far forward on top, how far back at the sides
//   beak tip, eye   the tip's place; the left eyeball's centre and radius
//   mouth corner    the gape's end, at the back of the Tripo's mouth-corner patch (hand tag)
// On our rig (any sliders):
//   eye clear       the eyeballs are outside the bills (their base is tucked under the skin, beakTuck)
//   bridge          the skin over the bill's top (bridgeFwd) goes with the upper bill: a thin layer on it, both its
//                   faces must move together
//   cheek muscle    the cheek joint (smile) moves the cheek: enough skin, centred on the cheek pad when there is one,
//                   none of it the socket's rim (the lids are their own pieces)
//   brows           the brow joints move the skin round the brow, none of the skin on the bills
import { build, DEFAULTS } from "../raven.js";
import { readFileSync } from "fs";

const here = new URL(".", import.meta.url);
const T = JSON.parse(readFileSync(new URL("tripo_junction.json", here), "utf8"));
// the editor's Tripo match, read from index.html so the two never drift apart
const html = readFileSync(new URL("../index.html", here), "utf8");
const src = html.slice(html.indexOf("const TRIPO_MATCH = {") + "const TRIPO_MATCH = ".length);
const MATCH = Function("return " + src.slice(0, src.indexOf("};") + 1))();
const useDefaults = process.argv[2] === "defaults";
const P = { ...DEFAULTS, ...(useDefaults ? {} : MATCH), detail: 0.04 };
const ch = build(P);
const J = Object.fromEntries(ch.joints.map((j, i) => [j.name, i]));
let fails = 0;
const check = (name, ok, detail) => { if (!ok) fails++; console.log(`${ok ? "PASS" : "FAIL"}  ${name.padEnd(34)} ${detail}`); };
const near = (name, ours, tripo, tol) => check(name, Math.abs(ours - tripo) <= tol, `ours ${ours.toFixed(3)}  tripo ${tripo.toFixed(3)}  (within ${tol})`);

if (!useDefaults) {
  console.log("-- against the Tripo raven");
  const sl = [...ch.info.mouth.skinLine.upper, ...ch.info.mouth.skinLine.lower];
  const ax = Math.max(...sl.map(p => Math.abs(p[0]))), y1 = Math.max(...sl.map(p => p[1])), y0 = Math.min(...sl.map(p => p[1]));
  const z1 = Math.max(...sl.map(p => p[2])), z0 = Math.min(...sl.map(p => p[2]));
  const tr = T.rim_pts, tmax = m => Math.max(...tr.map(p => m ? p[m] : Math.abs(p[m]))), tmin = m => Math.min(...tr.map(p => p[m]));
  near("socket rim: half-width", ax, tmax(0), 0.06);
  near("socket rim: top", y1, tmax(1), 0.06);
  near("socket rim: bottom", y0, tmin(1), 0.06);
  near("socket rim: forward on top (z)", z1, tmax(2), 0.08);
  near("socket rim: back at the sides (z)", z0, tmin(2), 0.06);
  const ub = ch.parts.find(p => p.name === "upperBeak_geo");
  let tip = [0, 0, -9]; for (let i = 0; i < ub.positions.length; i += 3) if (ub.positions[i + 2] > tip[2]) tip = [ub.positions[i], ub.positions[i + 1], ub.positions[i + 2]];
  near("beak tip: z", tip[2], T.beak.tip[2], 0.05);
  near("beak tip: y", tip[1], T.beak.tip[1], 0.05);
  const e = ch.joints[J.eye_L].at;
  check("eye: centre", Math.hypot(e[0] - T.eye.c[0], e[1] - T.eye.c[1], e[2] - T.eye.c[2]) <= 0.1,
        `ours ${e.map(v => v.toFixed(2)).join(", ")}  tripo ${T.eye.c.map(v => v.toFixed(2)).join(", ")}  (within 0.1)`);
  near("eye: radius", P.eyeSize, T.eye.r, 0.02);
  const c = ch.info.cornerL;
  check("mouth corner: at the back of the patch", c[2] >= T.corner.min[2] - 0.06 && c[2] <= T.corner.min[2] + 0.06 && c[1] >= T.corner.min[1] && c[1] <= T.corner.max[1],
        `gape end z ${c[2].toFixed(3)} y ${c[1].toFixed(3)}; patch z ${T.corner.min[2].toFixed(2)}..${T.corner.max[2].toFixed(2)}, y ${T.corner.min[1].toFixed(2)}..${T.corner.max[1].toFixed(2)}`);
}

console.log("-- the rig");
check("eye clear of the beak", ch.info.eyeClear > 0, `nearest bill vertex to the eyeball's surface ${ch.info.eyeClear.toFixed(3)} (> 0)`);
const h = ch.parts[0], nV = h.positions.length / 3;
const w = (v, j) => { let s = 0; for (let k = 0; k < 4; k++) if (h.joints[4 * v + k] === j) s += h.weights[4 * v + k]; return s; };
const pos = v => [h.positions[3 * v], h.positions[3 * v + 1], h.positions[3 * v + 2]];
// distance from a point to the upper bill's vertices (a grid)
const ubp = ch.parts.find(p => p.name === "upperBeak_geo").positions, lbp = ch.parts.find(p => p.name === "lowerBeak_geo").positions;
const grid = new Map(), cell = 0.08;
for (const [arr, tag] of [[ubp, "u"], [lbp, "l"]]) for (let i = 0; i < arr.length; i += 3) {
  const k = [arr[i], arr[i + 1], arr[i + 2]].map(c => Math.floor(c / cell)).join(","); if (!grid.has(k)) grid.set(k, []); grid.get(k).push([arr[i], arr[i + 1], arr[i + 2], tag]); }
const billDist = q => { const c = q.map(v => Math.floor(v / cell)); let best = [1e9, ""];
  for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (let d = -1; d <= 1; d++) for (const p of grid.get([c[0] + a, c[1] + b, c[2] + d].join(",")) || []) {
    const e = Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]); if (e < best[0]) best = [e, p[3]]; }
  return best; };
if (P.bridgeFwd > 0) {
  // the skin over the bill's top: head vertices in front of the face, above the gape, close over the upper bill
  let n = 0, sum = 0, lo = 1;
  for (let v = 0; v < nV; v++) { const q = pos(v); if (q[2] < ch.info.zb + 0.15 || q[2] > ch.info.zb + P.bridgeFwd - 0.05 || q[1] < 0.15) continue;
    const [d, tag] = billDist(q); if (d > 0.08 || tag !== "u") continue; const wu = w(v, J.upperBeak); n++; sum += wu; lo = Math.min(lo, wu); }
  check("bridge goes with the upper bill", n > 0 && sum / n > 0.9, `${n} skin vertices on the bill's top: mean upper-bill weight ${(sum / Math.max(n, 1)).toFixed(2)}, least ${lo.toFixed(2)} (mean > 0.9)`);
}
for (const side of ["L"]) {
  const cj = J[`cheek_${side}`], eye = ch.joints[J[`eye_${side}`]].at, socketR = P.eyeSize * 1.2;
  let n = 0, onRim = 0, cen = [0, 0, 0], ws = 0;
  for (let v = 0; v < nV; v++) { const wc = w(v, cj); if (wc < 0.05) continue; const q = pos(v);
    if (wc > 0.3) n++; if (Math.hypot(q[0] - eye[0], q[1] - eye[1], q[2] - eye[2]) < socketR) onRim++;
    for (let k = 0; k < 3; k++) cen[k] += wc * q[k]; ws += wc; }
  cen = cen.map(c => c / (ws || 1));
  check("cheek muscle moves the cheek", n >= 50, `${n} skin vertices with cheek weight > 0.3 (>= 50); its middle at ${cen.map(c => c.toFixed(2)).join(", ")}`);
  check("cheek muscle keeps off the eye", onRim === 0, `${onRim} cheek-weighted vertices inside the eye's socket (0)`);
}
for (const side of ["L"]) {
  const bj = J[`brow_${side}`]; let n = 0, onBill = 0;
  for (let v = 0; v < nV; v++) { const wb = w(v, bj); if (wb < 0.05) continue; if (wb > 0.3) n++; if (billDist(pos(v))[0] < 0.05) onBill++; }
  check("brow moves the skin round it", n >= 50, `${n} skin vertices with brow weight > 0.3 (>= 50)`);
  check("brow keeps off the skin on the bill", onBill === 0, `${onBill} brow-weighted vertices within 0.05 of a bill (0)`);
}
console.log(fails ? `${fails} check(s) failed` : "all checks pass");
process.exitCode = fails ? 1 : 0;
