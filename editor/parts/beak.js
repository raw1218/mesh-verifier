// The beak: one source for its shape, used by every editor that has one. Nothing here knows what the beak is
// attached to (a skull, a stylized head); the editor that uses it places it and joins it on.
//
// Beak space: x runs from the base (0) along the bill to the tip (L), y to the side, z up; lengths are relative
// to the bill's length (1 at the raven's default). Both bills come from one set of curves and share the gape
// line, so they always meet when closed.
//
//   curves(p)               the shape's curves
//   billMesh(p, which)      the bill ("upper" or "lower") as rings: { positions, indices } (flat arrays)
//   archPoint(c, which, t, i)  one point on a bill's outer surface, for joining things onto it
//   billField(p, which)     the same bill as a signed distance (negative inside), for editors that blend shapes
//   hinges(c)               where the bills turn when the beak opens, when nothing else says
//   OPEN                    how far each bill turns at full open (degrees)

export const PARAMS = [
  // key, label, min, max, raven default
  ["length", "Length", 0.6, 1.4, 1.0],
  ["depth", "Depth at base", 0.2, 0.6, 0.40],
  ["width", "Width at base", 0.15, 0.5, 0.30],
  ["lowerShare", "Lower bill share", 0.25, 0.55, 0.42],
  ["fullness", "Fullness", 1.0, 3.0, 1.9],
  ["arch", "Top ridge arch", 0.0, 0.6, 0.15],
  ["hook", "Hook", 0.0, 0.25, 0.11],
  ["hookLen", "Hook length", 0.05, 0.35, 0.2],
  ["gapeTilt", "Gape tilt", -0.1, 0.25, 0.08],
  ["gapeCurve", "Gape curve", -0.15, 0.15, 0.0],
  ["sharp", "Tip sharpness", 0.6, 2.2, 1.1],
  ["gonys", "Lower ridge angle", -0.15, 0.25, 0.08],
  // the cross-section's shape: round (an arch, the default) to peaked (wide at the cutting edges, narrow up top, as the
  // Tripo raven's is where it meets the face, clear of the eyes)
  ["peak", "Cross-section: round .. peaked", 0.85, 3, 0.85],
];
export const DEFAULTS = Object.fromEntries(PARAMS.map(p => [p[0], p[4]]));
export const PRESETS = {
  Raven: {},
  Crow: { length: 0.9, depth: 0.33, width: 0.27, fullness: 1.7, arch: 0.1, hook: 0.06, hookLen: 0.16, sharp: 1.2 },
  Rook: { length: 1.0, depth: 0.28, width: 0.22, fullness: 1.4, arch: 0.02, hook: 0.02, hookLen: 0.1, sharp: 1.5, gapeTilt: 0.05 },
  Jackdaw: { length: 0.65, depth: 0.42, width: 0.34, fullness: 2.2, arch: 0.15, hook: 0.05, hookLen: 0.15, sharp: 0.95 },
  Chough: { length: 1.25, depth: 0.22, width: 0.18, fullness: 1.3, arch: 0.0, hook: 0.0, gapeCurve: -0.1, sharp: 1.6, gonys: -0.05 },
};

// jaw drops; the quadrate linkage tips the upper bill up a quarter as much (prokinesis)
export const OPEN = { lower: 24, upper: -6 };

export const N_RING = 64, N_ARCH = 24, N_FLOOR = 8;
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };

// p.size (optional): what the base's depth and width, the hook and the hinges are relative to. Left out, they go with
// the length (the whole beak scales); a head that sets it (raven.js: 1) keeps its base and hinges where they are, so
// the length only grows the bill outward.
export function curves(p) {
  const L = p.length, size = p.size ?? L;
  const D = p.depth * size, W = p.width * size;
  const Du = D * (1 - p.lowerShare), Dl = D * p.lowerShare;
  const gapeZ = t => -p.gapeTilt * L * t + p.gapeCurve * L * 4 * t * (1 - t);
  // a full (convex) profile: the bill stays deep for most of its length, then curves down to the tip
  const taper = (t, full) => Math.pow(Math.max(0, 1 - Math.pow(Math.min(1, t), full)), 0.6 * p.sharp);
  const upperDepth = t => Du * taper(t, p.fullness) * (1 + p.arch * Math.sin(Math.PI * Math.min(t, 0.999)) * 0.8);
  const hookDrop = t => -p.hook * size * Math.pow(smooth(1 - p.hookLen, 1, t), 2);
  const width = t => W * taper(t, p.fullness * 0.85) + 0.004 * size;
  const lowerEnd = 1 - p.hookLen * 0.55;                         // the lower tip sits under the hook
  const lowerDepth = t => Dl * Math.pow(taper(t / lowerEnd, p.fullness), 1 + p.gonys);
  return { L, size, Du, Dl, W, gapeZ, upperDepth, hookDrop, width, lowerEnd, lowerDepth, peak: p.peak ?? 0.85 };
}

// One ring per station along the bill: the outer arch from one cutting edge to the other, then back along
// the inside (palate or trough) to close the loop. `prefix`: extra rings (arrays of [x, y, z], the same point
// count as a bill ring) placed before the base, e.g. to grow a bone out of a mount on a skull.
export function billMesh(p, which, prefix = []) {
  const c = curves(p);
  const pos = prefix.flat(2);
  const tEnd = which === "upper" ? 1 : c.lowerEnd;
  for (let r = 0; r <= N_RING; r++) {
    const t = (r / N_RING) * tEnd;
    const x = t * c.L;
    const g = c.gapeZ(t);
    const shrink = which === "upper" ? 1 : 0.94;                 // the upper bill's edges overlap the lower's
    const hw = Math.max(1e-4, c.width(t) / 2 * shrink);
    // (both bills dip with the hook, so the lower tip stays under it instead of the hook cutting through it)
    const drop = c.hookDrop(t);
    const d = Math.max(1e-4, which === "upper" ? c.upperDepth(t) : c.lowerDepth(t));
    const thick = Math.min(d * 0.35, 0.025 * c.size);
    const sgn = which === "upper" ? 1 : -1;
    if (r === N_RING) {                                          // the tip: one point (a ring that small folds)
      for (let i = 0; i < N_ARCH + N_FLOOR; i++) pos.push(x, 0, g + drop);
      continue;
    }
    for (let i = 0; i < N_ARCH; i++) pos.push(...archPoint(c, which, t, i));   // outer surface, edge to edge
    for (let j = 1; j <= N_FLOOR; j++) {                         // inside surface, back to the first edge
      const u = j / (N_FLOOR + 1);
      // (its height into the bill, and no wider than the arch is at that height: a peaked arch is narrow up there)
      const hf = thick + (d - 2 * thick) * 0.55 * (1 - Math.abs(2 * u - 1)) * Math.min(1, 0.85 / c.peak);
      const wf = Math.sqrt(Math.max(0, 1 - Math.pow(Math.min(1, hf / d), 2 / c.peak)));
      const y = ((which === "upper" ? -hw : hw) + u * 2 * (which === "upper" ? hw : -hw)) * Math.min(1, wf / 0.95);
      const z = g + drop + sgn * hf;
      pos.push(x, y, z);
    }
  }
  const ring = N_ARCH + N_FLOOR;
  const idx = [];
  for (let r = 0; r < N_RING + prefix.length; r++)
    for (let i = 0; i < ring; i++) {
      const a = r * ring + i, b = r * ring + (i + 1) % ring, a2 = a + ring, b2 = b + ring;
      idx.push(a, a2, b, b, a2, b2);
    }
  // cap the base: the ring is a crescent (the arch over the palate), so a fan from its middle would overlap
  // itself; zip the arch to the palate instead, edge to edge
  const top = [...Array(N_ARCH).keys()];
  const bot = [0, ...[...Array(N_FLOOR).keys()].map(j => N_ARCH + N_FLOOR - 1 - j), N_ARCH - 1];
  for (let i = 0, k = 0; i < top.length - 1 || k < bot.length - 1;) {
    if (k >= bot.length - 1 || (i < top.length - 1 && (i + 1) / (top.length - 1) <= (k + 1) / (bot.length - 1))) {
      idx.push(bot[k], top[i + 1], top[i]); i++;
    } else {
      idx.push(top[i], bot[k], bot[k + 1]); k++;
    }
  }
  return { positions: pos, indices: idx, c };
}

// A point on a bill's outer arch: station t (0 base, 1 tip), i from one cutting edge (0) to the other (N_ARCH - 1).
// Also for editors that join something onto the bill's surface.
export function archPoint(c, which, t, i) {
  const upper = which === "upper";
  const hw = Math.max(1e-4, c.width(t) / 2 * (upper ? 1 : 0.94));
  const d = Math.max(1e-4, upper ? c.upperDepth(t) : c.lowerDepth(t));
  const th = Math.PI * i / (N_ARCH - 1);
  return [t * c.L, hw * Math.cos(th) * (upper ? 1 : -1), c.gapeZ(t) + c.hookDrop(t) + (upper ? 1 : -1) * d * Math.pow(Math.sin(th), c.peak)];
}

// The same bill as a distance field: at each station, half an ellipse on the gape line (above it for the upper
// bill, below for the lower), as wide and deep as the rings. Solid bills (the rings' palate and trough are
// for the sheath's inside; a field shape has no inside). Past the tip, the distance to the tip's section;
// before the base, the base's section carried straight back (`back`: true) or the base closed off.
export function billField(p, which, { back = true } = {}) {
  const c = curves(p);
  const upper = which === "upper";
  const tEnd = upper ? 1 : c.lowerEnd;
  return (x, y, z) => {
    const s = x / c.L;
    const t = Math.min(Math.max(s, 0), tEnd);
    const hw = Math.max(1e-3 * c.L, c.width(t) / 2 * (upper ? 1 : 0.94));
    const d = Math.max(1e-3 * c.L, upper ? c.upperDepth(t) : c.lowerDepth(t));
    const g = c.gapeZ(t) + c.hookDrop(t);
    const h = upper ? z - g : g - z;                             // height away from the gape, into the bill
    let dc;
    if (c.peak <= 1) {
      const q = Math.sqrt((y / hw) ** 2 + (Math.max(h, 0) / d) ** 2);
      dc = Math.max((q - 1) * Math.min(hw, d), -h);              // half an ellipse: the ellipse, and its own side of the gape
    } else {
      // peaked: at height h the section is as wide as the rings are there (x = hw cos th where h = d sin(th)^peak)
      const up = Math.min(1, Math.max(0, h / d)), wAt = hw * Math.sqrt(Math.max(0, 1 - Math.pow(up, 2 / c.peak)));
      dc = Math.max(Math.abs(y) - wAt, h - d, -h);
    }
    if (s > tEnd) return Math.sqrt(Math.max(dc, 0) ** 2 + ((s - tEnd) * c.L) ** 2);
    if (s < 0 && !back) return Math.max(dc, -x);
    return dc;
  };
}

// where the bills turn: the jaw well behind and below the base, the upper bill's hinge at the forehead
export function hinges(c) {
  return { jaw: [-0.32 * c.size, 0, c.gapeZ(0) - 0.12 * c.size], fore: [-0.05 * c.size, 0, c.gapeZ(0) + c.Du * 0.9] };
}
