# Lane gaiapin: move the stack to the Gaia fork pin

Pin: `https://github.com/Lint111/gaia-ecs.git`, branch `pin/soa31-2026-10-20`, tip `84ba7f10dfabd376a862c6fc54d3d43ed365ae54` (31 SoA members, 65535-byte members). Old pin: upstream `a2e62491b56a8589c31da97128f0658088050a70`.

Raw evidence: `/home/liory/codeman-cases/undertow/reports/gaiapin-evidence/` (`vixen-configure.log`, `vixen-build.log`, `ctest-<label>.log`, `kernel-suites.log`, `native-boot.log`).

## One shared pin record

- `VIXEN/dependencies/native/gaia.pin` is the source of truth (flat `sha:`/`branch:`/`remote:` lines, the `toolchain/providers/*.pin` convention). `dependencies/native/CMakeLists.txt` parses `sha` and `remote` from it (FATAL_ERROR if either is missing, re-configures when the file changes) and keeps the existing revision-keyed cache and fetched-HEAD verification.
- `toolchain/providers/gaia.pin` is a byte-identical copy in undertow and in the kernel, so `tools/check-provider-pin.sh gaia` works (it reports `84ba7f10... is the tip of pin/soa31-2026-10-20`).
- Readers of the old hand-copied SHA now read the pin:
  - undertow `tools/kernel-test-witness.py` (`read_gaia_pin_sha`, `gaia_input`): uses undertow's copy as the cache-key revision and raises `KeyUnavailable` if the kernel's copy differs, so drift between the copies cannot produce a stale witness.
  - undertow `tools/check-hydrate-markers.sh`: the default `GAIA_INC` is derived from the pin sha.
  - kernel `CodegenTool~/Tests/NativeCompileFixture.cs`: `PinnedGaiaRevision` walks up to `toolchain/providers/gaia.pin`; the dead `PinnedVixenRevision` constant is removed.
  - nine kernel test files: the assert text now names the pin file instead of a SHA.
  - undertow `docs/design/TDD.md`: the Gaia row points at the pin file (no SHA).
- Remaining literal SHA outside the pin files: only historical `reports/*.md`, `.spt-proposals/*.jsonl` and `docs/in-progress.md` (never edited).

## Persisted or hard-coded raw Gaia ids (Do item 2): no STOP

The fork changes only the packed `Component::value()` bits. `Entity` encoding, `size()` and `alig()` are unchanged. Nothing in the kernel, undertow, VIXEN or KFR persists or hard-codes a raw component id. All hits are `Entity::value()` handles, which are unchanged:

- kernel `SourceGenerator~/Transpiler/GaiaSaveMapFormat.cs:156-157`, `NativeSubstrateEmitter.cs:735,958`, `Runtime/Schema/Ids.cs:32`, `SaveCodecEmitter.cs:12`;
- undertow `core/tests/Undertow.Core.Tests/Saves/SaveIoTests.cs:36,66`, `EntityIdBoundaryTests.cs:43`;
- generated headers use `.value()` only as sort comparators over live ids;
- `comp.size()/alig()/soa()` users (`GaiaQueryEmitter.cs:1279,1383-1384`, `RuntimeAppliedPairsCppEmitter.cs:36-37,114`, `GaiaFrontierRelationEmission.cs:124-125`) use unchanged API.

## Tests

Bases merged before the witness: undertow `origin/main-wave` (23 commits, clean), kernel `origin/main` (16 commits, clean); VIXEN was already current with `origin/wave/authoring-convergence`.

VIXEN (configure logs "Gaia source verified at 84ba7f10"; build green):

| label | result |
|---|---|
| GaiaVoxelWorld | 98/98 |
| GaiaArchetypes | 128/128 |
| VoxelComponents | 9/9 |
| VoxelData | 24/24 |
| Textures | 3/3 |
| VulkanResources | 17/17 |
| CashSystem | 117/117 |
| VIXENCapture (`vixen_wsl_capture_witness`) | 1/1 |
| HUD / INSTANCING capture producers | 1/1 each |
| SVO | 788/799 |
| RenderGraph | 1404/1429 (25 failures) |
| TEXT_SHADER capture producer | timeout 185 s standalone; SEGFAULT inside the RenderGraph run |

Failures by name against the known set:

- SVO, 11 (`RecipeCodegen`, `SdfRecipeCodegen`, `RecipeGlslOpcodeCoverage`, `CodegenEmit`): the committed `SdfCoreKernels.g.hlsl` uses `GlyphOutlineInstance.SegmentCapacity`, already MEASURED in `reports/vixleft.md:23`. Not Gaia.
- RenderGraph, 25 = the known set exactly (`reports/vixdeps.md:116-124`, `reports/exposure.md:107,275`, `reports/instdemo.md:30`): 10 `ProceduralRecipeRenderTest`, 3 `EditorDocumentRenderTest`, 1 `AppFlowEditorToggleRenderTest`, 6 `UiCompositionHostTest` (segfault), `ViewEditorLayersGolden.GeneratedBindFunctionCompilesAndBinds`, `ViewEditorLayersReconcile.ExternalGaiaWriteReconcilesIntoBoundView`, 2 `BlobView` (segfault), and `vixen_text_shader_capture_producer` (known Dozen device loss, `reports/vixleft.md:20`). No new name.

Kernel (paired with the undertow worktree, run through `tools/check-kernel-test-suite.sh`, results STORED):

- SG: 1208 passed, 0 failed, 29 skipped (1237).
- CG: 2612 passed, 0 failed, 18 skipped (2630), including the native compile fixtures (they would fail, not skip, without the pinned header). The 18 skips are `glslangValidator`/`spirv-cross` and E7-harness tests that skip on this machine; `TypedKindRefJoin_...` runs its native C++ part before its GLSL half skips.

Undertow:

- `tools/check-provider-pin.sh gaia`: OK.
- `tools/check-native-boot.sh`: PASS for default and composition, including `native-fixtures-ctest` 3/3, `default-evidence-check` and `compose-evidence-check` (`scheduled=67 plan_entries=9 ticked=27 frames=3 skipped=0`).
- `kernel-test-witness` is covered by the kernel suite runs above.

Canonical-shape checklist (R412 mechanical greps) on added lines in all three repos: empty.

## Boot VIXEN source (Do item 3)

The boot does not take the VIXEN worktree. `check-native-boot.sh` requires `UNDERSET_VIXEN_ROOT` to equal undertow's `vixen.pin`, but the composition build uses the VIXEN that KFR pins (`$kfr_root/tools/vixen-root.sh`, with KFR coming from the launcher pin), and there is no override. MEASURED in `native-boot.log`'s composition cache: `gaia_SOURCE_DIR` HEAD is `a2e62491...` from `richardbiely/gaia-ecs`. So both boots passed on the OLD Gaia, with the merged undertow and kernel tooling. They show that the tooling changes did not break the boot. They do not exercise the fork.

## Landing order and SHAs

VIXEN pin commit `2a2bc339` (branch tip is the commit that adds this report, see the final message), undertow `24c40f31c53640a608f94c08f23ddbaa0f077953`, kernel `f8d28a9967b2dc83a9b616b4d6b37f20791c72ba`.

1. VIXEN with the wave push (it carries the fork pin).
2. Undertow and kernel in the same batch (their pin copies must stay byte-identical, which the witness enforces).
3. After that batch: bump KFR's VIXEN pin (and the launcher's KFR pin, then undertow's `launcher.pin`/`vixen.pin`) so the boot composes the fork. Until then the boot runs on upstream Gaia.

## SPT DISPOSITION

- No SPT task ids were assigned to this lane. R668.5 and R668.7 are named in the brief and the commit messages; the pin move itself is done (CLOSE on landing).
- OPEN: the kernel proposal `ed3825bb...` in the kernel's `.spt-proposals/gaiapin.jsonl` (derive sibling FetchContent paths from the shared pin).

## CONSOLIDATION ISSUES

- Three byte-identical copies of `gaia.pin` (VIXEN, undertow, kernel). Only the kernel-test-witness cross-check and `check-provider-pin.sh gaia` guard the copies; nothing in VIXEN CI compares them.
- The native boot's VIXEN and Gaia come from KFR's pin, not undertow's `vixen.pin`, so a Gaia pin move needs a KFR pin bump before the boots test it.
- The SVO `GlyphOutlineInstance.SegmentCapacity` HLSL red (11 tests) and the RenderGraph known set (25) are still unresolved on the base.
- `.vscode/c_cpp_properties.json` still defaults the Gaia include path to `build/_deps/gaia-src/include` (build-tree relative); not a pin reader, left as is.
