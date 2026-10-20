# vixleft: three base reds (TextureCacher vtable link, mutex allowlist, frame_targets schema check)

Branch `lane-vixleft`, off `origin/wave/authoring-convergence` `13358fe3`, merged up to `58b70060` (vixdeps). Dozen ICD results are correctness only.

## What changed

- `VIXEN/libraries/CashSystem/tests/test_registration_api.cpp:6` includes `Textures/TextureCacher.h`; `VIXEN/libraries/CashSystem/tests/CMakeLists.txt` links `Textures` into `test_registration_api`. The test included a compat shim `CashSystem/include/TextureCacher.h` (now deleted, its only user was this test), but the TextureCacher source lives in the `Textures` library, so the vtable was undefined at link time.
- `VIXEN/scripts/no-new-mutex.allowlist`: removed the stale `Textures/src/TextureLoader.cpp` row; added `TaskExecutor.h` (3, KD2/KD3/KD4), `TaskExecutor.cpp` (1, KD5), `PhaseTiming.h` (1, RG8). Classification is in a dated addendum to `Vixen-Docs/01-Architecture/2026-09-08-runtime-lock-inventory-and-classification.md` (KD2 ORDER-able, KD3 PARTITION-able, KD4/KD5/RG8 DELTA-able; KD1 no longer exists).
- `VIXEN/codegen/CMakeLists.txt` (new block before `set(_gpu_schema_source_files)`): excludes `Undertow.Content.Core.Declarations/SubstrateKinds.cs` from the GPU schema overlay, same mechanism as the existing `FactionAi` exclusion. `codegen/generated/gpu/frame-targets.schemas.json` and `FrameTargets.g.cs` regenerated with `frame_targets_schema_regen` / `frame_targets_ref_regen` (33 Substrate kinds, 66 Ref/struct names removed; none used elsewhere in VIXEN, one comment mention in `ArchetypeBuilder.h`).

## frame_targets root cause (MEASURED)

Not an outdated undertow pair. With `main-wave` sources and the pinned kernel snapshot `96570e48` (`codegen/CMakeLists.txt:132`) the check still fails: that kernel's source reader does not parse the `.Rate` schema facet used at `SubstrateKinds.cs:173`. Bumping the pin to `38da3fa1` (what Undertow `main-wave` pins) clears `Rate` but drifts about 20 goldens (octreeconfig, glyph_outline_instance, sdf_core_kernels, recipe_simd, recipe_opcode_mirror, config headers), which is a separate, larger change; I reverted it. FrameTargets references no Substrate kind, so the exclusion is the smallest correct fix until the pin moves.

## Tests (MEASURED, Release, FSR3 OFF, Dozen ICD, merged tree)

- Plain `cmake --build build/wsl` (no `-k 0`): exit 0, 253 steps, after the merge. One warning, pre-existing in untouched `DeviceNode.cpp:683` (deprecated `SetBudgetManager`). Before the merge two plain builds also exited 0.
- Post-merge `ctest -L "^(CashSystem|Textures|VulkanResources)$"`: 137/137 pass (CashSystem 117, Textures 3, VulkanResources 17 including 1 skip reported as pass by ctest).
- `vixen_editor_capture_producer`, `vixen_hud_capture_producer`, `vixen_instancing_demo_capture_producer`: pass (each its own GPU job).
- `vixen_text_shader_capture_producer`: FAILS, three different ways across three runs: SEGFAULT (inside the full RenderGraph run), Timeout at 180 s (shared GPU), and with `--timeout 900` a `VK_ERROR_DEVICE_LOST` after ~90 s ("ComputeDispatchNode: failed to end tile commands", translated Dozen device, unrecoverable). Dozen TDR on the 1920x1080 ray-march. INFERRED environmental: nothing in this branch changes C++ or shaders; not run on the base.
- `-L RenderGraph`: 1404 pass, 25 fail of 1429 (not caused by this branch's diff, INFERRED; base not built):
  - 10 CPU RmlUi (UiCompositionHostTest x6, ViewEditorLayersGolden x1, ViewEditorLayersReconcile x1, BlobView x2): "No font engine interface set!". These are the same 10 `reports/instdemo.md` measured. MEASURED cause: `VIXEN/dependencies/CMakeLists.txt:374` forces `RMLUI_FONT_ENGINE none` on the base, and these tests never call `Rml::SetFontEngineInterface`, so `Rml::Initialise()` returns false. Not environmental: deterministic from committed code.
  - 10 `ProceduralRecipeRenderTest.*`: `HLSL compile failed ... shader:6 ')' Expected`. MEASURED: the committed `VIXEN/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl:10,12` uses `GlyphOutlineInstance.SegmentCapacity` (a C#-style static member access, invalid HLSL). The same golden drifts when the kernel pin moves to `38da3fa1`, so the stale pin is the likely root cause (INFERRED, not tried).
  - 3 `EditorDocumentRenderTest.*` (hit pixels 42082 vs 50000, etc.) and 1 `AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender` (boreDiffPixels 0 vs 3000): image-threshold failures on Dozen. Plausibly the same SDF shader failure (the body does not render); not isolated.
  - 1 `vixen_text_shader_capture_producer` (above).
- Hard-rule greps on added lines (EntityId, `KindRef<All>`, explicit `Id`, attributes): none in hand-written lines. The only match is a regenerated golden JSON line (`"[part_of-id, ...]"`).

## SPT DISPOSITION

- TextureCacher vtable link in `test_registration_api`: CLOSE.
- `no_new_mutex_check` (stale row plus unclassified TaskExecutor/PhaseTiming): CLOSE.
- `frame_targets_schema_check`: CLOSE with the SubstrateKinds exclusion; PARTIAL against the real cause (stale kernel pin `96570e48`). Proposed follow-up: bump the pin to `38da3fa1` and re-bless the ~20 goldens (tier: normal, effort: large), which should also be tried against the 10 `ProceduralRecipeRenderTest` HLSL failures.
- RmlUi "No font engine interface set": not closed; deterministic base gap (forced `RMLUI_FONT_ENGINE none` and tests that set no interface). Proposed: have the tests install a stub font engine like `test_hud_view.cpp:32` does (effort: small).
- `vixen_text_shader_capture_producer`: not closed; needs a base run or a native GPU run.

## CONSOLIDATION ISSUES

- The kernel pin `96570e48` is behind Undertow `main-wave` (`38da3fa1`); the goldens `SdfCoreKernels.g.hlsl` and friends are what that gap hides. Moving it is the real fix for the `Rate` parse error and probably the SDF compile failures.
- `frame-targets.schemas.json` now carries one `part_of` ref line whose target kind is no longer present in the catalogue (dangling after the exclusion). Not verified against `pair-hlsl`.
- The `RenderGraph` ctest label is not green (25 fails above; INFERRED to be the same on the base, not built), so "ctest passes for RenderGraph" cannot be shown without fixing the SDF HLSL and the RmlUi tests.
- The stb pin `2c980bb5` is not fetchable fresh; configure needs `-DFETCHCONTENT_SOURCE_DIR_STB`.
- `cmake/tests/test_kernel_snapshot_reuse.cmake:5` still uses `96570e48` as a fixture value; it will need updating together with a pin bump.
- Queue note: four plain GPU jobs from one agent queue head-of-line behind each other for minutes; the text-shader producer is slow enough on Dozen (700 submissions per frame) to monopolise the GPU slot for the full 180 s.
