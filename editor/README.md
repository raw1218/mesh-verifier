# Stylized character editor

A character is built from **parts**. Each part makes its shape and its rig together: the joints that move it, the
skin weights that tie its vertices to them, and the controls that drive them. Sliders change the shapes; the joints'
places come from the same numbers, so the rig always fits and nothing has to be found or painted afterwards. Every
vertex knows which part it came from, so the head is partitioned from the start (`<name>.parts.json`).

This lives here, beside the verifier, and apart from the 2D editor (AnimationStyles): the editor makes meshes and
rigs, and the verifier checks them.

```
python editor/serve.py 8790 --verifier <path to mesh-verify>      then open http://localhost:8790/editor/
```

## How it's put together

| File | What it does |
|---|---|
| `sdf.js` | Shapes as distance fields, smooth union and subtraction, and the mesher (surface nets, then each vertex pulled onto the surface). Plain arrays, no three.js. |
| `parts/beak.js` | The beak: its sliders and presets, its curves, the two bills as ring meshes (`billMesh`) and as distance fields (`billField`), its hinges and how far each bill turns. The one source of the beak: the skull's beak editor (AnimationStyles `tools/chareditor/beak.html`, which loads it at `/mesh/parts/beak.js`) and this editor both use it; each only decides where the beak sits and how it joins on. |
| `parts/mouth.js` | The mouth of a beaked head: two parts, the beak and the mouth corners, and how they attach. The **skin line** (where the skin reaches the beak with the mouth closed, per line of the bill's surface), the **attach points** (the skin line on the cutting edges: where the corners start), the **gape line** (from there back along the face to the corner's end), the slit cut along it, the corner's moving area, and the **join** (the face's skin onto the bill). It gets the face as a field and where the beak sits; nothing else about the head. |
| `raven.js` | The raven's parts: their sliders (`PARAMS`), shapes, joints, skin weights, part labels and rig (`build(params)`). |
| `scene.js` | A built character as a three.js skinned scene, posed by its rig the way the engine does (`src/rig/rig.cpp`); writes the GLB. |
| `index.html` | The editor: sliders on the left, poses and every control on the right, part colours, joints, the Tripo raven beside it. |
| `serve.py` | Serves the page; **Save and verify** writes `out/raven_stylized.{glb,rig.json,parts.json,params.json}`, then checks them with mesh-verify (below). |

## The raven's parts

| Part | Shape | Rig |
|---|---|---|
| Head | cranium, throat and face ellipsoids, smooth-joined; the neck cut open at the bottom | `root`, `head` |
| Beak | the shared beak's two bills (`parts/beak.js`, the same meshes the skull's beak editor makes), scaled and placed on the face with their base a little inside it | `jaw` and `upperBeak` at the beak's own hinges, turning as far as in the beak editor |
| Join (part of the face's mesh, built by `parts/mouth.js`) | along the skin line, where each line of the bill's surface comes out of the face, the face's skin runs round the corner onto the bill: a fillet from a little way out on the face to a little way along the bill, a hair off both, with the face's normals at one end and the bill's at the other, so the two read as one surface. Under it the face is hollowed round the bill, out of sight. At the cutting edges the upper and lower halves part, so the gape runs on into the face | each strip goes from its bill's joint (at the bill) to the face's own weights (where it lands) |
| Mouth corners (`parts/mouth.js`) | each starts at its attach point (where the skin reaches the beak on the cutting edge) and runs back along the face as the gape line, turning down, to the corner's end; the slit is cut along it | `mouthCorner_L/R` at the corner's end; its area is a band along the gape line, held at the beak end and free at the corner's end, so smile, frown and stretch bend the line while it stays attached to the beak |
| Eyes | a ball in a socket cut out of the head, placed by a ray out of the cranium | `eye_L/R` at the ball's centre (look up, down, in, out) |
| Lids | upper and lower shells just outside the ball, inside the socket | `lidUpper`, `lidLower` turn about the eye's side axis (blink, wide, squint; they follow the eye's look a little) |
| Brows (pieces) | a bar swept along the skin over each eye, lifted off it by a gap | `brow_L/R`: down, inner up, outer up |
| Nostrils | small hollows on the upper beak near its base | none |

Controls use ARKit-style names (`jawOpen`, `mouthSmile_L`, `eyeBlink_R`, `browInnerUp`…). The left side is written
once and the right mirrored: each side joint's frame is the other's mirror image turned right-handed, so a right-side
rotation is the left's with its Y and Z angles negated and a translation the left's with X negated.

## The check

Save and verify runs mesh-verify on what the editor made:

1. the rig must load against the GLB (every joint found, names unique);
2. nothing may pass through anything at rest;
3. nothing may pass through anything in each of the page's poses, and with each control alone at its maximum.

The page shows each failing pose and which meshes hit.

## Where the beak meets the face

Measured on the segmented Tripo raven (`models/raven_segmented.glb`: head, beak and eyes as separate parts, placed on
our head) and matched by the "Tripo match" sliders. On that raven the socket the beak plugs into is wide at the gape
(half-width 0.68) and narrow at eye height (0.38); its top runs far forward (z 1.71, the forehead's feathers along the
bill's ridge), its sides come back to just in front of the eye (z 1.21); the gape ends under the eye.

What makes ours do the same:

| Slider (group) | What it is |
|---|---|
| skin forward over the bill's top (Beak junction) | a bridge of skin along the bill's ridge from the forehead, so the skin line is forward on top and back at the sides; it moves with the upper bill |
| cheek pads (Beak junction) | soft masses beside the beak's base under the eyes; the cheek joint (smile) is their muscle, centred on the pad |
| Cross-section: round .. peaked (Beak) | the bill wide at the cutting edges and narrow on top, as the Tripo's is where it meets the face |
| base: how far under the skin (Beak) | each line of the bill starts only this far behind where the skin meets it, so the hidden base follows the face's slant and stays clear of the eyes |
| set forward on the face (Beak) | the whole beak forward or back |
| nostril: how far along the bill (Beak) | where the nostril sits (it's cut in the skin, so on the bridge) |
| ridge: out over the eye, mass up the head (Brows) | the brow ridge's overhang and the mass behind it running up the head |

Checks (run from the repository's root):

- `node editor/checks/junction.mjs [defaults]`: PASS/FAIL for the socket rim (half-width, top, bottom, forward on top,
  back at the sides), the beak tip, the eye (centre, radius), the mouth corner (the gape's end at the back of the
  Tripo's corner patch), the eyes clear of the beak, the bridge moving with the upper bill, the cheek muscle (enough
  skin, none on the socket's rim) and the brows (none of the skin on the bills). All pass on the Tripo match; on the
  defaults the cheek muscle is too small (no cheek pads: the cheek joint then moves only a patch behind the corner).
- `node editor/checks/poses.mjs [match|defaults]`: crossing triangle pairs in every pose, by pair of parts
  (`WHERE=1` for where). On the Tripo match: neutral 19, beak open 83, smile 166, angry 61, sad 42, caw 91 (the old
  match: 144, 229, 482, 184, 130, 262; the defaults are unchanged: 26, 15, 263, 22, 52, 47). What's left is mostly
  the corner mesh against itself, and the cheek's skin folding in a smile.
- `python editor/checks/measure_tripo.py`: remeasures the Tripo raven into `checks/tripo_junction.json`.

## Next

- Brows as bumps (the realistic kind): a ridge in the head's skin, weighted to the brow joint.
- More families: the part list per family as data, so a new species is new data.
- Fit to a reference: the sliders, then small offsets, matched to a concept's Tripo mesh (and its hand-checked part
  groups), so the result is our own mesh with a clean rig.
- Later: the anatomical track (skull, muscles, skin) drives the same control names.
