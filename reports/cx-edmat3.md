# cx-edmat3 — material channel consumer slice

## LANDABLE NOW

The landable partial is VIXEN commit `69100a074` (`Remove legacy material lowering and use categorical channels`).

- VIXEN merge base after the required Step 0 merges: `79362a213b1fcf1cef89f1662e244f60e76c0266`.
- Required Step 0 merge order is in that merge commit: capture `eb4fe2546`, then material runtime `e25eff7a4`.
- Kernel provider/pin used for regeneration: `6075f4129cd252faa370b3758a6bd658e75a7b2a`.
- Undertow checkout used for the paired generator witness: `ca26498d01c39cea7d9a9f25974f9c4251e29ce0`.
- Landing order: kernel `6075f4129`, then the two Step 0 VIXEN merges, then VIXEN `69100a074`; the controller lands this lane into the wave.

This is intentionally a partial landable slice. The assigned action/UI implementation and end-to-end witness are stopped by the AppFlow ordinal contract documented below.

## DELETED

- `VIXEN/libraries/VoxelDocument/src/EditorDocumentModel.cpp` (the former `Output`/`Plane`/`PushParam`/`Select` document-material suffix, formerly around lines 1723–1759): document major/minor/percent fields no longer lower into a recipe.
- `VIXEN/application/editor/source/EditorApplication.cpp` (former `SetDocumentMaterials` registration, formerly around lines 1161–1185): id 47 remains reserved and inert; the generated runtime reports it as unimplemented.
- `VIXEN/shaders/StoredSdf.glsl` (former scalar-trilinear/round material lookup around lines 351–353): material lookup now calls the generated categorical path and preserves the id bits.
- `VIXEN/shaders/VoxelChannelFormat.glsl`: removed the consumer-defined channel constants and the obsolete `MAT_COUNT` shim; the shader includes `Generated/VoxelChannelVocabulary.glsl`.

## CHANGE

- Added generated categorical channel dispatch to CPU mip reduction and brick/parent mip baking, with a regression test proving ids 5 and 8 reduce to an authored id rather than an intermediate value.
- Added the categorical trilinear shader helper and routed stored material lookup through `ChannelIsCategorical`/`SampleChannelValue`.
- Added the located note for non-zero legacy v3 material fields on document load; legacy fields remain readable for save/reopen but do not lower.
- Vendored the regenerated V5 `VoxelDocument.g.h` from the pinned kernel provider. Source and consumer SHA-256: `8a8a6dd401004add12e8b1280418d0dd51ebc53a4bec40c7149e3113793282a2`.
- Added the consumer compatibility cases for the new graph value types/opcodes and corrected the merged `EditorLayersBind` golden initializer order.
- The four requested actions (`SetLayerMaterial`, `ClearLayerMaterial`, `SetLayerMaterialFromInput`, `SetDocumentDefaultMaterial`), their UI, and the per-id Outputs coverage projection were not added because assigning them would violate the reserved id contract.

## WITNESS

| Check | Result | Evidence |
|---|---|---|
| Step 0 configure/pin/regen | PASS for the scoped generated artifacts | queued logs `1794647000-light-cx-edmat3:step0-configure.log`, `1794647186-light-cx-edmat3:step0-regen.log`; targeted codegen checks passed in `1794647558-light-cx-edmat3:codegen-checks.log` and `1794647602-light-cx-edmat3:consumer-codegen-checks.log` |
| Kernel VoxelDocument regeneration | PASS, 1498/1498 | first queued run lacked ICU; invariant retry lacked `UNDERTOW_ROOT`; final queued command with `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` and `UNDERTOW_ROOT=/home/liory/projects/undertow` passed; log `1794648207-test-cx-edmat3:kernel-vd-regenerate-invariant-undertow.log` |
| Affected VIXEN targets | PASS | queued build `1794648317-build-cx-edmat3:target-build-retry2.log` |
| CPU/UI targeted tests | PASS, 47/47 CTest cases | queued test `1794648380-test-cx-edmat3:target-tests1.log` |
| Categorical mip test | PASS, 13/13 | queued build/test `1794648419-build-cx-edmat3:mip-categorical-build.log` and `1794648433-test-cx-edmat3:mip-categorical-test.log`; the new test asserts the output id is exactly 5 for a 5/8 majority, with no intermediate id |
| Final full VIXEN codegen guard | FINDING: known red outside this lane | `1794648630-build-cx-edmat3:codegen-checks-final.log`; only `windows_identifier_check` fails on `VIXEN/libraries/RenderGraph/src/Ui/UiKit.cpp`, a registered out-of-scope red |
| Windowed GPU editor witness | NOT RUN | no GPU job was submitted; actions/UI were blocked before a meaningful visual witness could be authored |
| Required `{5, 8}` stored-channel readback, cut-face fallback, 0-compile material edit, undo hash, coverage count, flatten/VRC1 parity, image review, v3 note capture, no-material byte identity, save/reopen stability | NOT RUN | no visual or byte-level end-to-end claim is made |

## STILL WRONG

- AppFlow actions 70–79 are absent; consequently the node-outline Material row, picker/from-input flow, Outputs default picker, and per-id measured coverage are absent.
- The required hull/trim/subtract editor witness was not run, so hard-boundary, cut-face, no-intermediate-id, frame latency, undo, and image descriptions remain unproven.
- The final full codegen guard still has the registered `UiKit.cpp` Windows-identifier red.
- The generated V5 header is vendored as documented, but the consumer regeneration target still does not automatically refresh it.

## STOPs

1. **Required action ids cannot be declared safely.** The assigned ids are 70–79, while another lane owns 53–69. The pinned generator uses `BuildEnum(..., deriveOrdinals: true)` at `Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/AppFlowModel.cs:409-416`; it emits the declaration index (`i`) and ignores explicit enum constants. The current generated `FlowActionId` ends at 52. Adding the four actions would silently produce 53–56, not 70–73, and inventing 53–69 placeholders would modify another lane’s reserved range. This is a design/generator decision for the orchestrator.

2. **The final full codegen guard is not a lane red.** Its only failure is the registered `UiKit.cpp` Windows macro-like declaration. Scoped target generation/build/tests are green, so this known unrelated red does not invalidate the committed partial.

Recovery record: the configured global queue path was usable. The initial target compile exposed stale/missing generated consumer compatibility (V5 `VoxelDocument`, graph socket fields, and new graph value enum cases); regeneration plus the documented V5 artifact vendoring recovered the target build. The kernel generator recovery ladder was completed: missing ICU was handled with the supported invariant mode, then the missing paired root was supplied, producing a green 1498-test run.

## SPT DISPOSITION

Four proposals were appended to `.spt-proposals/cx-edmat3.jsonl` and committed with the lane. They cover sparse AppFlow ordinals, generated graph initializers, VoxelDocument vending, and generator runtime/pairing provisioning.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: AppFlow action enums need sparse reserved ordinal support
- proposed: Generated SDF graph initializers must initialize appended socket fields
- proposed: VIXEN codegen should vendor the refreshed VoxelDocument artifact
- proposed: Kernel generator test target needs explicit runtime and pairing provisioning
