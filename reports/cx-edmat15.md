# cx-edmat15 — d10 wave merge, stored-bake repair, and editor handoff

## LANDABLE NOW

VIXEN semantic tip: `d73461e79` (`show material coverage note with four rows`). The d10 wave is contained by merge commit `0a9cda077`, including wave SHA `6444d7c2bcb3bdee03275e0fc808c58ddedb0565`. Kernel pin remains `a2614dee46a87d57926eb4cb8a94ac449b718e99`; this head needs kernel d11 landed first.

This dispatch's lane-only commits not present in the wave are:

- `0a9cda077` — merge d10 authoring-convergence into the d11 editor line.
- `e228a4def` — material-chip contrast rule, generated row field, and golden-call-site repair.
- `eeb23d65d` — restore the shared sub-voxel bake filtering contract.
- `d73461e79` — keep four material coverage rows plus the note visible in the pinned region.

The semantic result is committed; no kernel change and no AppFlow action was added.

## MERGE

- `VIXEN/codegen/CMakeLists.txt`: wave intent was the d10 kernel pin; lane intent was the already-pinned d11 kernel. Kept d11 `a2614dee4`.
- `VIXEN/libraries/SVO/include/Recipe/SdfRecipeEval.h`: wave intent added the d10 material-carry compatibility/warning handling; lane intent retained d11 dispatch arguments and sidecar state. Kept both: the d11 evaluator signature and local material sidecar, plus the MSVC warning guard from the wave.
- `VIXEN/libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h`: wave generated d10 opcode/table values; lane generated d11 opcodes 119–122. Kept d11 generated output.
- `VIXEN/libraries/SVO/include/Recipe/generated/SdfRecipeEvalDispatch.g.inc`: wave generated d10 dispatch; lane generated d11 dispatch with element-row and plan-slot inputs. Kept d11 generated output.

No other hand-written file dealing with the generated hit payload or provider binding conflicted in this merge. The only hand merge delta was `SdfRecipeEval.h`; the other payload/provider conflict files were generated headers/includes.

## SdfRecipeEval.h FINAL SHAPE

`evalRecipe` retains the d11-compatible signature:

```cpp
evalRecipe(const SdfInstruction*, uint32_t, glm::vec3,
           std::span<const float> params = {},
           glm::vec3* outDeclaredPos = nullptr,
           const RecipeRegistry* registry = nullptr,
           std::span<const GlyphOutlineInstance> glyphInstances = {},
           std::span<const PathInstance> pathInstances = {},
           const void* elementRow = nullptr, uint32_t elementStride = 0u,
           const uint32_t* planSlots = nullptr, uint32_t planSlotCount = 0u)
```

It includes the generated glyph/path/core-kernel headers, `RecipeRegistry.h`, glm and the standard span/string/assertion headers; it materializes `elementRowBytes`, preserves the local `materialStack/materialPresent` sidecar, and includes the generated d11 dispatch under GCC/Clang diagnostic push/pop and MSVC warning 4101 push/pop guards.

## ROW 87

| Tree | Result |
|---|---|
| Wave `6444d7c2b` alone | PASS: `SdfBake.CenterSamplesStayAccurateAndSubVoxelSphereIsFiltered` |
| Merged pre-fix tree | FAIL: `alignedStored=-0.2`, absolute difference `0.611189902` |
| Final `d73461e79` tree | PASS: row-87 test and `EditorDocumentModelMaterialDiagnosis.HullMaterialDistanceAndDisplayedBakeSlotGuard` |

The failure bisected to `6f98553ce`, which added a shared `FilterSubVoxelDistanceAtGridCenter` center-sign clamp. The fix in `eeb23d65d` removes that clamp for the shared bake path while keeping the recipe-units-per-voxel/frame mapping needed by the editor. The final editor diagnosis passed with 4,913 stored and 4,913 neutral stored samples, zero material-vs-neutral distance delta, zero material-bake-vs-neutral-bake delta, two generated outputs (SDF plus MaterialId), and the 54%/46% coverage assertions. The stored/reference sign-mismatch assertion is zero.

Evidence: `wave-scratch-sdfbake.log`, `row87-fix-tests.log`, and the full-suite CPU XML under `/home/liory/codeman-cases/undertow/reports/cx-edmat15-evidence/`.

## ROW 86

The unchanged assertion is:

```cpp
EXPECT_EQ(SEM_COUNT,17u);
```

The d11 generated value is `SEM_COUNT = 18u` in `VoxelChannelVocabulary.g.h` (and GLSL). The controller's one-line update is `EXPECT_EQ(SEM_COUNT,18u);`; this lane did not edit the expectation.

## SUITES

- Fresh full Debug build: the initial queue payload terminated twice without a verdict; the reduced-credit queued recovery completed `BUILD_RC=0` for all 305 targets. The termination logs and recovery log are in `final-full-build*.log`.
- Codegen: aggregate `vixen_codegen_checks`, explicit `windows_identifier_check`, and `no_new_mutex_check` passed; the registered 40-check contract is green.
- CPU full set, through `tools/run-tests-split.sh`: 1,779 passed, 1 failed, 11 skipped. The only failure was `ChannelFormat.EnumValuesAreStable` (row 86). Compared by name with cx-edmat14, the two old reds became one: `SdfBake.CenterSamplesStayAccurateAndSubVoxelSphereIsFiltered` changed failed→passed; ChannelFormat remains failed.
- GPU full set, one GPU job at a time: 1,508 passed, 0 failed, 234 skipped. Compared by name with cx-edmat14, every prior GPU name retained its status and the new `UiKitRule.MaterialChipTextContrastsAgainstMaterialTableAlbedos` passed. cx-edmat14's shard summary was 1,509/0; the one-count aggregate difference has no named failure/status delta.

## WITNESS

Evidence root: `/home/liory/codeman-cases/undertow/reports/cx-edmat15-evidence/`.

- Capture service, 1280×720 and 1920×1080: neutral, dark-box/cream-bulge two-material state at 54%/46%, bulge change, undo, cut-own, cut-cleared, parameter edit, and `coverageReady=true` states all passed. Material rows use contrast-selected text; the cream `core:emissive_trim` chip is readable and the note remains visible. Save/reopen is byte-stable at both sizes: 1280 hash `dfdc7e881e2495444696f80813e1c572556c70e5362ee3cd7b231d6003df11df`; 1920 is the same.
- Default cases a–c plus undo/redo/save/reopen passed. Default ownership was `475` samples, clear was `0`, and the default witness save/reopen hash was `fa6b93d683b7ebc1d6c404c85999b781fc22c2c88467888045128ddff3924bfb`.
- Inversion a–f passed: a `386) slot-5, b `386) slot-8, c `401) with 254/147, d `401) with 254/147 reversed, e and f `401) with 254/147.
- Merged HUD frames 5/45/75 and text frames 5/45 were byte-identical to clean captures from the exact wave tip `6444d7c2b). Hashes: HUD `8ef581c7`, `1707ebe2`, `6ac8a82e`; text `f40a97fa`, `8b7db01e`.
- The optional point MaterialId probe is unavailable at the editor procedural frame pin; material captures used the witness's documented `--skip-material-probe` path and validated the UI coverage provider instead. This is recorded as a consolidation issue, not a product red.

## STILL WRONG

The inherited HUD/text capture still contains the known glyph-box/button overlap and clipped legacy panel content; it is visible in:

- `capture/merged/hud/hud_capture_45.png`
- `capture/merged/textshader/hud_capture_45.png`

Those images are byte-identical to the exact wave captures, so this is the accepted wave/base-red row-85 issue, not a regression from this lane.

## STOPs

None. Required generation, build, CPU/GPU suites, row-87 two-tree comparison, and capture witnesses completed. Queue diagnostic classification remains a tooling finding where CTest/PAYLOAD_FAIL hid direct payload details; the underlying required checks are classified by their captured logs.

## SPT DISPOSITION

`spt propose --check .spt-proposals/cx-edmat15.jsonl` passed for 9 entries. The lane-local inbox is committed with this report.

## CONSOLIDATION ISSUES

- proposed: Queued full VIXEN builds can be terminated without a payload verdict
- proposed: Aggregate VIXEN codegen regen needs a bounded focused fallback
- proposed: VIXEN CMake presets need a source-root invocation guard
- proposed: Queued CTest no-tests failures need usable shard diagnostics
- proposed: EditorLayers binding changes should update golden test call sites
- proposed: Queue PAYLOAD_FAIL must retain direct CTest failure output
- proposed: Windowed material witness needs documented DISPLAY bootstrap
- proposed: Material witness needs a d11-compatible coverage probe fallback
- proposed: Detached wave captures need an explicit Undertow text-shader provider
