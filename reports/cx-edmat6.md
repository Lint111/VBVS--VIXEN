# cx-edmat6 — stored material hull diagnosis

## ROOT CAUSE

There are two independent defects in the stored path.

1. The initial wrong silhouette was a bake-frame/unit defect, not a preview-slot alias. The pre-fix CPU witness, using the old unit contract, measured `max |stored-neutral| = 0.450405` on the fixed 17^3 grid, with the stored center sample having the wrong sign (`storedAtReferenceMax=-0.0582472`, `referenceAtStoredMax=0.35`, `storedNegative=0`). The editor was also using a stored grid whose recipe-unit scale did not match the evaluator and whose sub-voxel filter could cross the center-sample iso sign. The fix is in `VIXEN/libraries/SVO/include/SdfBake.h:229` and `:517`, with the editor contract set at `VIXEN/application/editor/source/EditorApplication.cpp:2588-2596`; distance evaluation now maps recipe units explicitly and the filtered center value cannot cross the sampled sign. The stored placement conversion is centralized in `VIXEN/libraries/VoxelDocument/include/EditorDocumentFraming.h:31-61`.

2. The missing materials are a generated-consumer contract gap. The material program is present and contains the expected `MaterialRef`/`Attach` opcodes (`117`/`118`, 11 instructions versus 7 for neutral), but the VIXEN-side `--recipe-simd-cpp` invocation at `VIXEN/codegen/CMakeLists.txt:895-912` does not request/materialize the MaterialId output channel. The generated program reports one output (`OutputCount()=1`), so `BakeRegistryToPool` cannot produce an output set; the live editor log consequently reports `[EDITOR/material-coverage] total=0` after every bake. `StoredSdf.glsl` can decode MaterialId, but no stored samples reach it. The VIXEN baker's output-set path validates the required output width at `VIXEN/libraries/SVO/include/Recipe/RecipeBaker.h:112-143`; the current generated program never reaches a valid material output set. The missing generated channel must be added through the kernel/codegen contract, not by editing generated files.

The slot lead was disproven by measurement: the final editor log reports `pool=1 previewRecipe=1 previewSlot=0 slot0Recipe=1`. The instance points to the preview recipe's actual slot.

## LANDABLE NOW

The VIXEN-side frame/unit fix, displayed-slot guard, CPU diagnosis, coverage logging, UI fixes, and tests are committed in the tip reported below. Pairing order remains kernel `23f8eb553236df9908b1cc735fc4b38cc4a8e9e3` → Undertow `1ef8cb5362901c1ff83a3959892e5b0ea554d382` → VIXEN. The material-output contract is not landable as a complete product result until the kernel/codegen owner exposes a supported MaterialId output/carry channel to the consumer generator and the generated header is regenerated.

## BEFORE / AFTER

Before: [neutral baseline]( /home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/02-neutral.png ) was the correct box/bulge/round-cut hull; [two-material baseline]( /home/liory/codeman-cases/undertow/reports/cx-edmat5-evidence/editor-final9-1280/05-two-materials.png ) was the wrong four-point concave star, with no hole and no requested material evidence.

After the frame fix: [1280 neutral]( /home/liory/codeman-cases/undertow/reports/cx-edmat6-evidence/editor-final64-1280/02-neutral.png ) and [1280 stored]( /home/liory/codeman-cases/undertow/reports/cx-edmat6-evidence/editor-final64-1280/05-two-materials.png ) both show the intended hull silhouette, hole, and bulge. The stored frame still has no visible hard material boundary because the generated bake has no MaterialId samples. The same producer check passed at [1920 neutral]( /home/liory/codeman-cases/undertow/reports/cx-edmat6-evidence/editor-final64-1920/02-neutral.png ) and [1920 stored]( /home/liory/codeman-cases/undertow/reports/cx-edmat6-evidence/editor-final64-1920/05-two-materials.png ). The rows fit and the wrap picker is reachable/collapsed in all cited frames.

## WITNESS

| Check | Result |
|---|---|
| Material flatten vs neutral flatten | `max=0` on 17^3; first max at `(0,0,0)`; materials do not alter distance |
| Stored bake vs neutral flatten | `max=0.0592727` at `(-0.941025,-0.941025,-0.941025)`; neutral `-0.0589746`, stored `0.000298039` |
| Material bake vs neutral bake | `max=0` |
| Stored/reference sign parity | `storedNegative=3060`, `referenceNegative=3060`, `signMismatch=0` |
| Stored grid/serialization | `4913` grid samples; serialized channel count `4`, stride `3072`; serialized-vs-world `max=0` |
| Material output set | `resolution=0`, `values=0`, `samplesById=5:0,8:0`; editor coverage `total=0` |
| Displayed slot | `pool=1`, `previewRecipe=1`, `previewSlot=0`, `slot0Recipe=1` |
| GPU mechanics | 1280 and 1920 action witnesses passed: material edit changed within two frames, compile count `0`, undo and save/reopen checks passed |
| 1280 silhouette | neutral bbox `(583,284)-(724,425)`, stored `(584,288)-(721,425)`, differing mask pixels `1470`; not within the required 2 px |
| 1920 silhouette | neutral bbox `(814,370)-(1134,680)`, stored `(820,379)-(1127,680)`, differing mask pixels `7823`; not within the required 2 px |
| Coverage row | Not satisfied: no samples, therefore no material names/chips/percentages and no 100% sum |
| CPU/guard checks | Targeted material diagnosis and named editor/model/layer/UI/codegen checks passed. Full CTest inventory and registered red rows 13/16/53 were not run. |

The opcode comparison specifically included `MaterialRef(5)`, `Attach`, `MaterialRef(8)`, and `Attach`; the VIXEN evaluator preserved the distance exactly against the neutral reference. The two requested ids were `5` and `8`, but the only current GPU-side id list comes from palette metadata; no pixel or stored-sample readback proves those ids because the output set is empty.

The paired `undertow-author bake` byte comparison was **NOT RUN**: the paired tool was unavailable without introducing a new pin.

## STILL WRONG

- The 1280 and 1920 material frames are the correct hull, but the material and trim viewport hashes are unchanged and there is no hard boundary between `core:hull_metal_major` and `core:emissive_trim`.
- The silhouette comparison is outside the requested 2 px tolerance at both resolutions.
- The Outputs row cannot show percentages while the generated consumer emits no MaterialId samples; the log remains `total=0`.
- The document-default material is visible and explicitly marked “not saved with the document yet”, as required by the current codec limitation; it remains inert rather than pretending persistence.
- The UI input-forwarder target still has its existing two staged-CSS failures (`WantsKeyboard`/commit count); it is outside this brief's named guards and was not changed.

## STOPs

1. **Kernel/codegen owner action required:** extend the supported recipe SIMD consumer contract so the generated VIXEN program declares and evaluates channel 7 / `SEM_MATERIAL_ID` (or an equivalent documented material-carry output), then regenerate the pinned header and request that output in `BakeRegistryToPool`. This is the precise remaining blocker; no shader-side compensation or generated-file edit was made.
2. **Parity witness not run:** `undertow-author bake` was unavailable without a new pin, so editor/VRC1 byte parity remains unclassified.
3. The final q=.25/64 consumer contract is the only producer-verified frame. A q=.125/64 experiment passed the CPU distance guard but produced a blank GPU frame, proving that the consumer's fixed-grid/trace contract still needs an explicit supported high-resolution path; it was reverted before the final witness.

## CONSOLIDATION ISSUES

- proposed: VIXEN recipe SIMD consumer cannot request MaterialId output channel
- proposed: Queued VIXEN builds need durable per-lane build-tree admission and repair
- proposed: VIXEN UI target tests need a canonical staged-asset working directory
