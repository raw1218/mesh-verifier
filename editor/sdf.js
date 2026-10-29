// Distance-field shapes and a mesher. A part is a function p -> signed distance (negative inside); parts are joined
// with smooth unions and cut with smooth subtractions, then the whole field is meshed once with surface nets and
// every vertex pulled back onto the surface. Plain arrays, no three.js, so it can run anywhere.

export const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
export const smoothstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
export const mix = (a, b, t) => a + (b - a) * t;

// polynomial smooth min/max (k: the blend's width, in the field's units)
export function sunion(a, b, k) {
  if (k <= 0) return Math.min(a, b);
  const h = clamp(0.5 + 0.5 * (b - a) / k, 0, 1);
  return mix(b, a, h) - k * h * (1 - h);
}
export function ssub(a, b, k) {            // a minus b
  if (k <= 0) return Math.max(a, -b);
  const h = clamp(0.5 - 0.5 * (a + b) / k, 0, 1);
  return mix(a, -b, h) + k * h * (1 - h);
}

// an ellipsoid centred at c with radii r (a close, cheap bound, good enough near the surface)
export function ellipsoid(px, py, pz, c, r) {
  const x = (px - c[0]) / r[0], y = (py - c[1]) / r[1], z = (pz - c[2]) / r[2];
  const k0 = Math.hypot(x, y, z);
  const k1 = Math.hypot(x / r[0], y / r[1], z / r[2]);
  return k1 > 0 ? k0 * (k0 - 1) / k1 : -Math.min(...r);
}

export function sphere(px, py, pz, c, r) {
  return Math.hypot(px - c[0], py - c[1], pz - c[2]) - r;
}

// Surface nets over a box: one vertex per cell the surface crosses, one quad per grid edge it crosses (two
// triangles, split along the shorter diagonal). Then each vertex is pulled onto the surface by a few Newton steps
// along the field's gradient. Faces point outward (from negative to positive).
export function mesh(f, lo, hi, h, project = 3) {
  const nx = Math.ceil((hi[0] - lo[0]) / h) + 1, ny = Math.ceil((hi[1] - lo[1]) / h) + 1, nz = Math.ceil((hi[2] - lo[2]) / h) + 1;
  const at = (i, j, k) => (k * ny + j) * nx + i;
  const val = new Float32Array(nx * ny * nz);
  for (let k = 0; k < nz; k++) for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++)
    val[at(i, j, k)] = f(lo[0] + i * h, lo[1] + j * h, lo[2] + k * h);

  const cellVert = new Int32Array((nx - 1) * (ny - 1) * (nz - 1)).fill(-1);
  const cat = (i, j, k) => (k * (ny - 1) + j) * (nx - 1) + i;
  const P = [];
  const EDGES = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
  const corner = new Float32Array(8);
  for (let k = 0; k < nz - 1; k++) for (let j = 0; j < ny - 1; j++) for (let i = 0; i < nx - 1; i++) {
    let neg = 0;
    for (let c = 0; c < 8; c++) {
      corner[c] = val[at(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))];
      if (corner[c] < 0) neg++;
    }
    if (neg === 0 || neg === 8) continue;
    let sx = 0, sy = 0, sz = 0, n = 0;
    for (const [a, b] of EDGES) {
      const va = corner[a], vb = corner[b];
      if ((va < 0) === (vb < 0)) continue;
      const t = va / (va - vb);
      sx += (a & 1) + ((b & 1) - (a & 1)) * t;
      sy += ((a >> 1) & 1) + (((b >> 1) & 1) - ((a >> 1) & 1)) * t;
      sz += ((a >> 2) & 1) + (((b >> 2) & 1) - ((a >> 2) & 1)) * t;
      n++;
    }
    cellVert[cat(i, j, k)] = P.length / 3;
    P.push(lo[0] + (i + sx / n) * h, lo[1] + (j + sy / n) * h, lo[2] + (k + sz / n) * h);
  }

  const T = [];
  const quad = (a, b, c, d) => {               // a b c d around the edge, counter-clockwise seen from outside
    if (a < 0 || b < 0 || c < 0 || d < 0) return;
    const d2 = (u, v) => (P[3 * u] - P[3 * v]) ** 2 + (P[3 * u + 1] - P[3 * v + 1]) ** 2 + (P[3 * u + 2] - P[3 * v + 2]) ** 2;
    if (d2(a, c) <= d2(b, d)) T.push(a, b, c, a, c, d); else T.push(a, b, d, b, c, d);
  };
  for (let k = 1; k < nz - 1; k++) for (let j = 1; j < ny - 1; j++) for (let i = 0; i < nx - 1; i++) {   // x edges
    const v0 = val[at(i, j, k)], v1 = val[at(i + 1, j, k)];
    if ((v0 < 0) === (v1 < 0)) continue;
    const q = [cellVert[cat(i, j - 1, k - 1)], cellVert[cat(i, j, k - 1)], cellVert[cat(i, j, k)], cellVert[cat(i, j - 1, k)]];
    if (v0 < 0) quad(q[0], q[1], q[2], q[3]); else quad(q[3], q[2], q[1], q[0]);
  }
  for (let k = 1; k < nz - 1; k++) for (let j = 0; j < ny - 1; j++) for (let i = 1; i < nx - 1; i++) {   // y edges
    const v0 = val[at(i, j, k)], v1 = val[at(i, j + 1, k)];
    if ((v0 < 0) === (v1 < 0)) continue;
    const q = [cellVert[cat(i - 1, j, k - 1)], cellVert[cat(i - 1, j, k)], cellVert[cat(i, j, k)], cellVert[cat(i, j, k - 1)]];
    if (v0 < 0) quad(q[0], q[1], q[2], q[3]); else quad(q[3], q[2], q[1], q[0]);
  }
  for (let k = 0; k < nz - 1; k++) for (let j = 1; j < ny - 1; j++) for (let i = 1; i < nx - 1; i++) {   // z edges
    const v0 = val[at(i, j, k)], v1 = val[at(i, j, k + 1)];
    if ((v0 < 0) === (v1 < 0)) continue;
    const q = [cellVert[cat(i - 1, j - 1, k)], cellVert[cat(i, j - 1, k)], cellVert[cat(i, j, k)], cellVert[cat(i - 1, j, k)]];
    if (v0 < 0) quad(q[0], q[1], q[2], q[3]); else quad(q[3], q[2], q[1], q[0]);
  }

  // drop unused vertices (cells at the box's edge whose quads were never made)
  const used = new Int32Array(P.length / 3).fill(-1);
  const V = [];
  for (const v of T) if (used[v] < 0) { used[v] = V.length / 3; V.push(P[3 * v], P[3 * v + 1], P[3 * v + 2]); }
  const F = T.map(v => used[v]);

  // onto the surface (but never further than a cell: thin gaps must not close)
  const e = h * 0.05;
  const before = V.slice();
  for (let s = 0; s < project; s++) for (let v = 0; v < V.length; v += 3) {
    const x = V[v], y = V[v + 1], z = V[v + 2];
    const d = f(x, y, z);
    const gx = (f(x + e, y, z) - f(x - e, y, z)) / (2 * e), gy = (f(x, y + e, z) - f(x, y - e, z)) / (2 * e), gz = (f(x, y, z + e) - f(x, y, z - e)) / (2 * e);
    const g2 = gx * gx + gy * gy + gz * gz;
    if (g2 < 1e-12) continue;
    const step = clamp(d / g2, -0.5 * h / Math.sqrt(g2), 0.5 * h / Math.sqrt(g2));
    V[v] = x - gx * step; V[v + 1] = y - gy * step; V[v + 2] = z - gz * step;
  }
  // at sharp creases the pull can tip a triangle over (it then faces into the shape, and its neighbours cross
  // it): its corners go back to where the mesher put them, until none is tipped
  for (let pass = 0; pass < 4; pass++) {
    const back = new Set();
    for (let t = 0; t < F.length; t += 3) {
      const [a, b, c] = [F[t], F[t + 1], F[t + 2]].map(i => 3 * i);
      const u = [V[b] - V[a], V[b + 1] - V[a + 1], V[b + 2] - V[a + 2]], w = [V[c] - V[a], V[c + 1] - V[a + 1], V[c + 2] - V[a + 2]];
      const n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
      const m = [0, 1, 2].map(k => (V[a + k] + V[b + k] + V[c + k]) / 3);
      const g = [f(m[0] + e, m[1], m[2]) - f(m[0] - e, m[1], m[2]), f(m[0], m[1] + e, m[2]) - f(m[0], m[1] - e, m[2]), f(m[0], m[1], m[2] + e) - f(m[0], m[1], m[2] - e)];
      if (n[0] * g[0] + n[1] * g[1] + n[2] * g[2] < 0) for (const i of [a, b, c]) back.add(i);
    }
    if (!back.size) break;
    for (const i of back) for (let k = 0; k < 3; k++) V[i + k] = before[i + k];
  }
  return { positions: new Float32Array(V), indices: new Uint32Array(F) };
}

// a lid: part of a sphere as a band round its x axis (the axis a lid turns about to blink), from pitch p0 to p1
// (about x, from z toward y; it may go past 90 degrees, over the top and back), and from -latMax to latMax toward
// +-x. Its poles are on the x axis, outside the band, so the band never folds onto itself.
export function lidShell(c, axes, r, p0, p1, latMax, nP, nL) {
  const [X, Y, Z] = axes;
  const V = [], F = [];
  for (let a = 0; a <= nP; a++) {
    const p = mix(p0, p1, a / nP);
    for (let b = 0; b <= nL; b++) {
      const t = mix(-latMax, latMax, b / nL);
      const lx = Math.sin(t), ly = Math.cos(t) * Math.sin(p), lz = Math.cos(t) * Math.cos(p);
      V.push(c[0] + r * (X[0] * lx + Y[0] * ly + Z[0] * lz), c[1] + r * (X[1] * lx + Y[1] * ly + Z[1] * lz), c[2] + r * (X[2] * lx + Y[2] * ly + Z[2] * lz));
    }
  }
  const row = nL + 1;
  for (let a = 0; a < nP; a++) for (let b = 0; b < nL; b++) {
    const i = a * row + b;
    F.push(i, i + 1, i + row + 1, i, i + row + 1, i + row);
  }
  return { positions: V, indices: F };
}

// a UV sphere (or the part of one between two pitch angles), in a frame: origin c, axes x, y, z (z: the
// pole-free "forward"; pitch is the angle about x, from z toward y; yaw about y)
export function sphereBand(c, axes, r, pitch0, pitch1, yaw0, yaw1, nPitch, nYaw) {
  const [X, Y, Z] = axes;
  const V = [], F = [];
  for (let a = 0; a <= nPitch; a++) {
    const p = mix(pitch0, pitch1, a / nPitch);
    for (let b = 0; b <= nYaw; b++) {
      const w = mix(yaw0, yaw1, b / nYaw);
      const lx = Math.cos(p) * Math.sin(w), ly = Math.sin(p), lz = Math.cos(p) * Math.cos(w);
      V.push(c[0] + r * (X[0] * lx + Y[0] * ly + Z[0] * lz), c[1] + r * (X[1] * lx + Y[1] * ly + Z[1] * lz), c[2] + r * (X[2] * lx + Y[2] * ly + Z[2] * lz));
    }
  }
  const row = nYaw + 1;
  for (let a = 0; a < nPitch; a++) for (let b = 0; b < nYaw; b++) {
    const i = a * row + b;
    F.push(i, i + 1, i + row + 1, i, i + row + 1, i + row);
  }
  return { positions: V, indices: F };
}
