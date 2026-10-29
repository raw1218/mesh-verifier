// A built character as a three.js skinned scene, posed by its rig exactly as the engine does (src/rig/rig.cpp):
// controls are clamped, values map them through their curves, each joint gets its rest transform plus the sum of
// its offsets (rotationEuler in degrees, X then Y then Z, after the rest rotation). Also writes the GLB.

import * as THREE from "three";
import { GLTFExporter } from "three/addons/exporters/GLTFExporter.js";
import { PART_COLOURS } from "./raven.js";

export function toScene(ch) {
  const group = new THREE.Group();
  const bones = ch.joints.map(j => { const b = new THREE.Bone(); b.name = j.name; return b; });
  const world = ch.joints.map(j => {
    const m = new THREE.Matrix4().makeBasis(new THREE.Vector3(...j.frame[0]), new THREE.Vector3(...j.frame[1]), new THREE.Vector3(...j.frame[2]));
    return m.setPosition(...j.at);
  });
  ch.joints.forEach((j, i) => {
    const parent = j.parent ? ch.joints.findIndex(k => k.name === j.parent) : -1;
    const local = parent < 0 ? world[i].clone() : world[parent].clone().invert().multiply(world[i]);
    local.decompose(bones[i].position, bones[i].quaternion, bones[i].scale);
    if (parent < 0) group.add(bones[i]); else bones[parent].add(bones[i]);
  });
  group.updateMatrixWorld(true);
  const rest = bones.map(b => ({ p: b.position.clone(), q: b.quaternion.clone() }));
  const skeleton = new THREE.Skeleton(bones);

  const meshes = ch.parts.map(part => {
    const g = new THREE.BufferGeometry();
    g.setAttribute("position", new THREE.BufferAttribute(part.positions, 3));
    g.setIndex(new THREE.BufferAttribute(part.indices, 1));
    g.setAttribute("skinIndex", new THREE.BufferAttribute(part.joints, 4));
    g.setAttribute("skinWeight", new THREE.BufferAttribute(part.weights, 4));
    const col = new Float32Array(part.labels.length * 3);
    part.labels.forEach((l, i) => col.set(PART_COLOURS[l] || [1, 0, 1], 3 * i));
    g.setAttribute("color", new THREE.BufferAttribute(col, 3));
    // (a part may give its own normals, e.g. a join whose edges must shade like what they lie on)
    if (part.normals) g.setAttribute("normal", new THREE.BufferAttribute(part.normals, 3));
    else g.computeVertexNormals();
    const m = new THREE.SkinnedMesh(g, new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.75, side: THREE.DoubleSide }));
    m.name = part.name;
    m.bind(skeleton, new THREE.Matrix4());
    group.add(m);
    return m;
  });

  const index = Object.fromEntries(ch.joints.map((j, i) => [j.name, i]));
  function pose(values) {
    const v = {};
    for (const c of ch.rig.controls) v[c.name] = Math.min(Math.max(values[c.name] ?? 0, c.min), c.max);
    for (const val of ch.rig.values) v[val.name] = curve(val.map, v[val.control]);
    for (const c of ch.rig.combinations || []) v[c.name] = c.of.reduce((s, n) => s * v[n], 1);
    const dt = bones.map(() => [0, 0, 0]), dr = bones.map(() => [0, 0, 0]);
    for (const o of ch.rig.joints) {
      const x = v[o.from], j = index[o.joint];
      if (o.translation) o.translation.forEach((t, k) => dt[j][k] += t * x);
      if (o.rotationEuler) o.rotationEuler.forEach((r, k) => dr[j][k] += r * x);
    }
    bones.forEach((b, j) => {
      b.position.copy(rest[j].p).add(new THREE.Vector3(...dt[j]));
      const e = new THREE.Euler(...dr[j].map(d => d * Math.PI / 180), "ZYX");
      b.quaternion.copy(rest[j].q).multiply(new THREE.Quaternion().setFromEuler(e));
    });
    group.updateMatrixWorld(true);
  }
  return { group, meshes, bones, pose };
}

function curve(map, x) {
  if (x <= map[0][0]) return map[0][1];
  for (let i = 1; i < map.length; i++) if (x <= map[i][0]) {
    const [a, fa] = map[i - 1], [b, fb] = map[i];
    return fa + (fb - fa) * (x - a) / (b - a);
  }
  return map[map.length - 1][1];
}

// the GLB (at rest), the rig file, and the partition (every vertex's part, per mesh)
export async function exportFiles(ch) {
  const s = toScene(ch);
  s.pose({});
  const scene = new THREE.Scene();
  scene.add(s.group);
  const glb = await new GLTFExporter().parseAsync(scene, { binary: true });
  const partition = Object.fromEntries(ch.parts.map(p => [p.name, p.labels]));
  return { glb, rig: ch.rig, partition };
}
