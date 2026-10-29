// The stylized raven head, built from parts. Each part makes its own shape and its own rig: the joints that move it,
// the skin weights that tie its vertices to them, and the controls (ARKit-style names) with what they do. Sliders
// change the shapes; the joints' places come from the same numbers, so the rig always fits.
//
// Units: the cranium is about 1 across. glTF axes: +Y up, the head faces +Z, the character's left is +X.

import { clamp, smoothstep, mix, sunion, ssub, ellipsoid, sphere, mesh, sphereBand, lidShell } from "./sdf.js";
import * as BEAK from "./parts/beak.js";
import * as MOUTH from "./parts/mouth.js";
import * as SKULL from "./parts/skull.js";

export const PARAMS = [
  // group, key, label, min, max, value
  // the skull: the first layer (parts/skull.js); the skin is it grown by the thickness at each point
  ...SKULL.PARAMS.map(([k, label, min, max, v]) => ["Skull", k, label, min, max, v]),
  ...SKULL.THICK_PARAMS.map(([k, label, min, max, v]) => ["Skin thickness", k, label, min, max, v]),
  ["Head", "throatOn", "throat", 0, 1, 1, "check"],
  ["Head", "throat", "throat fullness", 0, 1, 0.55],
  ["Head", "throatDrop", "throat: hangs lower", 0, 1, 0],
  ["Head", "jawLine", "jaw line into the neck", 0, 1, 0],
  ["Neck", "neckOn", "neck", 0, 1, 1, "check"],
  ["Neck", "neckWidth", "width", 0.4, 1.3, 0.58],
  ["Neck", "neckFlare", "wider toward the bottom", 0, 1, 0],
  ["Neck", "neckTilt", "leans forward", -0.2, 0.8, 0.2],
  ["Neck", "neckBack", "set back", 0, 0.8, 0.35],
  ["Head", "face", "face: skin onto the beak", 0, 1, 0.6],
  // where the face meets the beak (measured against the Tripo raven: scratchpad junction_*.py/mjs)
  ["Beak junction", "bridgeFwd", "skin forward over the bill's top", 0, 0.8, 0],
  ["Beak junction", "cheekPad", "cheek pads beside the beak", 0, 1, 0],
  ["Beak junction", "cheekPadHeight", "cheek pads: height", -0.3, 0.4, 0],
  // the beak's own shape: the shared beak's sliders (parts/beak.js), as "beak_<key>"
  // (wider at the base than the beak editor allows: the Tripo raven's bill is as wide as the face where they meet)
  ...BEAK.PARAMS.map(([k, label, min, max, v]) => ["Beak", "beak_" + k, label, min, k === "width" ? 0.7 : max, v]),
  // how this head carries it
  ["Beak", "beakSize", "size on this head", 1.2, 2.8, 1.9],
  ["Beak", "beakHeight", "height on the face", -0.4, 0.3, -0.12],
  // (its base sits under the face's skin: set forward, it stays clear of the eyes behind it)
  ["Beak", "beakForward", "set forward on the face", -0.2, 0.5, 0],
  ["Beak", "beakTuck", "base: how far under the skin", 0.05, 1, 1],
  // the mouth: the beak and the corners, and how they attach (parts/mouth.js)
  ...MOUTH.PARAMS.map(([k, label, min, max, v, kind]) => ["Mouth", k, label, min, max, v, kind]),
  ...MOUTH.LIP_PARAMS.map(([k, label, min, max, v, kind]) => ["Lips", k, label, min, max, v, kind]),
  ["Eyes", "eyeSize", "size", 0.1, 0.34, 0.19],
  ["Eyes", "eyeSide", "to the side (deg)", 20, 80, 58],
  ["Eyes", "eyeHeight", "height", -0.2, 0.5, 0.16],
  ["Eyes", "eyeForward", "forward", -0.2, 0.7, 0.28],
  ["Eyes", "eyeBulge", "bulge", 0, 0.8, 0.35],
  ["Eyes", "irisSize", "iris", 0.3, 0.95, 0.72],
  ["Eyes", "lidOpen", "lids open", 0.2, 1, 0.62],
  ["Brows", "browRidge", "ridge in the skin", 0, 0.3, 0.05],
  ["Brows", "browOnRim", "ridge on the socket's rim (the rim is the brow)", 0, 1, 0, "check"],
  ["Brows", "browOverhang", "ridge: out over the eye", 0, 1, 0],
  ["Brows", "browMass", "ridge: mass up the head", 0, 1, 0],
  ["Brows", "browOn", "strips over the ridge", 0, 1, 1, "check"],
  ["Brows", "browHeight", "height over the eye (deg)", 10, 70, 32],
  ["Brows", "browLength", "length (deg)", 30, 140, 90],
  ["Brows", "browWidth", "width", 0.03, 0.2, 0.09],
  ["Brows", "browThick", "thickness", 0.02, 0.1, 0.045],
  ["Brows", "browTilt", "tilt (inner end down)", -40, 40, 12],
  ["Brows", "browGap", "gap over the skin", 0.01, 0.08, 0.04],
  ["Beak", "nostril", "nostril size", 0, 1, 0.5],
  ["Beak", "nostrilAlong", "nostril: how far along the bill", 0.05, 0.6, 0.13],
  ["Build", "detail", "mesh cell size", 0.02, 0.07, 0.04],
];

export const DEFAULTS = Object.fromEntries(PARAMS.map(p => [p[1], p[5]]));

// colours per part (also the partition: every vertex belongs to exactly one). Meshes are named <part>_geo, so no
// mesh shares a name with a joint (the verifier finds nodes by name).
export const PART_COLOURS = {
  head: [0.32, 0.33, 0.37], throat: [0.28, 0.29, 0.34], upper_beak: [0.05, 0.05, 0.06], lower_beak: [0.07, 0.07, 0.075],
  mouth: [0.36, 0.17, 0.19], mouth_corner: [0.85, 0.45, 0.35], nostril: [0.08, 0.08, 0.09], socket: [0.42, 0.36, 0.4],
  eyeball: [0.93, 0.92, 0.88], iris: [0.35, 0.22, 0.1], pupil: [0.02, 0.02, 0.02],
  lid_upper: [0.38, 0.38, 0.44], lid_lower: [0.36, 0.36, 0.42], brow: [0.12, 0.12, 0.14],
};

const norm = v => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const add = (a, b, s = 1) => [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];
const mirror = v => [-v[0], v[1], v[2]];
const deg = Math.PI / 180;

// a right side frame from a left one: mirrored, with x turned round so it stays right-handed. A rotation on the
// right is then the left's with y and z angles negated; a translation, the left's with x negated.
const mirrorFrame = ([x, y, z]) => [mirror(x).map(c => -c), mirror(y), mirror(z)];
const mirrorRot = r => [r[0], -r[1], -r[2]];
const mirrorMove = t => [-t[0], t[1], t[2]];

// opts.skull: also mesh the skull layer (for the editor's view; it isn't one of the exported parts)
export function build(P, opts = {}) {
  const p = { ...DEFAULTS, ...P };
  // how far the head reaches: the braincase plus its skin (parts/skull.js)
  const R = [p.skullWidth + p.thickBase, p.skullHeight + p.thickBase, p.skullLength + p.thickBase];

  // ---- the beak: the shared beak (parts/beak.js), the same bills the skull's beak editor makes, placed on
  // the face and scaled. The face's skin runs onto the bill's base like a sleeve, just clear of it.
  // (beak space -> head space: the beak's x is forward, its z up, its y the head's x)
  const zb = R[2] * 0.88 + p.beakForward;         // where the beak leaves the face
  const S = p.beakSize, by = p.beakHeight;
  // (size 1: the base, hook and hinges stay put, so the length slider only grows the bill outward)
  const bp = { ...Object.fromEntries(BEAK.PARAMS.map(([k]) => [k, p["beak_" + k]])), size: 1 };
  const bc = BEAK.curves(bp);
  const billU = BEAK.billField(bp, "upper", { back: false }), billL = BEAK.billField(bp, "lower", { back: false });
  const local = (x, y, z) => [(z - zb) / S, x / S, (y - by) / S];
  const toHead = (lx, ly, lz) => [S * ly, by + S * lz, zb + S * lx];
  const clear = 0.03;                             // the sleeve's gap over the bill
  const fBillU = (x, y, z) => S * billU(...local(x, y, z));
  const fBillL = (x, y, z) => S * billL(...local(x, y, z));
  const L = S * bc.L;                             // the bill's length in head units
  const halfW = t => S * bc.width(t) / 2;
  const gap = Math.max(0.06, p.detail * 1.6);      // the closed gape is a slit this wide (the mesher must see it)

  // ---- the skull (parts/skull.js): the bone the beak is set into ----
  const H = BEAK.hinges(bc);                      // where the bills turn, as in the beak editor
  const hinge = toHead(...H.jaw), fore = toHead(...H.fore);
  const sk = SKULL.skull({ p, beak: { S, by, zb, bc }, hinge });

  // ---- eyes: on the skin over the skull, along a direction to the side ----
  const eyeDir = norm([Math.sin(p.eyeSide * deg), 0, Math.cos(p.eyeSide * deg)]);
  const origin = [0, p.eyeHeight, p.eyeForward];
  // where the ray from inside leaves the skin (the last crossing, out to beyond the head)
  let tHit = 0;
  { let inside = sk.outer0(...origin) < 0;
    for (let t = 0.02; t < 3; t += 0.02) {
      const now = sk.outer0(...add(origin, eyeDir, t)) < 0;
      if (inside && !now) { let lo = t - 0.02, hi = t;
        for (let k = 0; k < 30; k++) { const m = (lo + hi) / 2; if (sk.outer0(...add(origin, eyeDir, m)) < 0) lo = m; else hi = m; }
        tHit = hi; }
      inside = now;
    } }
  const re = p.eyeSize;
  const eyeC = add(add(origin, eyeDir, tHit), eyeDir, (p.eyeBulge - 1) * re);
  const eyeL = { c: eyeC, frame: (() => { const z = eyeDir, y0 = [0, 1, 0]; const x = norm(cross(y0, z)); return [x, norm(cross(z, x)), z]; })() };
  const eyeR = { c: mirror(eyeC), frame: mirrorFrame(eyeL.frame) };
  const socketR = re * 1.2;
  // the sockets cut for them, and the skin's thickness with the brows' share
  const skL = sk.withEyes([{ ...eyeL, r: re }, { ...eyeR, r: re }]);

  // ---- nostrils: on the upper beak's side near its base ----
  const tn = p.nostrilAlong;
  const nostril = sx => [sx * halfW(tn) * 0.82, by + S * (bc.gapeZ(tn) + bc.upperDepth(tn) * 0.55), zb + tn * L];
  const nr = [0.035 + 0.05 * p.nostril, 0.022 + 0.02 * p.nostril, 0.06 + 0.08 * p.nostril];

  // ---- the head field: the skin over the skull, then the soft bodies with no bone under them ----
  // the neck: down and a little back from under the skull, leaning forward by its tilt, wider toward the bottom by its
  // flare; open at the box's floor
  const neckAt = y => [0, -p.neckBack - p.neckTilt * (-y)];          // its axis (x, z) at height y
  const fNeck = (x, y, z) => {
    const [ax, az] = neckAt(y), r = p.neckWidth * R[0] * (1 + p.neckFlare * smoothstep(-0.6, -1.75, y));
    return Math.max(Math.sqrt((x - ax) ** 2 / 0.62 + (z - az) ** 2) - r, y + 0.1);
  };
  // the jaw line: from under the lower bill's base back and down into the neck's front, so the chin runs into the neck
  const jawFrom = [0, by - S * bc.Dl * 0.8, zb - 0.15], jawTo = (() => { const y = -1.3, [, az] = neckAt(y); return [0, y, az + p.neckWidth * R[0] * 0.7]; })();
  const fJaw = (x, y, z) => {
    const ab = add(jawTo, jawFrom, -1), t = clamp(((y - jawFrom[1]) * ab[1] + (z - jawFrom[2]) * ab[2]) / (ab[1] ** 2 + ab[2] ** 2), 0, 1);
    const q = add(jawFrom, ab, t), w = mix(halfW(0) * 1.1, p.neckWidth * R[0] * 0.8, t), h = mix(0.12, 0.3, t) * (0.5 + p.jawLine);
    return ellipsoid(x, y, z, q, [w, h, h]);
  };
  // the bridge over the bill's top: from behind the base forward bridgeFwd along the ridge, as wide as the bill there,
  // its underside inside the bill (the bill is cut out of the skin afterwards, so only a thin layer shows on top)
  // (each cross-section an ellipse round the bill's upper half, a little bigger than it; thinning toward its front end.
  // Where its lower edge meets the bill's side the skin is a thin wedge, and the mesher folds a little of it: about 90
  // crossing pairs at rest on the Tripo match. Reaching the whole side instead moved the attach point forward and folded
  // the corner; leaving it out let the eyes into the beak's base.)
  const fBridge = (x, y, z) => {
    const t = clamp((z - zb) / L, 0, 1), du = S * bc.upperDepth(t), top = by + S * (bc.gapeZ(t) + bc.hookDrop(t)) + du;
    const thin = smoothstep(zb + 0.5 * p.bridgeFwd, zb + p.bridgeFwd, z);
    const hw = halfW(t) + 0.04 * (1 - thin), ry = 0.5 * du + 0.05 * (1 - thin), yc = top - 0.5 * du;
    const d2 = (Math.hypot(x / hw, (y - yc) / ry) - 1) * Math.min(hw, ry);
    return Math.max(d2, z - (zb + p.bridgeFwd), zb - 0.35 - z);
  };
  // the cheek pads: centred beside the bill's base, a little above the gape, under the eye (left; the right mirrored)
  const cheekC = [halfW(0) * 0.85 + 0.12, by + 0.12 + p.cheekPadHeight, zb];
  const cheekRad = [0.12 + 0.18 * p.cheekPad, 0.15 + 0.2 * p.cheekPad, 0.2 + 0.25 * p.cheekPad];
  const fBase = (x, y, z) => {
    let d = skL.outer(x, y, z);
    // the throat: the soft mass under the jaw, hanging by its depth
    if (p.throatOn >= 0.5) d = sunion(d, ellipsoid(x, y, z, [0, -0.45 * R[1] - 0.3 * p.throatDrop, 0.22 * R[2] + 0.2 * p.throatDrop],
                                                   [0.62 * R[0], 0.42 + 0.3 * p.throat + 0.15 * p.throatDrop, 0.75 * R[2]]), 0.35);
    if (p.neckOn >= 0.5) d = sunion(d, fNeck(x, y, z), 0.4);
    if (p.jawLine > 0) d = sunion(d, fJaw(x, y, z), 0.25);
    // the face: from the cranium forward round the beak's base (the lores and cheeks)
    const bw = halfW(0), bd = S * (bc.Du + bc.Dl) / 2;
    if (p.face > 0) d = sunion(d, ellipsoid(x, y, z, [0, by + S * (bc.Du - bc.Dl) / 2, zb - 0.3],
                                            [bw * (1.2 + 0.8 * p.face), bd * (1.15 + 0.6 * p.face), 0.45 + 0.25 * p.face]), 0.3);
    // the skin over the top of the bill: the forehead's feathers running forward along its ridge, so the skin line is
    // far forward on top and back at the sides (as on the Tripo raven), hugging the bill a hair off it
    if (p.bridgeFwd > 0) d = sunion(d, fBridge(x, y, z), 0.12);
    // the cheek pads: soft masses beside the beak's base, under the eyes (the cheek joints move them: a smile)
    if (p.cheekPad > 0) d = sunion(d, ellipsoid(Math.abs(x), y, z, cheekC, cheekRad), 0.2);
    return d;
  };
  // the brows: a ridge of the skin itself over each eye, along the brow's line (the strips, if on, sit over it)
  const browLines = [["L", eyeL], ["R", eyeR]].map(([side, e]) => ({ side, ...browCurve(fBase, e, p, side) }));
  // Each brow is a ridge along its line, pushed out over the eye by the overhang, and with a mass behind it reaching up
  // the head (so a heavy brow changes the top of the head too, as on the Tripo raven), blended broadly into the skin.
  for (const b of browLines) {
    const r = i => { const s = i / (b.pts.length - 1) - 0.5; return p.browRidge * (1 - 0.6 * Math.pow(Math.abs(2 * s), 2)); };
    b.ridge = b.pts.map((q, i) => add(q, b.nrm[i], r(i) * (p.browOverhang - 0.4)));
    // (up along the skin: the world's up with the normal taken out; right against the ridge, so the two read as one
    // broad bulge rather than a second ridge above the first)
    b.mass = b.pts.map((q, i) => { const n = b.nrm[i], u = norm(add([0, 1, 0], n, -n[1])); return add(add(q, u, r(i) * 0.9), n, -r(i) * 0.5); });
    b.r = b.pts.map((_, i) => r(i));
    b.reach += p.browRidge * 3;
  }
  const ridge = (x, y, z) => {
    let d = 1e9;
    for (const b of browLines) {
      if (Math.hypot(x - b.mid[0], y - b.mid[1], z - b.mid[2]) > b.reach) continue;
      for (let i = 0; i + 1 < b.pts.length; i++) {
        d = Math.min(d, capsule(x, y, z, b.ridge[i], b.ridge[i + 1], b.r[i]));
        if (p.browMass > 0) d = sunion(d, capsule(x, y, z, b.mass[i], b.mass[i + 1], b.r[i] * 1.2 * p.browMass), 0.25);
      }
    }
    return d;
  };
  const fCranium = p.browRidge > 0 ? (x, y, z) => sunion(fBase(x, y, z), ridge(x, y, z), 0.04 + 0.4 * p.browRidge) : fBase;
  // ---- the mouth: the beak and the corners, attached where the skin reaches the beak (parts/mouth.js) ----
  const mouth = MOUTH.beakMouth({ p, bp, place: { S, by, zb }, face: fCranium, gap });
  const gape = mouth.gapeY, fSlit = mouth.slit, zc = mouth.deep;
  const cornerL = mouth.corner.L, cornerR = mouth.corner.R, cz = cornerL[2], cx = Math.abs(cornerL[0]);
  const solid = fCranium;
  // the face's skin (what the join lands on), then hollowed round the bills' bases, out of sight under the join
  const fSkin = (x, y, z) => {
    let d = ssub(fCranium(x, y, z), fSlit(x, y, z), 0.025);
    d = ssub(d, sphere(x, y, z, eyeL.c, socketR), 0.08);
    d = ssub(d, sphere(x, y, z, eyeR.c, socketR), 0.08);
    if (p.nostril > 0) for (const sx of [1, -1]) d = ssub(d, ellipsoid(x, y, z, nostril(sx), nr), 0.03);
    return d;
  };
  const field = (x, y, z) => ssub(fSkin(x, y, z), Math.min(fBillU(x, y, z), fBillL(x, y, z)) - clear, 0.02);

  const rc = sk.reach;
  // (wide and deep enough for the neck at the box's floor too)
  const neckR = p.neckOn >= 0.5 ? p.neckWidth * R[0] * (1 + p.neckFlare) : 0, neckZ = neckAt(-1.75)[1];
  const lo = [-Math.max(rc.side, neckR * 0.8) - 0.4, -1.75, Math.min(-rc.back, neckZ - neckR) - 0.35];
  const hi = [Math.max(rc.side, neckR * 0.8) + 0.4, rc.top + 0.3, zb + 0.8];
  const head = mesh(field, lo, hi, p.detail);
  // the skull layer itself, to see under the skin (the bone, eye sockets cut)
  const skullMesh = opts.skull ? mesh(skL.bone, lo, hi, p.detail) : null;

  // ---- joints (rest place in head space; frame: local x, y, z axes) ----
  const I = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
  const joints = [
    { name: "root", parent: null, at: [0, -1.6, -0.6], frame: I },
    { name: "head", parent: "root", at: [0, -0.4, -0.25], frame: I },
    { name: "jaw", parent: "head", at: hinge, frame: I },
    { name: "upperBeak", parent: "head", at: fore, frame: I },
    // the throat: drops with the open beak as the lower bill's underside near its base does (drop: that move)
    { name: "throat", parent: "head", at: [0, by - S * bc.Dl, zb - 0.2], frame: I, drop: (() => {
      const ref = toHead(0.1 * bc.L, 0, bc.gapeZ(0.1) + bc.hookDrop(0.1) - bc.lowerDepth(0.1));
      const a = BEAK.OPEN.lower * deg, c = Math.cos(a), s = Math.sin(a), y = ref[1] - hinge[1], z = ref[2] - hinge[2];
      return [0, y * c - z * s - y, y * s + z * c - z];
    })() },
  ];
  // the cheek's middle: above and behind the corner, on the skin
  // (with cheek pads, the pad's middle, and the area it covers: the cheek joint is the pad's muscle)
  const cheekR = p.cheekPad > 0 ? Math.max(0.3, 1.1 * Math.max(...cheekRad)) : 0.3;
  const cheekAt = (() => { const q = p.cheekPad > 0 ? [...cheekC] : [cx, cornerL[1] + 0.1, cz - 0.3]; return add(q, [1, 0, 0], -fCranium(...q)); })();
  // the mouth corners, if it has them (without, a plain beak: nothing to smile with)
  if (mouth.on) joints.push({ name: "mouthCorner_L", parent: "head", at: cornerL, frame: I },
                            { name: "mouthCorner_R", parent: "head", at: cornerR, frame: I },
                            // the lips' middle parts (the lip editor, parts/mouth.js)
                            { name: "lipUpper_L", parent: "head", at: mouth.lipJoints.upperL, frame: I },
                            { name: "lipUpper_R", parent: "head", at: mirror(mouth.lipJoints.upperL), frame: I },
                            { name: "lipLower_L", parent: "head", at: mouth.lipJoints.lowerL, frame: I },
                            { name: "lipLower_R", parent: "head", at: mirror(mouth.lipJoints.lowerL), frame: I },
                            // the cheeks: the skin above and behind each corner, bunched up by a smile
                            { name: "cheek_L", parent: "head", at: cheekAt, frame: I },
                            { name: "cheek_R", parent: "head", at: mirror(cheekAt), frame: I });
  for (const [side, e] of [["L", eyeL], ["R", eyeR]]) {
    joints.push({ name: `eye_${side}`, parent: "head", at: e.c, frame: e.frame });
    joints.push({ name: `lidUpper_${side}`, parent: "head", at: e.c, frame: e.frame });
    joints.push({ name: `lidLower_${side}`, parent: "head", at: e.c, frame: e.frame });
  }
  // the brows: at the middle of each brow line, x along it (inner to outer), z out of the skin. They move the skin
  // round the brow (the ridge, the forehead over it, the lid's skin under it), and the strips if those are on.
  for (const b of browLines) {
    const n = b.nrm[b.pts.length >> 1];
    let bx = norm(add(b.pts[b.pts.length - 1], b.pts[0], -1));
    if (b.side === "R") bx = bx.map(c => -c);             // (so the right frame is the left's mirror, right-handed)
    const bz = norm(add(n, bx, -(n[0] * bx[0] + n[1] * bx[1] + n[2] * bx[2])));
    joints.push({ name: `brow_${b.side}`, parent: "head", at: b.mid, frame: [bx, cross(bz, bx), bz] });
  }
  const J = Object.fromEntries(joints.map((j, i) => [j.name, i]));
  const mouthJoints = { upper: J.upperBeak, lower: J.jaw, cornerL: J.mouthCorner_L, cornerR: J.mouthCorner_R };

  // ---- head skin weights and the partition ----
  const nV = head.positions.length / 3;
  const hLab = new Array(nV), hJ = new Uint16Array(nV * 4), hW = new Float32Array(nV * 4);
  const faceWeights = (x, y, z) => {
    const g = gape(z);
    // the jaw: below the gape, in front of the corner, fading behind it and down the throat
    let wj = smoothstep(gap / 2, -gap / 2, y - g) * smoothstep(cz - 0.4, cz + 0.02, z) * smoothstep(cx + 0.25, cx, Math.abs(x));
    const du = fBillU(x, y, z), dl = fBillL(x, y, z);
    // the sleeve on the bill's base goes with its bill: below the gape the jaw's, above it the upper bill's
    // (the skin over the bill's top, the bridge, is a thin layer on it: all of it goes with the upper bill, or its two
    // faces part as the beak opens and cross)
    const inBridge = p.bridgeFwd > 0 ? smoothstep(0.06, 0, fBridge(x, y, z)) : 0;
    const onBill = smoothstep(zb - 0.35, zb - 0.05, z) * Math.max(smoothstep(0.16, 0.07, Math.min(du, dl)), inBridge);
    let wu = 0;
    if (y < g) wj = Math.max(wj, onBill); else wu = Math.min(onBill, 1 - wj);
    // the throat: the skin under the jaw, away from the bill, goes with the throat joint, which drops as the beak
    // opens (as the bill's underside does) without turning. Turning with the jaw, whose hinge is far back and high,
    // it swung back into the throat's floor and creased; dropping, it only stretches. It fades out towards the neck.
    // (by how far below the lower bill's underside, not by distance from the bill: a big beak leaves a thin shelf of
    // skin under it, and both faces of it must go together)
    const tb = Math.min(1, Math.max(0, (z - zb) / L)), under = by + S * (bc.gapeZ(tb) + bc.hookDrop(tb) - bc.lowerDepth(tb));
    const toThroat = smoothstep(0.05, 0.3, under - y) * smoothstep(0.1, 0.3, g - y);
    const wt = toThroat * smoothstep(cz - 0.8, cz + 0.02, z) * smoothstep(cx + 0.4, cx, Math.abs(x));
    wj *= 1 - toThroat;
    // the corner: the band along the gape line, on its own side (parts/mouth.js)
    const ca = mouth.cornerAt(x, y, z), dc = ca.d;
    // (but not round the eye: the skin at the socket's rim stays put, or the cheek pushes into the lids)
    const de = Math.min(...[eyeL.c, eyeR.c].map(e => Math.hypot(x - e[0], y - e[1], z - e[2])));
    const wc = 0.9 * ca.w * smoothstep(socketR + 0.03, socketR + 0.18, de);
    const ws = [[J.head, (1 - wc) * Math.max(0, 1 - wj - wu - wt)], [J.jaw, (1 - wc) * wj], [J.upperBeak, (1 - wc) * wu],
                [J.throat, (1 - wc) * wt], [mouth.on ? (x >= 0 ? J.mouthCorner_L : J.mouthCorner_R) : J.head, mouth.on ? wc : 0]];
    // the lips' middle parts, each lip on its own side of the gape (the lip editor, parts/mouth.js)
    if (mouth.on) {
      const lip = mouth.lipAt(x, y, z), wl = 0.85 * lip.w * smoothstep(socketR + 0.03, socketR + 0.18, de);
      if (wl > 0) {
        for (const e of ws) e[1] *= 1 - wl;
        ws.push([J[`lip${lip.upper ? "Upper" : "Lower"}_${x >= 0 ? "L" : "R"}`], wl]);
      }
      // the cheek: a soft blob above the gape, behind the corner (kept off the lids)
      const ch = x >= 0 ? cheekAt : mirror(cheekAt), dch = Math.hypot(x - ch[0], y - ch[1], z - ch[2]);
      // (the whole cheek, from beside the beak to under the eye, fading out by cheekR; not the gape's lower side, nor the
      // socket's rim)
      // (with a pad, the whole pad's area; without, a smaller patch behind the corner, as before the pads)
      // (along the gape the slit parts the cheek from the lip below it; behind the corner's end there's no slit, so the
      // cheek fades out over a wider band there, or the skin folds where a smile lifts it)
      const behind = smoothstep(cz + 0.02, cz - 0.15, z), lowEdge = smoothstep(g - 0.2 * behind, g + 0.08 + 0.2 * behind, y);
      const wch = 0.75 * Math.exp(-(p.cheekPad > 0 ? 1 : 2) * (dch / cheekR) ** 2) * lowEdge * smoothstep(socketR, socketR + 0.12, de);
      if (wch > 0.01) {
        for (const e of ws) e[1] *= 1 - wch;
        ws.push([J[`cheek_${x >= 0 ? "L" : "R"}`], wch]);
      }
    }
    // the brow: the skin round its line goes with it, further up the forehead than down toward the lid, and not the
    // socket's rim (the lids are their own pieces)
    {
      const b = browLines[x >= 0 ? 0 : 1];
      if (Math.hypot(x - b.mid[0], y - b.mid[1], z - b.mid[2]) < b.reach + 0.4) {
        let best = 1e9, bi = 0;
        for (let i = 0; i < b.pts.length; i++) { const e = (x - b.pts[i][0]) ** 2 + (y - b.pts[i][1]) ** 2 + (z - b.pts[i][2]) ** 2; if (e < best) { best = e; bi = i; } }
        const q = b.pts[bi], up = (y - q[1]) > 0, r = up ? 0.4 : 0.2;
        // (and not the skin on the bill: near it the brow fades out, so the thin skin over the bill's top moves as one)
        const wb = 0.85 * Math.exp(-best / (r * r)) * smoothstep(socketR, socketR + 0.1, de) * smoothstep(0.05, 0.3, Math.min(du, dl));
        if (wb > 0.01) {
          for (const e of ws) e[1] *= 1 - wb;
          ws.push([J[`brow_${b.side}`], wb]);
        }
      }
    }
    // the sleeve takes its bill's own weights outright (corner share and all): where the skin round a big beak's base
    // is thin, both its faces lie on the sleeve, so they go together
    if (onBill > 0) {
      for (const e of ws) e[1] *= 1 - onBill;
      for (const [j, w] of mouth.billWeights(y < g ? "lower" : "upper", [x, y, z], mouthJoints)) {
        const e = ws.find(a => a[0] === j); if (e) e[1] += w * onBill; else ws.push([j, w * onBill]);
      }
    }
    // (four at most: the largest, summing to 1)
    ws.sort((a, b) => b[1] - a[1]);
    const top = ws.slice(0, 4), sum = top.reduce((s, a) => s + a[1], 0) || 1;
    return { ws: top.map(([j, w]) => [j, w / sum]), dc, g };
  };
  for (let v = 0; v < nV; v++) {
    const x = head.positions[3 * v], y = head.positions[3 * v + 1], z = head.positions[3 * v + 2];
    const { ws, dc, g } = faceWeights(x, y, z);
    ws.forEach(([j, w], k) => { hJ[4 * v + k] = j; hW[4 * v + k] = w; });
    // which part
    let lab;
    if (Math.min(Math.hypot(...[0, 1, 2].map(i => [x, y, z][i] - eyeL.c[i])), Math.hypot(...[0, 1, 2].map(i => [x, y, z][i] - eyeR.c[i]))) < socketR + 0.07) lab = "socket";
    else if (p.nostril > 0 && Math.min(ellipsoid(x, y, z, nostril(1), nr), ellipsoid(x, y, z, nostril(-1), nr)) < 0.02) lab = "nostril";
    else if (Math.abs(y - g) < gap / 2 + 0.01 && solid(x, y, z) < -0.02) lab = "mouth";        // (the slit's walls, inside)
    else if (y < g - 0.15 && z > -0.3) lab = "throat";
    else lab = "head";
    hLab[v] = lab;
  }

  const parts = [{ name: "head_geo", positions: head.positions, indices: head.indices, labels: hLab, joints: hJ, weights: hW }];

  // ---- the bills: the beak editor's own meshes (parts/beak.js), in head space ----
  for (const [which, joint] of [["upper", J.upperBeak], ["lower", J.jaw]]) {
    const m = BEAK.billMesh(bp, which);
    const pos = [];
    for (let i = 0; i < m.positions.length; i += 3) pos.push(...toHead(m.positions[i], m.positions[i + 1], m.positions[i + 2]));
    // the base tucked under the skin only so far: each line of the bill starts beakTuck behind where it comes out of the
    // face (the skin line), so its hidden base follows the face's slant (forward on top, back at the cutting edges) and
    // stays clear of the eyes beside it. (Each line's rings spread again from its own start to the tip: piled up at the
    // start instead, they left flat triangles the checks counted as crossings.)
    {
      const RS = BEAK.N_ARCH + BEAK.N_FLOOR, NR = BEAK.N_RING, tEnd = which === "upper" ? 1 : bc.lowerEnd;
      const tMin = mouth.skinLine[which].map(c => Math.max(0, c.t - p.beakTuck / L)), tFloor = Math.min(...tMin);
      const orig = pos.slice();
      const at = (col, f) => {                         // the line's original point at ring position f (0..NR)
        const r0 = Math.min(NR - 1, Math.floor(f)), s = f - r0;
        return [0, 1, 2].map(k => orig[3 * (r0 * RS + col) + k] * (1 - s) + orig[3 * ((r0 + 1) * RS + col) + k] * s);
      };
      for (let r = 0; r < NR; r++) for (let col = 0; col < RS; col++) {
        const t0 = col < BEAK.N_ARCH ? tMin[col] : tFloor, f0 = t0 / tEnd * NR;
        const q = at(col, f0 + (r / NR) * (NR - f0));
        for (let k = 0; k < 3; k++) pos[3 * (r * RS + col) + k] = q[k];
      }
    }
    // (rigid with its joint, or its base bending with the corner: the mouth decides, parts/mouth.js)
    const part = rigid(`${which}Beak_geo`, { positions: pos, indices: m.indices }, `${which}_beak`, joint);
    for (let v = 0; v < pos.length / 3; v++) {
      const ws = mouth.billWeights(which, pos.slice(3 * v, 3 * v + 3), mouthJoints);
      for (let k = 0; k < 4; k++) { part.joints[4 * v + k] = ws[k] ? ws[k][0] : 0; part.weights[4 * v + k] = ws[k] ? ws[k][1] : 0; }
    }
    parts.push(part);
  }

  // ---- the join: the face's own skin onto the bill, over the skin line (parts/mouth.js) ----
  // the face's normals from its field (smooth, so they meet the join's face end exactly)
  const headNormals = [];
  for (let v = 0; v < head.positions.length; v += 3) {
    const q = [head.positions[v], head.positions[v + 1], head.positions[v + 2]], f0 = field(...q);
    headNormals.push(...norm([0, 1, 2].map(k => { const r = [...q]; r[k] += 1e-4; return field(...r) - f0; })));
  }
  const joins = mouth.join({ skin: fSkin, weights: (x, y, z) => faceWeights(x, y, z).ws, joints: mouthJoints });
  // it is part of the face's mesh (no extra mesh); each column takes the colour of the skin where it lands
  {
    const hp = parts[0], base = hp.positions.length / 3;
    const pos = [...hp.positions], idx = [...hp.indices], nrm = [...headNormals], lab = [...hp.labels];
    const jj = [...hp.joints], ww = [...hp.weights];
    const nearest = q => {
      let best = 1e9, bl = "head";
      for (let w = 0; w < base; w++) {
        const d = (hp.positions[3 * w] - q[0]) ** 2 + (hp.positions[3 * w + 1] - q[1]) ** 2 + (hp.positions[3 * w + 2] - q[2]) ** 2;
        if (d < best) { best = d; bl = hp.labels[w]; }
      }
      return bl === "throat" ? bl : "head";       // (skin only: not the mouth's inside or the corner it hovers by)
    };
    let off = base;
    for (const j of joins) {
      pos.push(...j.positions); nrm.push(...j.normals); jj.push(...j.joints); ww.push(...j.weights);
      idx.push(...j.indices.map(v => v + off));
      const last = (j.rows - 1) * j.cols;
      const colLab = [...Array(j.cols).keys()].map(i => nearest(j.positions.slice(3 * (last + i), 3 * (last + i) + 3)));
      for (let k = 0; k < j.positions.length / 3; k++) lab.push(colLab[k % j.cols]);
      off += j.positions.length / 3;
    }
    parts[0] = { ...hp, positions: Float32Array.from(pos), indices: Uint32Array.from(idx), normals: Float32Array.from(nrm),
                 labels: lab, joints: Uint16Array.from(jj), weights: Float32Array.from(ww) };
  }

  // ---- the mouth corners: their own meshes, the lips along each gape line (parts/mouth.js) ----
  for (const m of mouth.corners({ weights: (x, y, z) => faceWeights(x, y, z).ws, joints: mouthJoints })) {
    const n = m.positions.length / 3;
    parts.push({ name: `mouthCorner_${m.side}_geo`, positions: Float32Array.from(m.positions), indices: Uint32Array.from(m.indices),
                 normals: Float32Array.from(m.normals), labels: new Array(n).fill("mouth_corner"),
                 joints: Uint16Array.from(m.joints), weights: Float32Array.from(m.weights) });
  }

  // ---- eyeballs and lids: their own pieces in the socket ----
  const openU = mix(5, 45, p.lidOpen) * deg, openL = mix(10, 45, p.lidOpen) * deg;
  const meet = -openL + (openU + openL) * 0.38;
  for (const [side, e] of [["L", eyeL], ["R", eyeR]]) {
    const ball = sphereBand(e.c, e.frame, re, -Math.PI / 2, Math.PI / 2, -Math.PI, Math.PI, 20, 40);
    const n = ball.positions.length / 3;
    const lab = new Array(n);
    for (let v = 0; v < n; v++) {
      const d = norm([0, 1, 2].map(i => ball.positions[3 * v + i] - e.c[i]));
      const cosA = d[0] * e.frame[2][0] + d[1] * e.frame[2][1] + d[2] * e.frame[2][2];
      const ang = Math.acos(clamp(cosA, -1, 1));
      lab[v] = ang < 0.32 * p.irisSize ? "pupil" : ang < 0.75 * p.irisSize ? "iris" : "eyeball";
    }
    parts.push(rigid(`eyeball_${side}_geo`, ball, lab, J[`eye_${side}`]));
    const up = lidShell(e.c, e.frame, re * 1.1, openU, 190 * deg, 80 * deg, 18, 22);
    parts.push(rigid(`lidUpper_${side}_geo`, up, "lid_upper", J[`lidUpper_${side}`]));
    const low = lidShell(e.c, e.frame, re * 1.07, -190 * deg, -openL, 80 * deg, 18, 22);
    parts.push(rigid(`lidLower_${side}_geo`, low, "lid_lower", J[`lidLower_${side}`]));
  }

  // ---- brow strips (optional): pieces floating just over the ridge, on the brow joints ----
  if (p.browOn >= 0.5) {
    for (const [side, e] of [["L", eyeL], ["R", eyeR]]) {
      const br = browPiece(field, e, p, side);
      parts.push(rigid(`brow_${side}_geo`, br, "brow", J[`brow_${side}`]));
    }
  }

  // (a check: how far the eyeball's surface is from the bills, negative where it's inside one: the bills' fields at
  // points all over the eyeball, a sphere of them)
  // (the bills as built: their vertices' distance from the eye's centre, less its radius)
  let eyeClear = 1e9;
  for (const part of parts.filter(q => /Beak_geo$/.test(q.name)))
    for (let v = 0; v < part.positions.length; v += 3)
      eyeClear = Math.min(eyeClear, Math.hypot(part.positions[v] - eyeL.c[0], part.positions[v + 1] - eyeL.c[1], part.positions[v + 2] - eyeL.c[2]) - re);
  return { parts, joints, skull: skullMesh, rig: makeRig(p, { openU, openL, meet }, joints), info: { cornerL, hinge, eyeL: eyeL.c, zc, zb, eyeClear,
    mouth: { skinLine: { upper: mouth.skinLine.upper.map(c => c.E), lower: mouth.skinLine.lower.map(c => c.E) },
             attach: mouth.attach, gapeLine: mouth.gapeLine } } };
}

function capsule(x, y, z, a, b, r) {
  const ab = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], ap = [x - a[0], y - a[1], z - a[2]];
  const t = clamp((ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / (ab[0] ** 2 + ab[1] ** 2 + ab[2] ** 2 || 1), 0, 1);
  return Math.hypot(ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t) - r;
}

// A brow's line on the skin over an eye: rays from behind the eye out through the field's surface, at the brow's
// height and over its length (inner end toward the middle of the face). Points, the skin's normals there, the middle.
function browCurve(field, e, p, side) {
  const [X, Y, Z] = e.frame;
  const N = 24, pts = [], nrm = [];
  const grad = (q) => { const h = 1e-3; return norm([0, 1, 2].map(i => { const a = [...q], b = [...q]; a[i] += h; b[i] -= h; return field(...a) - field(...b); })); };
  for (let i = 0; i <= N; i++) {
    const s = i / N - 0.5;                                // -0.5 inner .. 0.5 outer
    const yaw = (side === "L" ? 1 : -1) * s * p.browLength * deg;  // (the left eye's +x points out, the right's in)
    const pitch = (p.browHeight + p.browTilt * s) * deg;
    const dir = norm([0, 1, 2].map(k => Math.cos(pitch) * Math.sin(yaw) * X[k] + Math.sin(pitch) * Y[k] + Math.cos(pitch) * Math.cos(yaw) * Z[k]));
    let q;
    if (p.browOnRim >= 0.5) {
      // on the socket's rim: just outside the socket's opening at this angle from the eye's centre, so the cut that opens
      // the socket trims the ridge's inner side and that edge is the brow (one bulge over the socket, as on the Tripo
      // raven, not a lip with a ridge above it). Its normal: straight out from the eye.
      q = add(e.c, dir, p.eyeSize * 1.2 + 0.5 * p.browRidge);
      nrm.push(dir);
      pts.push(q);
      continue;
    } else {
      // (rays from a point behind the eye, so they leave the head past the socket's rim; from outside, in to the skin)
      const o = add(e.c, Z, -0.6);
      let t = 3;
      for (; t > 0; t -= 0.01) { q = add(o, dir, t); if (field(...q) < 0) break; }
      let lo = t, hi = t + 0.01;
      for (let k = 0; k < 30; k++) { const m = (lo + hi) / 2; if (field(...add(o, dir, m)) > 0) hi = m; else lo = m; }
      q = add(o, dir, hi);
    }
    nrm.push(grad(q));
    pts.push(q);
  }
  const mid = pts[N / 2];
  const reach = Math.max(...pts.map(q => Math.hypot(q[0] - mid[0], q[1] - mid[1], q[2] - mid[2]))) + 0.3;
  return { pts, nrm, mid, reach };
}

function rigid(name, m, label, joint) {
  const n = m.positions.length / 3;
  const J = new Uint16Array(n * 4), W = new Float32Array(n * 4);
  for (let v = 0; v < n; v++) { J[4 * v] = joint; W[4 * v] = 1; }
  return { name, positions: Float32Array.from(m.positions), indices: Uint32Array.from(m.indices),
           labels: Array.isArray(label) ? label : new Array(n).fill(label), joints: J, weights: W };
}

// A brow: a curve over the eye on the skin (rays from the eye's centre, out through the field's surface), a
// rectangle swept along it, lifted off the skin by the gap. Inner end toward the middle of the face.
function browPiece(field, e, p, side) {
  const [X, Y, Z] = e.frame;
  const N = 24, pts = [], nrm = [];
  const grad = (q) => { const h = 1e-3; return norm([0, 1, 2].map(i => { const a = [...q], b = [...q]; a[i] += h; b[i] -= h; return field(...a) - field(...b); })); };
  for (let i = 0; i <= N; i++) {
    const s = i / N - 0.5;                                // -0.5 inner .. 0.5 outer
    const yaw = (side === "L" ? 1 : -1) * s * p.browLength * deg;  // (the left eye's +x points out, the right's in)
    const pitch = (p.browHeight + p.browTilt * s) * deg;
    const dir = norm([0, 1, 2].map(k => Math.cos(pitch) * Math.sin(yaw) * X[k] + Math.sin(pitch) * Y[k] + Math.cos(pitch) * Math.cos(yaw) * Z[k]));
    // march out from inside the socket wall to the skin
    // (rays from a point behind the eye, so they leave the head past the socket's rim; from outside, in to the skin)
    const o = add(e.c, Z, -0.6);
    let t = 3, q;
    for (; t > 0; t -= 0.01) { q = add(o, dir, t); if (field(...q) < 0) break; }
    let lo = t, hi = t + 0.01;
    for (let k = 0; k < 30; k++) { const m = (lo + hi) / 2; if (field(...add(o, dir, m)) > 0) hi = m; else lo = m; }
    q = add(o, dir, hi);
    const n = grad(q);
    nrm.push(n);
    pts.push(add(q, n, p.browGap + p.browThick / 2));
  }
  const V = [], F = [];
  for (let i = 0; i <= N; i++) {
    const tng = norm(i < N ? add(pts[i + 1], pts[i], -1) : add(pts[i], pts[i - 1], -1));
    const n = nrm[i];
    const u = norm(cross(n, tng));
    const s = i / N - 0.5;
    const w = p.browWidth * (1 - 0.65 * Math.pow(Math.abs(2 * s), 3)) / 2, th = p.browThick / 2;
    for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) V.push(...add(add(pts[i], u, a * w), n, b * th));
  }
  for (let i = 0; i < N; i++) for (let k = 0; k < 4; k++) {
    const a = 4 * i + k, b = 4 * i + (k + 1) % 4;
    F.push(a, b, b + 4, a, b + 4, a + 4);
  }
  F.push(0, 2, 1, 0, 3, 2);
  const e0 = 4 * N; F.push(e0, e0 + 1, e0 + 2, e0, e0 + 2, e0 + 3);
  // outward faces: flip all if the signed volume is negative
  let vol = 0;
  for (let f = 0; f < F.length; f += 3) {
    const A = V.slice(3 * F[f], 3 * F[f] + 3), B = V.slice(3 * F[f + 1], 3 * F[f + 1] + 3), C = V.slice(3 * F[f + 2], 3 * F[f + 2] + 3);
    vol += A[0] * (B[1] * C[2] - B[2] * C[1]) - A[1] * (B[0] * C[2] - B[2] * C[0]) + A[2] * (B[0] * C[1] - B[1] * C[0]);
  }
  if (vol < 0) for (let f = 0; f < F.length; f += 3) [F[f + 1], F[f + 2]] = [F[f + 2], F[f + 1]];
  // the joint: at the middle, x along the brow (inner to outer), z out of the skin
  const mid = pts[N / 2], n = nrm[N / 2];
  let x = norm(add(pts[N], pts[0], -1));
  if (side === "R") x = x.map(c => -c);                 // (so the right frame is the left's mirror, right-handed)
  const z = norm(add(n, x, -(n[0] * x[0] + n[1] * x[1] + n[2] * x[2])));
  return { positions: V, indices: F, at: mid, frame: [x, cross(z, x), z] };
}

// The controls and what each does. Left side written once, the right mirrored.
function makeRig(p, lid, joints) {
  const controls = [], values = [], jointOut = [];
  const control = (name, min = 0, max = 1) => controls.push({ name, min, max });
  const drive = (ctl, joint, { t, r }) => {
    const name = `${ctl}__${joint}`;
    values.push({ name, control: ctl, map: [[0, 0], [1, 1]] });
    const o = { joint, from: name };
    if (t) o.translation = t;
    if (r) o.rotationEuler = r;
    jointOut.push(o);
  };
  const sides = (base, joint, move) => {
    for (const s of ["L", "R"]) {
      control(`${base}_${s}`);
      const m = { ...move };
      if (s === "R") { if (m.t) m.t = mirrorMove(m.t); if (m.r) m.r = mirrorRot(m.r); }
      drive(`${base}_${s}`, `${joint}_${s}`, m);
    }
  };
  const has = n => joints.some(j => j.name === n);

  control("jawOpen");
  drive("jawOpen", "jaw", { r: [BEAK.OPEN.lower, 0, 0] });
  drive("jawOpen", "upperBeak", { r: [BEAK.OPEN.upper, 0, 0] });
  const throat = joints.find(j => j.name === "throat");
  if (throat) drive("jawOpen", "throat", { t: throat.drop });
  if (has("mouthCorner_L")) {
    // a smile: the corner up and back, the lips' middles up a little less (so the gape line curves up toward the
    // corner), and the cheek bunched up and out
    sides("mouthSmile", "mouthCorner", { t: [0.05, 0.26, -0.1] });
    for (const s of ["L", "R"]) {
      const m = t => s === "R" ? mirrorMove(t) : t;
      drive(`mouthSmile_${s}`, `lipUpper_${s}`, { t: m([0.01, 0.08, -0.03]) });
      drive(`mouthSmile_${s}`, `lipLower_${s}`, { t: m([0.01, 0.08, -0.03]) });
      // (the cheek goes the corner's way, so the skin round the corner's end moves with it)
      drive(`mouthSmile_${s}`, `cheek_${s}`, { t: m([0.05, 0.26, -0.1]) });
    }
    sides("mouthFrown", "mouthCorner", { t: [0.02, -0.09, 0.0] });
    sides("mouthStretch", "mouthCorner", { t: [0.07, -0.01, -0.06] });
    // the lips' middle parts: raised, lowered, pressed together (ARKit's names)
    sides("mouthUpperUp", "lipUpper", { t: [0.01, 0.06, 0] });
    sides("mouthLowerDown", "lipLower", { t: [0.01, -0.06, 0] });
    for (const s of ["L", "R"]) {
      control(`mouthPress_${s}`);
      drive(`mouthPress_${s}`, `lipUpper_${s}`, { t: [0, -0.015, 0] });
      drive(`mouthPress_${s}`, `lipLower_${s}`, { t: [0, 0.015, 0] });
    }
  }
  const close = (lid.openU - lid.meet) / deg, rise = (lid.meet + lid.openL) / deg;
  sides("eyeBlink", "lidUpper", { r: [close, 0, 0] });
  for (const s of ["L", "R"]) drive(`eyeBlink_${s}`, `lidLower_${s}`, { r: [-rise, 0, 0] });
  sides("eyeWide", "lidUpper", { r: [-14, 0, 0] });
  sides("eyeSquint", "lidLower", { r: [-0.6 * rise, 0, 0] });
  sides("eyeLookUp", "eye", { r: [-25, 0, 0] });
  sides("eyeLookDown", "eye", { r: [25, 0, 0] });
  sides("eyeLookOut", "eye", { r: [0, 30, 0] });
  sides("eyeLookIn", "eye", { r: [0, -30, 0] });
  // the lids follow the eye a little when it looks up and down
  for (const s of ["L", "R"]) {
    drive(`eyeLookUp_${s}`, `lidUpper_${s}`, { r: [-10, 0, 0] });
    drive(`eyeLookDown_${s}`, `lidUpper_${s}`, { r: [12, 0, 0] });
    drive(`eyeLookDown_${s}`, `lidLower_${s}`, { r: [6, 0, 0] });
  }
  if (has("brow_L")) {
    // moves along the skin, not into it: down the brow's own up axis, and out along its normal as the skin
    // under it bulges (in the head's space, as the rig wants; the left brow's frame, mirrored for the right)
    const [, bu, bn] = joints.find(j => j.name === "brow_L").frame;
    const along = (up, out) => [0, 1, 2].map(k => bu[k] * up + bn[k] * out);
    // (bigger than a real brow's: the skin round it goes too, and this is a stylized face)
    sides("browDown", "brow", { t: along(-0.11, 0.05), r: [0, 0, 10] });
    sides("browOuterUp", "brow", { t: along(0.07, 0.01), r: [0, 0, 14] });
    control("browInnerUp");
    for (const s of ["L", "R"]) {
      const m = { t: along(0.07, 0.01), r: [0, 0, -14] };
      drive("browInnerUp", `brow_${s}`, s === "R" ? { t: mirrorMove(m.t), r: mirrorRot(m.r) } : m);
    }
  }
  return { name: "raven_stylized", controls, values, joints: jointOut };
}
