// The mouth of a beaked head: two parts, the beak (parts/beak.js) and the mouth corners, attached explicitly.
// Nothing here knows the rest of the head: the head passes its face (as a field) and where the beak sits.
//
//   skin line     where the skin reaches the beak, with the mouth closed: along each line of the bill's surface,
//                 the point where it comes out of the face. The face's skin runs onto the bill over it (the join).
//   attach point  where the skin line crosses the cutting edge, one per side: where the corner starts.
//   gape line     from the attach point back along the face to the corner's end, where the corner joint sits. The
//                 slit is cut along it; the corner's moving area is a band along it, held at the beak end and
//                 free at the corner's end.
//   corners       (can be off: then the gape ends at the attach point, a plain beak with nothing to smile with)
//                 each its own mesh: the soft skin of the gape. It owns the place where the skin meets the beak
//                 there (no crease, as on a bird): from a little way forward on the cutting edges, over the attach
//                 point, back along the gape line, round at the corner's end; on the beak it goes with the bills,
//                 on the face with the face, so it stretches when the beak opens and bends when it smiles.
//                 Under it the face is slit along the line (hidden), so the face opens with the beak too.
//   join          the face's skin onto the bill: from a little way out on the face, round the corner, to a little
//                 way along the bill, a hair off both, the face's normals at one end and the bill's at the other.
//
// Head space: +Y up, the head faces +Z, the character's left is +X.

import { smoothstep, mix } from "../sdf.js";
import * as BEAK from "./beak.js";

export const PARAMS = [
  // key, label, min, max, default
  // key, label, min, max, default[, "check": an on/off box]
  ["corners", "corners (off: a beak, no lips to smile with)", 0, 1, 1, "check"],
  ["softBeak", "beak bends with the corner", 0, 1, 0, "check"],
  ["bendLen", "beak bend: how far along the beak", 0.05, 0.8, 0.3],
  ["gapeBack", "corner: how far back from the beak", 0.05, 0.7, 0.3],
  ["gapeCurve", "corner: turns down", -0.15, 0.25, 0.06],
  ["cornerSize", "corner: area that moves", 0.08, 0.4, 0.18],
  ["joinWidth", "skin onto the beak: how far", 0.06, 0.4, 0.16],
];

// The lip editor: the corner mesh is two lips, an upper and a lower, each in three parts along it (as MetaHuman's
// lips are split up): on the beak (goes with the bills), the middle (its own joint, lipUpper_* / lipLower_*: raised,
// lowered, pressed) and the corner (the corner joint: smile, frown, stretch).
export const LIP_PARAMS = [
  ["upperFull", "upper lip: fullness", 0, 0.05, 0.012],
  ["lowerFull", "lower lip: fullness", 0, 0.05, 0.012],
  ["upperWidth", "upper lip: width", 0.02, 0.14, 0.07],
  ["lowerWidth", "lower lip: width", 0.02, 0.14, 0.07],
  ["fullBeak", "fullness at the beak (x)", 0, 2, 1],
  ["fullCorner", "fullness at the corner (x)", 0, 2, 1],
  ["lipReach", "how far onto the beak", 0.05, 1, 1],
  ["lipRoll", "roll out (fullness to the outer edge)", -1, 1, 0],
  ["lipMid", "middle part: how far back", 0.2, 0.9, 0.55],
];

const add = (a, b, s = 1) => [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = v => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const mirror = v => [-v[0], v[1], v[2]];
const grad = (f, q) => { const f0 = f(...q); return [0, 1, 2].map(k => { const r = [...q]; r[k] += 1e-4; return (f(...r) - f0) / 1e-4; }); };
const onSurface = (f, q) => {
  for (let k = 0; k < 8; k++) { const g = grad(f, q); q = add(q, g, -f(...q) / (dot(g, g) || 1)); }
  return q;
};
// onto the surface straight out to the side, keeping its height and depth (so a line keeps its shape on the face)
const toSide = (f, q) => {
  let a = 0, b = 2;
  if (f(q[0] >= 0 ? b : -b, q[1], q[2]) < 0) return onSurface(f, q);
  for (let k = 0; k < 30; k++) { const m = (a + b) / 2; if (f(Math.sign(q[0] || 1) * m, q[1], q[2]) > 0) b = m; else a = m; }
  return [Math.sign(q[0] || 1) * b, q[1], q[2]];
};
const NA = BEAK.N_ARCH, NG = 16, NJ = 10;
const BEND = 0.3;          // how much of the corner's move the beak's base takes, where the skin meets it (softBeak)

// p: the mouth's sliders (PARAMS) with the beak's; bp: the beak's own params; place: where the beak sits
// ({ S: scale, by: height, zb: where it leaves the face }); face: the face as a field before anything is cut
// out of it; gap: how wide the closed gape is.
export function beakMouth({ p, bp, place, face, gap }) {
  const bc = BEAK.curves(bp), { S, by, zb } = place, L = S * bc.L;
  const toHead = (lx, ly, lz) => [S * ly, by + S * lz, zb + S * lx];
  const billAt = (which, t, i) => toHead(...BEAK.archPoint(bc, which, t, i));
  const bill = Object.fromEntries(["upper", "lower"].map(w => {
    const f = BEAK.billField(bp, w);
    return [w, (x, y, z) => S * f((z - zb) / S, x / S, (y - by) / S)];
  }));

  // ---- the skin line: where each line of the bill comes out of the face ----
  const skinLine = {};
  for (const [which, sgn] of [["upper", 1], ["lower", -1]]) {
    const tEnd = which === "upper" ? 1 : bc.lowerEnd;
    skinLine[which] = [...Array(NA).keys()].map(i => {
      let a = 0, b = tEnd * 0.8;
      if (face(...billAt(which, 0, i)) < 0) {
        for (let t = 0; t < b; t += 0.01) if (face(...billAt(which, t, i)) > 0) { b = t; break; }
        a = Math.max(0, b - 0.01);
        for (let k = 0; k < 20; k++) { const m = (a + b) / 2; if (face(...billAt(which, m, i)) > 0) b = m; else a = m; }
      } else b = 0;                                // (the base already shows: the skin line is the base)
      const t = b, E = billAt(which, t, i);
      // the bill's outward normal there, and the way along the bill
      const i0 = Math.max(0, i - 0.05), i1 = Math.min(NA - 1, i + 0.05);
      const along = add(billAt(which, t + 1e-3, i), E, -1);
      let nb = norm(cross(along, add(billAt(which, t, i1), billAt(which, t, i0), -1)));
      const g = bc.gapeZ(t) + bc.hookDrop(t), d = which === "upper" ? bc.upperDepth(t) : bc.lowerDepth(t);
      if (dot(nb, add(E, toHead(t * bc.L, 0, g + sgn * d * 0.4), -1)) < 0) nb = nb.map(c => -c);
      return { t, E, nb, along: norm(along) };
    });
  }

  // ---- the attach points: the skin line on the cutting edge (upper arch: i = 0 is the left) ----
  const attachL = skinLine.upper[0].E;

  // ---- the gape line: from the attach point back along the face, turning down, to the corner's end ----
  const on = p.corners >= 0.5;
  // (it leaves the attach point the way the cutting edge goes, so no elbow there, and bends down gently over its
  // whole length: a curve from the attach point, aimed back along the edge, to the end)
  const back = norm(add(billAt("upper", skinLine.upper[0].t, 0), billAt("upper", skinLine.upper[0].t + 0.01, 0), -1));
  const endL = add(attachL, [0, -p.gapeCurve, -p.gapeBack]), midL = add(attachL, back, p.gapeBack * 0.5);
  const lineL = on ? [...Array(NG + 1).keys()].map(k => {
    const u = k / NG;
    return k === 0 ? attachL : toSide(face, [0, 1, 2].map(m => (1 - u) ** 2 * attachL[m] + 2 * u * (1 - u) * midL[m] + u * u * endL[m]));
  }) : [attachL];
  const lineR = lineL.map(mirror);
  const cornerL = lineL[lineL.length - 1], cornerR = mirror(cornerL);

  // the gape's height at depth z: the beak's own in front of the attach point, the gape line's behind it
  const gapeY = z => {
    if (z >= attachL[2]) return by + S * bc.gapeZ(Math.min(1, Math.max(0, (z - zb) / L)));
    for (let k = 1; k < lineL.length; k++) if (z >= lineL[k][2]) {
      const a = lineL[k - 1], b = lineL[k], s = (z - b[2]) / ((a[2] - b[2]) || 1);
      return mix(b[1], a[1], s);
    }
    return cornerL[1];
  };
  // the slit: a slab at the gape's height, as far back as the corner's end at the sides (deeper in the middle,
  // inside the head, for the mouth)
  // (behind the attach points it runs on under the corner, which covers it, so the face can open when the beak
  // does while the corner stretches over the gap)
  const cx = Math.abs(cornerL[0]), deep = zb - 0.75;
  const slit = (x, y, z) => {
    const back = mix(deep, cornerL[2], smoothstep(0.5 * cx, 0.85 * cx, Math.abs(x)));
    return Math.max(Math.abs(y - gapeY(z)) - gap / 2, back - z, Math.abs(x) - cx - 0.15);
  };
  // the corner's area: a band along the gape line; u: how far along it (0 at the beak, 1 at the corner's end)
  const cornerAt = (x, y, z) => {
    if (!on) return { d: 1e9, u: 0, w: 0 };
    const line = x >= 0 ? lineL : lineR;
    let best = 1e9, bu = 0;
    for (let k = 0; k < NG; k++) {
      const a = line[k], ab = add(line[k + 1], a, -1), q = [x, y, z];
      const s = Math.min(1, Math.max(0, dot(add(q, a, -1), ab) / (dot(ab, ab) || 1)));
      const d = Math.hypot(...add(q, add(a, ab, s), -1));
      if (d < best) { best = d; bu = (k + s) / NG; }
    }
    // (the corner's end can move a lot; the beak end not at all with a rigid beak, as much as the beak's base with
    // a bending one, so they move together; past the end it fades out with distance)
    // (most of it at the corner's end: a smile happens at the corner, the lip near the beak hardly moves)
    const ramp = smoothstep(0.3, 1, bu), held = p.softBeak >= 0.5 ? mix(BEND, 1, ramp) : ramp;
    return { d: best, u: bu, w: (1 - smoothstep(0, p.cornerSize, best)) * held, upper: y >= gapeY(z) };
  };
  // the lips' middle parts (the lip editor): on each side of the gape, from a little behind the attach point to
  // lipMid along the line, a band as wide as that lip; upper: which lip. Each has its own joint (lipJoints).
  const lipAt = (x, y, z) => {
    const c = cornerAt(x, y, z);
    if (!on) return { w: 0, upper: true };
    const band = 1.6 * (c.upper ? p.upperWidth : p.lowerWidth);
    const zone = smoothstep(-0.05, 0.2, c.u) * (1 - smoothstep(p.lipMid, Math.min(1.2, p.lipMid + 0.35), c.u));
    return { w: zone * (1 - smoothstep(0, band, c.d)), upper: c.upper };
  };
  // where the lips' middle joints sit: halfway along the middle part, on each lip (left; the right is the mirror)
  const lipJoints = on ? (() => {
    const u = p.lipMid / 2, k = Math.min(NG - 1, Math.floor(u * NG)), G = add(lineL[k], add(lineL[k + 1], lineL[k], -1), u * NG - k);
    return { upperL: add(G, [0, 1, 0], p.upperWidth / 2), lowerL: add(G, [0, 1, 0], -p.lowerWidth / 2) };
  })() : null;

  // ---- how the beak moves: rigid with its bill, or (softBeak) its base bending with the corner ----
  // With softBeak on, the rear of each bill's cutting edge goes partly with that side's corner joint: most where the
  // skin meets the beak, fading out along the bill over bendLen and up the bill's side from the cutting edge.
  // joints: { upper, lower, cornerL, cornerR }. Returns [[joint, w], ...].
  const tA = skinLine.upper[0].t;
  function billWeights(which, q, joints) {
    if (!on || !(p.softBeak >= 0.5)) return [[joints[which], 1]];
    const lx = (q[2] - zb) / S, ly = q[0] / S, lz = (q[1] - by) / S, t = Math.max(0, lx / bc.L);
    const along = Math.max(0, (t - tA) * L);                     // how far forward of the skin line
    const d = Math.max(1e-3, which === "upper" ? bc.upperDepth(Math.min(t, 1)) : bc.lowerDepth(Math.min(t, bc.lowerEnd)));
    const up = Math.abs(lz - (bc.gapeZ(Math.min(t, 1)) + bc.hookDrop(Math.min(t, 1)))) / d;   // up the side, 0 at the edge
    // (0.9: as the face's corner area, raven.js)
    const w = 0.9 * BEND * (1 - smoothstep(0, p.bendLen, along)) * (1 - smoothstep(0.1, 0.8, up));
    return [[joints[which], 1 - w], [ly >= 0 ? joints.cornerL : joints.cornerR, w]];
  }
  // a mix of weight lists, as four [joint, w] (the largest, summing to 1)
  function blend(...parts) {
    const ws = [];
    for (const [list, f] of parts) for (const [j, w] of list) {
      const e = ws.find(x => x[0] === j); if (e) e[1] += w * f; else ws.push([j, w * f]);
    }
    ws.sort((a, b) => b[1] - a[1]);
    const top = ws.slice(0, 4), sum = top.reduce((s, x) => s + x[1], 0) || 1;
    while (top.length < 4) top.push([0, 0]);
    return top.map(([j, w]) => [j, w / sum]);
  }

  // ---- the join: built once the face's skin and weights exist ----
  // skin: the finished face's field (what the join lands on); weights(x, y, z): the face's [[joint, w] x4] there;
  // joints: { upper, lower } the joint each bill moves with
  function join({ skin, weights, joints }) {
    const out = [];
    for (const [which, sgn] of [["upper", 1], ["lower", -1]]) {
      const tEnd = which === "upper" ? 1 : bc.lowerEnd;
      const cols = skinLine[which].map(({ t, E, nb, along }, i) => {
        // at the cutting edges the two halves part, so the gape runs on into the face; there the join lands on
        // the face as if the slit were not cut, so it bridges it
        const edge = 1 - smoothstep(0, 4, Math.min(i, NA - 1 - i));
        const land = (x, y, z) => mix(skin(x, y, z), face(x, y, z), edge);
        const nf = norm(grad(face, E));
        let dir = add(nb, nf, -dot(nb, nf));
        dir = Math.hypot(...dir) > 1e-3 ? norm(dir) : along.map(c => -c);
        // (and it narrows to nothing there: the gape behind the attach point is the corner's, not the join's)
        let jw = mix(Math.min(0.06, p.joinWidth), p.joinWidth, on ? smoothstep(0.5, 2.5, Math.min(i, NA - 1 - i)) : 1);
        // (under the jaw it stays on the bill's sleeve, which goes rigidly with the bill: further out the skin's
        // weights change fast as the jaw opens (jaw, throat, head) and a strip a hair over it cannot follow)
        if (which === "lower") jw = Math.min(jw, 0.07);
        const P1 = onSurface(land, add(E, dir, jw));
        const n1 = norm(add(norm(grad(skin, P1)).map(c => c * (1 - edge)), norm(grad(face, P1)), edge));
        const P0 = billAt(which, Math.min(t + p.joinWidth / L, tEnd * 0.85), i);
        return { P0, E, P1, nb, nf: n1, edge };
      });
      const pos = [], jj = [], ww = [];
      for (let s = 0; s <= NJ; s++) for (let i = 0; i < NA; i++) {
        const u = s / NJ, { P0, E, P1, nb, nf, edge } = cols[i];
        const q = [0, 1, 2].map(m => (1 - u) ** 2 * P0[m] + 2 * u * (1 - u) * E[m] + u * u * P1[m]);
        const n = norm(nb.map((c, m) => mix(c, nf[m], u)));
        let v = add(add(q, n, 0.006),[0, sgn * gap / 2 * edge * (1 - (1 - u) ** 2), 0]);
        // (a wide join over a rounded face or bill would sag into it: keep it a hair off both)
        const land = (x, y, z) => mix(skin(x, y, z), face(x, y, z), edge);
        for (let k = 0; k < 4; k++) {
          if (u > 0.3) { const d = land(...v); if (d < 0.006) v = add(v, norm(grad(land, v)), 0.006 - d); }
          if (u < 0.7) { const d = bill[which](...v); if (d < 0.006) v = add(v, norm(grad(bill[which], v)), 0.006 - d); }
        }
        pos.push(...v);
        // the bill's joint at the bill end, the face's own weights at the face end
        const sig = smoothstep(0, 1, u);
        for (const [j, w] of blend([weights(...v), sig], [billWeights(which, v, joints), 1 - sig])) { jj.push(j); ww.push(w); }
      }
      const idx = [];
      for (let s = 0; s < NJ; s++) for (let i = 0; i < NA - 1; i++) {
        const a = s * NA + i, b = a + 1, c = a + NA, d = c + 1;
        idx.push(a, c, b, b, c, d);
      }
      // faces outward (away from the bill)
      const T = idx.slice(3 * NA, 3 * NA + 3).map(v => pos.slice(3 * v, 3 * v + 3));
      if (dot(cross(add(T[1], T[0], -1), add(T[2], T[0], -1)), cols[NA >> 1].nb) < 0)
        for (let k = 0; k < idx.length; k += 3) [idx[k + 1], idx[k + 2]] = [idx[k + 2], idx[k + 1]];
      // normals: the bill's at the bill end, the face's at the face end, the strip's own between
      const P = (s, i) => pos.slice(3 * (s * NA + i), 3 * (s * NA + i) + 3);
      const nrm = [];
      for (let s = 0; s <= NJ; s++) for (let i = 0; i < NA; i++) {
        let n = norm(cross(add(P(Math.min(s + 1, NJ), i), P(Math.max(s - 1, 0), i), -1),
                           add(P(s, Math.min(i + 1, NA - 1)), P(s, Math.max(i - 1, 0)), -1)));
        if (dot(n, cols[i].nb) + dot(n, cols[i].nf) < 0) n = n.map(c => -c);
        const u = s / NJ, w0 = 1 - smoothstep(0, 0.25, u), w1 = smoothstep(0.75, 1, u);
        nrm.push(...norm(n.map((c, m) => c + 4 * w0 * cols[i].nb[m] + 4 * w1 * cols[i].nf[m])));
      }
      out.push({ which, positions: pos, indices: idx, normals: nrm, joints: jj, weights: ww, rows: NJ + 1, cols: NA });
    }
    return out;
  }

  // ---- the corners: one mesh per side ----
  // The corner owns the place where the skin meets the beak at the gape (on a bird there is no crease there: the
  // soft skin of the gape runs on along the bill's cutting edges). So it starts a little way forward on the
  // cutting edges, runs back over the attach point and along the gape line to the corner's end, and closes round
  // there. It lies on the beak and face smoothed together (the crease between them filled), a hair over it; an
  // upper and a lower half with the gape between. On the beak it goes with the bills, on the face with the face.
  // weights(x, y, z): the face's [[joint, w] x4] there; joints: { upper, lower } the joint each bill moves with
  function corners({ weights, joints }) {
    if (!on) return [];
    const NU = 48, NV = 20, eps = 0.012, k = 0.07;
    const land = (x, y, z) => { const fu = bill.upper(x, y, z), fl = bill.lower(x, y, z), f = face(x, y, z);
                                const bu = Math.min(fu, fl); const h = Math.max(0, k - Math.abs(f - bu)) / k;
                                return Math.min(f, bu) - h * h * k / 4; };   // (beak and face, smoothed together)
    // it starts where the skin meets the beak: the join's front edge, on the cutting edge
    // (or less of the way, lipReach)
    const tF = Math.min(tA + p.lipReach * p.joinWidth / L, 0.85);
    const lipW = Math.max(p.upperWidth, p.lowerWidth);
    // (the round end as long as the lips are wide, however short the gape line: capped, a short line squashed it flat
    // and the lips folded over each other there)
    const cap = lipW * 0.9 / p.gapeBack;
    const tEnd = norm(add(lineL[NG], lineL[NG - 1], -1));
    // s: -1 at the front (on the cutting edge), 0 at the attach point, 1 at the corner's end, then round the end
    const lineAt = s => {
      if (s < 0) return billAt("upper", tA - s * (tF - tA), 0);
      if (s > 1) return toSide(face, add(cornerL, tEnd, (s - 1) * p.gapeBack));
      const kk = Math.min(NG - 1, Math.floor(s * NG)), f = s * NG - kk;
      return add(lineL[kk], add(lineL[kk + 1], lineL[kk], -1), f);
    };
    const s0 = -1, s1 = 1 + cap;
    const stations = [...Array(NU + 1).keys()].map(n => {
      const s = s0 + (s1 - s0) * n / NU, G = lineAt(s);
      const T = norm(add(lineAt(Math.min(s1, s + 0.01)), lineAt(Math.max(s0, s - 0.01)), -1));
      const N = norm(grad(land, G));
      let B = norm(cross(N, T));
      if (B[1] < 0) B = B.map(c => -c);           // (B points up, across the gape)
      // how much corner there is: round at the front (where it meets the beak) and at the end, full between
      const fc = Math.min(0.3, lipW * 0.9 / (L * (tF - tA) || 1));
      const size = (s < -1 + fc ? Math.sqrt(Math.max(0, 1 - ((s + 1 - fc) / fc) ** 2)) : 1)
                 * (s > 1 ? Math.sqrt(Math.max(0, 1 - ((s - 1) / cap) ** 2)) : 1);
      return { s, G, B, size };
    });
    // two lips, parted along the gape line: columns 0..H the lower lip (v -1 to 0), H+1..2H+1 the upper (0 to 1).
    // Closed they meet in a fine line; open, each goes with its own side and the corner opens.
    const H = NV / 2, C = 2 * H + 2, vOf = j => (j <= H ? -1 + j / H : (j - H - 1) / H), sideOf = j => (j <= H ? -1 : 1);
    const pos = [], nrm0 = [], jj = [], ww = [], jjR = [], wwR = [];
    for (const { s, G, B, size } of stations) for (let j = 0; j < C; j++) {
      // each lip its own width and fullness; fullness from the beak's to the corner's along it
      const v = vOf(j), side = sideOf(j), w = (side > 0 ? p.upperWidth : p.lowerWidth) * size;
      const full = (side > 0 ? p.upperFull : p.lowerFull) * size * mix(p.fullBeak, p.fullCorner, smoothstep(-1, 1, s));
      // (across the lip a bump, fullest halfway; rolled out, fullest nearer its outer edge, in, nearer the gape)
      const across = Math.pow(Math.abs(Math.sin(Math.PI * Math.pow(Math.abs(v), Math.pow(2, p.lipRoll)))), 0.7);
      // a sheet across the gape line: pushed out where it would be inside, but bridging any step or groove (the
      // lower bill sits a little inside the upper's edge), so it never sinks into one
      let q = add(onSurface(land, G), B, v * w + side * 0.002);
      if (land(...q) < 0) q = onSurface(land, q);
      const N = norm(grad(land, q));
      // (each lip moves as the skin on its own side of the gape does)
      const at0 = add(q, B, side * (gap / 2 + 0.01) * (1 - Math.abs(v)) * (1 - smoothstep(0.7, 1, s)));
      // (but at the corner's end both lips move as one: one corner, where they meet, not one per lip)
      q = add(q, N, eps + full * across);
      // (never into a bill)
      for (const f of [bill.upper, bill.lower]) { const d = f(...q); if (d < eps) q = add(q, norm(grad(f, q)), eps - d); }
      pos.push(...q); nrm0.push(N);
      // on the beak, the bills' joints (upper half, lower half); on the face, the face's weights; between, both
      // (the beak's weights as the beak itself has them: rigid, or bending with the corner)
      const onBeak = 1 - smoothstep(0, 0.3, s);
      for (const [out_j, out_w, at] of [[jj, ww, at0], [jjR, wwR, mirror(at0)]]) {
        const ws = blend([weights(...at), 1 - onBeak], [billWeights(side > 0 ? "upper" : "lower", at, joints), onBeak]);
        for (const [jn, wt] of ws) { out_j.push(jn); out_w.push(wt); }
      }
    }
    const R = C, idx = [];
    for (let n = 0; n < NU; n++) for (let j = 0; j < C - 1; j++) {
      if (j === H) continue;                      // (the lips' parting)
      const a = n * R + j, b = a + 1, c = a + R, d = c + 1;
      idx.push(a, b, c, b, d, c);
    }
    // faces outward; normals the surface's own, the land's at the edges (so it settles onto beak and face)
    const P = (n, j) => pos.slice(3 * (n * R + j), 3 * (n * R + j) + 3);
    const T0 = [P(NU >> 1, 2), P(NU >> 1, 3), P((NU >> 1) + 1, 2)];
    if (dot(cross(add(T0[1], T0[0], -1), add(T0[2], T0[0], -1)), nrm0[(NU >> 1) * R + 2]) < 0)
      for (let q = 0; q < idx.length; q += 3) [idx[q + 1], idx[q + 2]] = [idx[q + 2], idx[q + 1]];
    const nrm = [];
    for (let n = 0; n <= NU; n++) for (let j = 0; j < C; j++) {
      const jp = j === H || j === C - 1 ? j : j + 1, jm = j === H + 1 || j === 0 ? j : j - 1;
      let m = norm(cross(add(P(Math.min(n + 1, NU), j), P(Math.max(n - 1, 0), j), -1), add(P(n, jp), P(n, jm), -1)));
      const f = nrm0[n * R + j];
      if (dot(m, f) < 0) m = m.map(c => -c);
      if (!isFinite(m[0]) || Math.hypot(...m) < 0.5) m = f;
      const e = smoothstep(0.7, 1, Math.abs(vOf(j)));
      nrm.push(...norm(m.map((c, q) => c + 4 * e * f[q])));
    }
    // the left as built, the right its mirror image (winding turned so it still faces out)
    const flip = a => a.map((c, q) => (q % 3 === 0 ? -c : c));
    const idxR = [];
    for (let q = 0; q < idx.length; q += 3) idxR.push(idx[q], idx[q + 2], idx[q + 1]);
    return [{ side: "L", positions: pos, normals: nrm, joints: jj, weights: ww, indices: idx },
            { side: "R", positions: flip(pos), normals: flip(nrm), joints: jjR, weights: wwR, indices: idxR }];
  }

  return { on, corners, billWeights, skinLine, attach: { L: attachL, R: mirror(attachL) }, gapeLine: { L: lineL, R: lineR },
           corner: { L: cornerL, R: cornerR }, gapeY, slit, cornerAt, lipAt, lipJoints, join, deep };
}
