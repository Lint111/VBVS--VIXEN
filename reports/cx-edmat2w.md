# cx-edmat2w — material pixels witness

Base before this lane: `122652df08d53875bceb5c43f26fb9a3b0297eae` (`lane-edmat2`). The pack witness used the `matkind1` material table at `a8c6b1731` paired with kernel `41c23511c`. The editor was windowed on X11 at 1280×720; Dozen was the queued GPU device.

Evidence root: `/home/liory/codeman-cases/undertow/reports/cx-edmat2w-evidence/`.

## RESULT

The editor path now shows the stored material body after a material edit. B, C, and D each became visible two presented frames after dispatch and produced zero shader/procedural/pipeline compile lines in their dispatch-to-readback log interval. Undoing the three edits returns the viewport image exactly to A’s semantic hash.

| action | compiles counted | frames to visible | viewport hash | image | what the image shows |
|---|---:|---:|---:|---|---|
| A neutral | n/a | n/a | `433215457901572519` | `editor/A-neutral.png` | neutral procedural box/bulge, flat light gray |
| B major 5 / minor 6 / 70% | 0 | 2 | `15118269256192089327` | `editor/B-hull-major-minor-70.png` | stored rounded hull with dark blue-gray major and light gray minor on one side |
| C major 5 / emissive trim 8 / 70% | 0 | 2 | `17689236024572595027` | `editor/C-hull-major-emissive-trim-70.png` | same hull with a dark/black major field and a tan trim edge |
| D major 5 / emissive trim 8 / 40% | 0 | 2 | `17689236024572595027` | `editor/D-hull-major-emissive-trim-40.png` | pixel-identical to C on this small document; the percent edit was accepted but moved no sampled surface boundary |
| undo 1 | 0 observed in log | 2 | `17689236024572595027` | `editor/undo-1.png` | C-like stored image while two edits remain |
| undo 2 | 0 observed in log | 2 | `15118269256192089327` | `editor/undo-2.png` | B-like stored image while one edit remains |
| undo 3 | 0 observed in log | 2 | `433215457901572519` | `editor/undo-3.png` | neutral image; viewport hash exactly matches A |

The full-frame hashes differ across undo states because the status bar contains the changing undo/redo depth. The semantic hashes above are the `screenRect` readback of the 620×692 viewport, excluding dock chrome. The full frame is retained in `editor/witness.json`.

The channel-visible material rows were:

`1 core:rock`, `2 core:regolith`, `3 core:ice`, `4 core:vegetation`, `5 core:hull_metal_major`, `6 core:hull_metal_minor`, `7 core:dark_trim`, `8 core:emissive_trim`.

The generated `EditorLayers_materials` noun is catalogued but has no scalar Data action; the same eight rows are therefore recorded by the new top-level `appflow.read.materials` projection in `editor/witness.json`.

Dozen look-dev correctness captures, all four admitted and passing after four presentation frames:

| state | PNG SHA-256 | image |
|---|---|---|
| midday | `1706f4080aeeecdbdc6daf38940734cdaea7d56601b04ae61ca7dd25f0eed887` | `lookdev-dozen-fixed/midday-angle-0.png` |
| late-afternoon | `1d68bcafa89c85a757f0c25c819c25a043a76085c423bf1a95e68bce77a2f0d8` | `lookdev-dozen-fixed/late-afternoon-angle-0.png` |
| overcast | `e26454f41a5344823a73b5bdfb454d9cdffac5094e1a5b4e8f1fd25517096a78` | `lookdev-dozen-fixed/overcast-angle-0.png` |
| night-service | `89b4d4fca8db05e2622c2b3a981780cbbf099191d95fad0db6703ef671e866d3` | `lookdev-dozen-fixed/night-service-angle-0.png` |

The loose-document readback is `loose/loose-neutral.png`, frame 4, full-frame hash `2260940479735578127`. Its located diagnostic is `MaterialTable`: `material table unavailable: the editor is rendering a loose document`.

## LOOK FINDINGS

- B visibly has two materials on one asset: the broad dark major surface and the bright minor band are separate and intentional-looking, with a hard vertical boundary rather than speckle. The measured stored-hull call is `major=0.764706`, `minor=0.235294` (`2301/708` voxels), which agrees qualitatively with the picture’s broad-major/narrow-minor split but is closer to 76/24 than the nominal 70/30. See `editor/B-hull-major-minor-70.png` and the coverage line in `1794609120-gpu-cx-edmat2w:lookdev-dozen-four-states-fixed.log`.
- C’s emissive-trim state is visibly distinct at the left edge, but it reads as a warm brown/tan band rather than the flat row color `#F3D7AD`; a dominant rendered patch is `(171,134,116)` versus `(243,215,173)`. That is a large lighting/shading difference, not a color-space-only rounding error. The major sample `(39,49,54)` is close to `#2A3234` `(42,50,52)`; the minor sample `(211,212,212)` is within about 3% of `#D9DBDB` `(217,219,219)`. See `editor/C-hull-major-emissive-trim-70.png`.
- D is the important negative finding: `SetDocumentMaterials(5,8,40)` succeeds, rematerializes, and does not compile, but the viewport is byte-identical to C. The rule is a plane offset derived at `VIXEN/libraries/VoxelDocument/src/EditorDocumentModel.cpp:1451`; this 64³, small two-layer fixture does not produce a sampled surface change between the 70% and 40% plane positions. This should be judged as a content/rule-resolution issue, not tuned in this lane.
- The editor images have no visible banding or frame-to-frame flicker in the viewport states. The only changing pixels outside the asset are the expected status-bar undo/redo labels. The first pre-fix B–D images were blank because the stored bake remained in its grid-centered frame while the camera stayed at the procedural origin; this was corrected by the editor affine placement in `EditorApplication.cpp:1608-1624`.
- Dozen now shows a complete scene in all four states. Midday and late-afternoon have bright yellow ground and green foliage; overcast is paler/cooler; night-service is dark but still readable. The first blank capture was not a settle/warm-up problem: the original run already rendered four presentation frames and reported the coverage call. The stored look-dev fixture had routed every recipe through the stored provider and left the hull’s grid-space bake outside the ordinary camera frame. The fix is in `LookdevSceneDefinition.h:474-502`.
- The loose capture shows the neutral default body and the status diagnostic in the bottom bar; it does not pretend that a pack palette exists for a loose file.

Contact sheets committed with the lane are `reports/cx-edmat2w-visual/editor-material-states.png` and `reports/cx-edmat2w-visual/dozen-lookdev-states.png`.

## FIXES

- Added the pack material-row projection to `appflow.read`, preserving slot ids and names for channel witnesses.
- Mapped the editor’s stored material bake back into the existing camera frame with the established 64-cell/10-unit/scale-5 convention. This is why the stored body is now visible while preserving the zero-recompile edit path.
- Made the look-dev stored fixture route only its hull recipe through the stored provider and apply the matching grid-to-world scale; the other look-dev recipes remain procedural.
- Added the loose-document witness and semantic viewport-hash capture tooling under the evidence root.

Commit: `c0d715c20` (`witness material edits in editor and lookdev pixels`).

## LANDABLE NOW

The lane tip contains the source fixes, SPT inbox entry, contact sheets, and this report. The required target build passed, the editor and Dozen GPU witnesses passed, and the semantic A→undo-3 image round-trip passed.

## REDS

- Initial editor GPU invocation at base `122652df0`: queued command exited 255 before channel startup because `DISPLAY` was unset. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1794607947-gpu-cx-edmat2w:editor-material-windowed.log`, diagnostic `[WindowNode] glfwInit failed: X11: The DISPLAY environment variable is missing`. Recovery rung A succeeded by using the documented live queue with `DISPLAY=:0`, `WAYLAND_DISPLAY` unset, and the Dozen ICD; retry log: `1794608056-gpu-cx-edmat2w:editor-material-windowed-retry.log`.
- The required final full CTest was run through the box queue and finished 99% passing: 1789 executed, one failure, exit 8. Failing check: `LODRayCastingTest.NoPerformanceRegressionWithoutLOD`, `test_lod.cpp:418`, observed `lodMs=0.196092` versus `regularMs*1.2=0.171818`. Raw log: `/home/liory/.local/state/undertow/undertow-box-logs/1794610207-test-cx-edmat2w:full-ctest-final.log`. This is an unclassified pre-existing/out-of-scope SVO performance red: its source/link target is `VIXEN/libraries/SVO/tests/test_lod.cpp`, outside the changed editor/look-dev targets; the scoped editor build, editor GPU witness, Dozen GPU witness, and VoxelDocument tests in the full run passed. No claim of a base before/after delta is made.

## SPT DISPOSITION

- Appended to `.spt-proposals/cx-edmat2w.jsonl`: `3aeef7760221` (DISPLAY provisioning), `66a00630c879` (readable collection-shaped AppFlow materials view), and `bfe1ccd4b817` (declarative stored look-dev affine/provider mapping).

## STOPs

- None for the requested scoped evidence. The full-suite SVO performance red is recorded above and is not attributed to this lane.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: windowed editor capture needs explicit DISPLAY provisioning
- proposed: AppFlow materials collection lacks a readable view noun contract
- proposed: stored look-dev hull requires hand-authored affine placement
