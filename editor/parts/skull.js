// The skull: the head's first layer. Simple bone shapes after the skull editor (tools/chareditor/human_skull.html in
// AnimationStyles): an egg-shaped braincase with a top, back and forehead of its own, a base, a brow ridge, a cheek
// bar, temple hollows, eye sockets, and a mount the beak is set into. The skin is the second layer: the skull grown
// by a thickness that every point of the skull has (thickness(x, y, z)), set per region by the "Skin thickness"
// sliders and blended between them.
//
// Same units and axes as raven.js: +Y up, the head faces +Z, the character's left is +X.

import { clamp, smoothstep, mix, sunion, ssub, ellipsoid, sphere } from "../sdf.js";

export const PARAMS = [
  // key, label, min, max, value
  ["skullWidth", "width", 0.65, 1.2, 0.92],
  ["skullHeight", "height", 0.7, 1.2, 0.94],
  ["skullLength", "length", 0.75, 1.3, 1.04],
  ["skullEgg", "narrower forehead", 0, 0.4, 0.1],
  ["skullTop", "top: forward (+) or back (-)", -0.4, 0.4, 0],
  ["skullDome", "top: dome", 0, 0.3, 0.06],
  ["skullBack", "back bulge", 0, 0.3, 0.08],
  ["skullForehead", "forehead fullness", 0, 0.3, 0.08],
  ["skullBase", "base (flat underneath)", -1, -0.4, -0.8],
  ["skullBrow", "brow ridge", 0, 0.12, 0.03],
  ["skullCheek", "cheek bar", 0, 0.12, 0.05],
  ["skullTemple", "temple hollow", 0, 0.2, 0.06],
  ["skullTempleFill", "temple: muscle filling the hollow", 0, 1, 0.8],
  ["skullSocket", "eye socket size (x eye)", 1, 1.4, 1.12],
  ["skullMount", "beak mount (x the bill's base)", 0.5, 1.4, 0.9],
  ["skullBridge", "bridge to the beak", 0, 0.2, 0.08],
];

// the skin's thickness over the skull, by region (head units); every point blends these by distance
export const THICK_PARAMS = [
  ["thickBase", "everywhere (base)", 0.01, 0.25, 0.08],
  ["thickCrown", "crown", 0.01, 0.3, 0.08],
  ["thickForehead", "forehead", 0.01, 0.3, 0.08],
  ["thickBack", "back of the head", 0.01, 0.3, 0.08],
  ["thickTemple", "temples", 0.01, 0.3, 0.08],
  ["thickBrow", "over the eyes", 0.01, 0.3, 0.08],
  ["thickCheek", "cheeks", 0.01, 0.4, 0.1],
  ["thickBeak", "round the beak's base", 0.01, 0.3, 0.06],
];

const add = (a, b, s = 1) => [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];

// a tube from a to b, its radius going from ra to rb
function cone(x, y, z, a, b, ra, rb) {
  const ab = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], ap = [x - a[0], y - a[1], z - a[2]];
  const t = clamp((ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / (ab[0] ** 2 + ab[1] ** 2 + ab[2] ** 2 || 1), 0, 1);
  return Math.hypot(ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t) - mix(ra, rb, t);
}

// p: the sliders; beak: where the bill's base sits ({ S, by, zb, bc }, as raven.js places it); hinge: the jaw's hinge
export function skull({ p, beak, hinge }) {
  const W = p.skullWidth, H = p.skullHeight, Lz = p.skullLength;
  const { S, by, zb, bc } = beak;
  const bw = S * bc.width(0) / 2, bTop = by + S * bc.Du, bBot = by - S * bc.Dl;

  // ---- the braincase: an egg, narrower at the forehead, with a dome on top, a back bulge and a full forehead ----
  const braincase = (x, y, z) => {
    const taper = 1 - p.skullEgg * clamp(z / Lz, -1, 1) * 0.9 + p.skullEgg * 0.15;
    let d = ellipsoid(x / taper, y, z, [0, 0, 0], [W, H, Lz]);
    if (p.skullDome > 0) d = sunion(d, ellipsoid(x, y, z, [0, H * 0.55 + p.skullDome * 0.6, p.skullTop * Lz], [W * 0.62, 0.35 + p.skullDome, Lz * 0.6]), 0.3);
    if (p.skullBack > 0) d = sunion(d, ellipsoid(x, y, z, [0, H * 0.1, -Lz * 0.72], [W * 0.55, H * 0.5, 0.3 + p.skullBack]), 0.25);
    if (p.skullForehead > 0) d = sunion(d, ellipsoid(x, y, z, [0, H * 0.4, Lz * 0.62], [W * 0.5, H * 0.38, 0.3 + p.skullForehead]), 0.25);
    // the base: flat underneath, the cut dropping away toward the front so the face is kept
    d = ssub(d, y - (p.skullBase * H - Math.max(0, z) * 0.6), 0.15);
    return d;
  };

  // ---- the face's bone: the mount round the bill's base, the bridge from the forehead into the bill's top, and a
  // cheek bar on each side from the mount's lower corner back toward the jaw's hinge (a bird's jugal) ----
  const mountC = [0, (bTop + bBot) / 2, zb - 0.12], mountR = [bw * p.skullMount * 1.05, (bTop - bBot) / 2 * p.skullMount, 0.32];
  const face = (x, y, z) => {
    let d = ellipsoid(x, y, z, mountC, mountR);
    if (p.skullBridge > 0) d = sunion(d, cone(x, y, z, [0, H * 0.45, Lz * 0.75], [0, bTop - 0.02, zb + 0.05], p.skullBridge * 1.2, p.skullBridge), 0.12);
    if (p.skullCheek > 0) {
      const ax = Math.abs(x);
      d = sunion(d, cone(ax, y, z, [bw * 0.75, bBot + 0.25 * (bTop - bBot), zb - 0.1], [W * 0.7, hinge[1] + 0.05, hinge[2] - 0.15], p.skullCheek, p.skullCheek * 0.7), 0.15);
    }
    return d;
  };

  // the skull without its eye sockets (the eyes are placed on the skin over it, then the sockets cut for them)
  const solid = (x, y, z) => sunion(braincase(x, y, z), face(x, y, z), 0.25);
  const temple = (x, y, z, d) => p.skullTemple > 0
    // (a shallow dish: a broad ellipsoid set into the side as deep as the slider says)
    ? ssub(d, ellipsoid(Math.abs(x), y, z, [W + 0.35 - p.skullTemple, 0.0, Lz * 0.1], [0.35, H * 0.42, Lz * 0.38]), 0.12) : d;
  const bone0 = (x, y, z) => temple(x, y, z, solid(x, y, z));
  // what the skin lies on: the bone, with the temple hollow mostly filled by its muscle (the temporalis), so the
  // skin only dips there instead of showing the bone's edge
  const under0 = (x, y, z) => { const d = solid(x, y, z); return mix(temple(x, y, z, d), d, p.skullTempleFill); };

  // ---- the skin's thickness at every point: region values, blended by inverse distance from where each sits ----
  const anchors = (eyes) => {
    const A = [
      [[0, H * 1.02, p.skullTop * Lz], p.thickCrown],
      [[0, H * 0.45, Lz * 0.92], p.thickForehead],
      [[0, H * 0.1, -Lz * 1.0], p.thickBack],
      [mountC, p.thickBeak],
    ];
    for (const s of [1, -1]) {
      A.push([[s * W * 0.98, H * 0.05, Lz * 0.05], p.thickTemple]);
      A.push([[s * (bw + 0.2), bBot + 0.1, zb - 0.35], p.thickCheek]);
      if (eyes) for (const e of eyes) if (Math.sign(e.c[0]) === s) A.push([add(e.c, e.frame[1], e.r * 1.3), p.thickBrow]);
    }
    return A;
  };
  const thicknessWith = (A) => (x, y, z) => {
    let sw = 0, st = 0;
    for (const [q, t] of A) {
      const d2 = (x - q[0]) ** 2 + (y - q[1]) ** 2 + (z - q[2]) ** 2, w = 1 / (d2 * d2 + 1e-4);
      sw += w; st += w * t;
    }
    // (the base value holds everywhere the regions don't reach)
    const near = sw / (sw + 4);
    return mix(p.thickBase, st / sw, near);
  };

  // the outer skin before the eyes: what the eyes are placed on
  const t0 = thicknessWith(anchors(null));
  const outer0 = (x, y, z) => under0(x, y, z) - t0(x, y, z);

  // once the eyes are placed: the sockets, and the thickness with the brows' share
  const withEyes = (eyes) => {
    const sockets = (x, y, z, d) => { for (const e of eyes) d = ssub(d, sphere(x, y, z, e.c, e.r * p.skullSocket), 0.05); return d; };
    const bone = (x, y, z) => sockets(x, y, z, bone0(x, y, z));
    const thickness = thicknessWith(anchors(eyes));
    // (the skin doesn't follow the sockets: the eyeballs fill them, and raven.js opens the skin round each eye)
    return { bone, thickness, outer: (x, y, z) => under0(x, y, z) - thickness(x, y, z) };
  };

  // how far out the skull and its skin reach, with the dome, back and forehead (for the mesh box)
  const tMax = Math.max(...THICK_PARAMS.map(([k]) => p[k]));
  const reach = {
    top: Math.max(H, H * 0.55 + p.skullDome * 0.6 + 0.35 + p.skullDome, H * 0.4 + H * 0.38) + tMax,
    back: Math.max(Lz, Lz * 0.72 + 0.3 + p.skullBack) + tMax,
    side: W * (1 + p.skullEgg * 0.15 + p.skullEgg * 0.9) + tMax,
  };
  return { bone0, outer0, withEyes, reach };
}
