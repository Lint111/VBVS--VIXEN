# Lane `editorbase` — R433 editor on the shared backend

## LANDABLE NOW

- Implementation, focused witnesses, contact sheet, and consolidation proposals: `f0d75336f8851ddf1260ab28c465f5e3e76c5628` (`feat(editor): route document edits through shared operations`).
- Base: `fc69e0c33be2321f52c0c1fb3c260d42a7de88a4`.
- Fetched `origin/wave/authoring-convergence` before the final witnesses; it still equals the base. This checkout has no `origin/main-wave` ref, so there was no wave movement to merge.

## R433 implementation

The editor remains a standalone `vixen_editor` application. It consumes the shared VIXEN render graph, the headless `VoxelDocument` operations, AppFlow history, and the existing recipe codecs. No editor or editing tools were added to KFR.

The editor no longer forces Lambert/GGX in its capture test. Its capture runner clears an inherited legacy shading override so the shared graph default is measured. The editor therefore uses cel shading by default: 3 bands, softness `0.08`, and hue shift `+18°/-18°` (R432). It inherits the shared directional key and the render graph's emissive point-light integration. The starfield remains the shared `SkySphereNode` feature, controlled by its existing `PARAM_ENABLED` toggle; this lane added no editor-private rendering nodes.

All editor layer state now lives in `EditorDocumentModel`. `ToggleLayer` dispatch and undo/redo call `SetLayerEnabled`; external Gaia view changes are validated through `SetEnabledMask`; flatten and save read the model's state. The AppFlow layer mask is a synchronized view projection, not another mutation authority.

## Document hazards

1. **32-versus-256 layers:** the editor rejects documents with more than 32 layers at admission and returns `DocumentDiagnosticCode::TooManyLayers` with a typed message. The codec remains capable of reading 256 layers, but the editor's layer controller and mask are 32-bit. Rejecting above 32 is the smaller safe fix and prevents shifts at indices 32 and above. Tests accept 31 and 32 layers and reject 33 and 256.
2. **Non-transactional reload:** `Load` reads and parses into candidate storage, checks the editor limit, and only then swaps the backing bytes and commits the view, path, and enabled mask. A failed parse leaves the accepted revision and its pointer-backed view intact. The valid → invalid → valid test confirms the accepted path, mask, and flattened document survive the invalid load and that the next valid document commits.

The layer-operation witness uses the same headless operations as the editor: toggle, undo, save, reopen, then compare canonical flattened bytes. The reopened document matches.

Hazards 3–5 remain for later lanes: whole-document rebake, fixed camera framing, and the nested-recipe callee registry. This lane did not change their mechanisms. The small editor viewport visible in the capture confirms the existing framing limitation; no new rendering or document-state change makes it worse.

## Visual witness

Target direction: T01 (shared light set), T13 (machine lights in the shared framework), T15 (starfield), and T21 (bi-modal cel). The references are the lighting iteration's R2-01 midday, R2-02 late afternoon, R2-03 overcast, and R2-04 night/service images. They are direction references, not pixel-match targets:

- R2-01: `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-01-bimodal-midday.png`
- R2-02: `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-02-bimodal-late-afternoon.png`
- R2-03: `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-03-bimodal-overcast.png`
- R2-04: `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-04-bimodal-night.png`

The committed contact sheet shows the sample document capture, an enlarged center crop, and all four references: [editor-sample-cel-r2-contact-sheet.png](editorbase-visual/editor-sample-cel-r2-contact-sheet.png). It was captured at 500×500 from the sample tri-layer VXD in the default shared lighting state. The visual comparison is limited by the existing editor framing: the render content is a small central outline against a nearly black frame. This is not a look pass and does not establish the lighting quality of a representative world scene.

Measured on `editor_capture_5.png`: luma p5/p50/p95 `0.0135 / 0.0180 / 0.0224`; emissive share `0.0000`. These values reflect the sparse frame and cannot be compared as a lighting verdict against the reference-world midday target. The parameters used were the shared defaults above; no owner-tuned values were changed.

| §7 step | Result | Observation |
|---|---|---|
| 1. Log the frame | N/A | Capture is 500×500, frame 5, sample tri-layer VXD, default shared lighting. The document and capture do not encode star, atmosphere, material-world, or time-of-day metadata. |
| 2. Shared light set | N/A | The editor inherits the shared graph and light integration, but the tiny visible render cannot show enough surfaces or shadows to trace. |
| 3. Value range | FAIL | p5/p50/p95 are `0.0135 / 0.0180 / 0.0224`, far below the reference midday range because almost the entire frame is black. The frame is too sparse to judge shading. |
| 4. Depth by haze | N/A | No atmosphere or far/near terrain layers are represented in this fixture. |
| 5. Domain split | N/A | The sample does not contain both nature and machine domains. The shared graph is in 3-band cel mode. |
| 6. Voxel step | N/A | No foliage, rock, excavation, or orbit scene is present. |
| 7. Intrusion read | N/A | No faction site or biome comparison is present. |
| 8. Machine lights | N/A | No emissive machine fixtures are visible; measured emissive share is zero. |
| 9. Weight | N/A | There is no machine or person/scale reference. |
| 10. Silhouette | FAIL | The render occupies only a small central region (about 32×35 pixels of changed capture area), too little to judge a hero silhouette. The fixed editor camera/framing hazard remains deferred. |
| 11. Materials | N/A | The fixture does not show a representative rock/metal/ice/vegetation set. |
| 12. Scale | N/A | No human-scale object is included. |
| 13. Clean frame | PASS | The capture reads the scene target without an abstract HUD overlay; no magenta neon is visible. |
| 14. Hull colour | N/A | No machine hull is present. |
| 15. Edges | N/A | No representative nature, machine-plate, or excavation edge treatment is present. |
| 16. Extraction read | N/A | No mined site or untouched comparison area is present. |

The remaining visual issue is framing, not a cel parameter: a future editor-framing lane should enlarge and center the document before using this fixture to judge light and silhouette quality.

Capture comparison: all four editor frames changed after enabling the shared cel default (`editor_capture_5.png`, `_45.png`, `_75.png`, `_105.png`); all three HUD frames remained byte-identical (`hud_capture_5.png`, `_45.png`, `_75.png`). The pixel differences were confined to the small central render area. The capture test suite passed.

## Witness and findings

- Fresh queued target build: `cmake --build build/wsl --target vixen_editor test_editor_document_model_preview --parallel 4` — **PASS**.
- Focused editor tests: `ctest --test-dir build/wsl -R EditorDocumentModel --output-on-failure --parallel 1` — **5/5 PASS**.
- Editor plus capture tests: `ctest --test-dir build/wsl -R '(vixen_editor_capture|EditorDocumentModel)' --output-on-failure --parallel 1` — **6/6 PASS**.
- Full RenderGraph witness: `ctest --test-dir build/wsl -L RenderGraph --output-on-failure --parallel 1` — **1,339/1,339 PASS**, 10 skipped.
- Native capture producer: queued capture run passed; HUD frames stayed byte-identical and all four editor frames changed as expected.
- Full SVO witness: `ctest --test-dir build/wsl -L SVO --output-on-failure --parallel 1` — **1 failure out of 751 executed tests**; CTest also listed eight skipped tests and one disabled test. `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` fails with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. This exact test failed on the recorded base too. The base run also hit `BulkMaterializationIntegrationTest.CpuRecipeOneVsNWorkerCanonicalHashParity` with `double free or corruption (out)`; its isolated rerun passed, and it did not recur in the final SVO run. Baseline log: `.tmp/baseline-svo-ctest.log`; final log: `/home/liory/.local/state/undertow/undertow-box-logs/1791410135-test-svo-final1.log`. The recipe SIMD failure is outside the editor document/render path.

### Full-build and generation recovery

Before semantic edits, at base `fc69e0c33be2321f52c0c1fb3c260d42a7de88a4`, queued `cmake --build build/wsl --parallel 4` passed (log: `.tmp/baseline-build.log`) and the RenderGraph baseline passed 1,339/1,339 (log: `.tmp/baseline-rendergraph-ctest.log`). The required baseline SVO run exited 8; its log is `.tmp/baseline-svo-ctest.log` and it showed the double-free and recipe gradient mismatch described above. A later queued full build, `cmake --build build/wsl --parallel 4`, failed at the `ALL` target's `appflow_check`, before editor linking. Its diagnostic is `RuntimeState 'Undertow.Content.Core.Systems.Diplomacy.Leveraged' index 'Source' must be unique with owner visibility.` The schema catalogue is `/home/liory/projects/undertow/core/src/Undertow.Authoring/Schema/schemas.json`; it was updated after the successful baseline build and is outside this lane. The full-build log is `/home/liory/.local/state/undertow/undertow-box-logs/1791409003-build-editorbase:impl-build2.log`.

- **A — invocation/provisioning:** the brief's queued `bash tools/check-content-codegen.sh` command exited 127 because the helper is absent from both `tools/` and `VIXEN/tools/`. The live global queue exists and was used. I checked the documented CMake codegen targets and used their available checks.
- **B — regenerate and recheck:** fresh queued restore and Release build of the pinned `CodegenTool` passed. A fresh queued CodegenTool AppFlow `--check` exited 134 on the same duplicate schema index; no generated output was changed. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791409901-light-editorbase:codegentool-appflow-check1.log`.
- **C — isolate:** the fresh queued `vixen_editor` and document-test target build passed, as did the editor, RenderGraph, and scoped document witnesses above. The remaining `appflow_check` failure is in the shared Undertow schema catalogue and is not an editor source or generated-document change. It remains a full-`ALL` finding; the supported target scope is green.

The SVO recipe SIMD failure is also a finding, not a STOP for this lane: it reproduces on base and does not depend on the editor target. **STOPs for the R433 editor scope: none.** The full `ALL` build and full SVO suite are not fully green for the findings above.

### Queue-rule incident

While authoring the AppFlow SPT proposal, unescaped backticks inside a shell double-quoted body triggered two raw, unqueued commands: `cmake --build build/wsl --parallel 4` and `cmake --build build/wsl --target vixen_editor test_editor_document_model_preview --parallel 4`. Their output and exit statuses were swallowed by command substitution, so those invocations are not treated as witnesses. Subsequent reported build, test, generation, and capture commands used the live global queue. This was an operator error; the queued results above are the recorded witnesses.

## SPT DISPOSITION

T-1103 remains fulfilled by the standalone headless `VoxelDocument` operations. Editor mutations, flatten, and save use that module; no KFR editing tools were introduced. No new orchestrator design decision is needed. The full-build schema catalogue mismatch is an external finding, not a request to change the editor contract.

## CONSOLIDATION ISSUES

- Baseline log redirection needs a worktree evidence directory
- Windowed capture script should canonicalize its output directory
- The VIXEN build invocation needs a canonical repository-root working directory
- Pin VIXEN AppFlow schema catalogue to the build wave
- Provide image dependencies for the visual lane measurement environment
- Restore the documented content codegen helper to the VIXEN worktree
- Isolate legacy shading overrides in visual capture runs
