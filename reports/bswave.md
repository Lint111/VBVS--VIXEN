# bswave — boundscore VIXEN wave integration

## LANDABLE NOW

**State:** the pin bump and merge are committed, and the merged Release tree passed its build, generated checks, and most requested witnesses. It is **not ready to land** while the required wave byte-identity capture gate remains red.

Witnessed product tree: `816fd5095b14e9091e77c9f10527f3c4e9071fa9`, based on wave `bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360`, with `lane-boundscore` `97f2934bc2c8e1aa951c4e4f24d5b1038daa7d2f` merged. Kernel pin is the full SHA `c3fb3847979e3f9eef04783510c42b70d7058ae7`.

Commits:

- Pin bump: `0a85b878631f57b792e83afd2422ebcb81a12081` — `build(vixen): track kernel b21 boundscore pin`.
- Merge: `816fd5095b14e9091e77c9f10527f3c4e9071fa9` — `merge lane-boundscore: generated interval transfers and occupancy`.

### Generation and Release build

The queued `recipe_simd_regen` target ran against the pinned kernel. Its four outputs (`RecipeSimd.g.hpp`, `SdfRecipeEvalDispatch.g.inc`, `SdfRecipeCodegenGlslDispatch.g.inc`, and `RecipeTapeEval.g.glsl`) reported no changes. The fresh Release build completed 1,146 actions successfully, and all 22 `*_check` targets passed:

`octreeconfig_check`, `recipeparams_check`, `recipe_simd_check`, `sdf_core_kernels_check`, `recipe_opcode_mirror_check`, `lightingconfig_check`, `shadowconfig_check`, `accumulationconfig_check`, `prevcameraconfig_check`, `reservoirconfig_check`, `probegridconfig_check`, `lighttreebuffer_check`, `miningbeambuffer_check`, `reservoirrecord_check`, `view_hud_check`, `view_editor_layers_check`, `view_hud_markup_check`, `view_hud_blob_check`, `view_hud_writer_check`, `appflow_check`, `view_noun_enum_check`, and `callables_check`. `no_new_mutex_check` also passed.

The CMake cache selected `Release`; `VIXEN_YEROKET_KERNEL_SHA_OVERRIDE` was empty. Configure used the explicit workspace kernel root and worktree-local FetchContent and kernel-codegen caches, and auto-discovered the Undertow schema catalogue. It provisioned the Vulkan SDK and X11 payloads. The configure, regeneration, and full-build queue logs are respectively `1791545949-build-bswave:configure-release-pin-c3fb.log`, `1791546087-build-bswave:recipe-simd-regen-c3fb.log`, and `1791546173-build-bswave:fresh-release-full-build-and-checks.log` under `/home/liory/.local/state/undertow/undertow-box-logs/`.

### Test witnesses

- **RenderGraph:** full CTest run serially; 1,343 passed, zero failed, 10 skipped. Evidence: `.tmp/boundscore/run4/rendergraph-serial.log` and queued log `1791547217-test-bswave:rendergraph-serial-final-c3fb.log`.
- **SVO:** 761 passed, 1 failed, 8 skipped, 1 disabled. The sole failure is the known T-1449 exception, `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`: `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. No other SVO failure occurred. Evidence: `.tmp/boundscore/run4/svo-serial.log` and `1791547534-test-bswave:svo-serial-final-c3fb.log`.
- **Boundscore occupancy exactness and cost:** `RecipeOccupancy.GeneratedReductionMatchesOracleAndMeasuresCellWork` passed all four fixtures; each generated result was byte-identical to its dense oracle.

  | Fixture | Exact / sampled cells | Points dense → generated | Median dense → generated |
  |---|---:|---:|---:|
  | Sphere | 1,160 / 2,936 | 262,144 → 201,584 | 1.93846 → 1.52758 ms |
  | Box | 760 / 3,336 | 262,144 → 221,375 | 2.69567 → 2.01503 ms |
  | Union | 832 / 3,264 | 262,144 → 220,764 | 2.75160 → 2.17751 ms |
  | Subtract | 1,452 / 2,644 | 262,144 → 187,872 | 2.72880 → 1.80416 ms |

  Evidence: `1791547680-test-boundscore-occupancy-exactness.log`.
- **rvcompact identity and cost:** `DeclaredPositionRenderTest.IntervalPrunedTileTapesMatchFullAndUnrolledGpuPixels` passed. Dead terms compacted 325 → 1 instructions (0 differing output bytes; 0.11556 → 0.00628 ms full/compact GPU). Visible edit compacted 433 → 177 (0 differing output bytes; 0.76776 → 0.33208 ms). Evidence: `1791547686-test-boundscore-rvcompact-output-identity.log`.
- **Capture assertions:** the isolated Release CTest selection passed 13/13 (11 canonical assertions plus two producer setup tests). The first attempt overlapped another lane's DZN GPU run and lost the device; the isolated rerun passed. The earlier overlap is covered by the existing DZN serialization proposal.
- **Capture byte identity:** comparison against the wave run 3 Release `capture-a` set failed. All three HUD PNGs matched byte-for-byte; all four editor PNGs did not.

  | Editor captures | PNG bytes differing | Pixels differing | Maximum channel delta | Bounding box |
  |---|---:|---:|---:|---|
  | 105 / 45 | 6,558 | 81,496 / 250,000 | 178 | x=103..397, y=95..378 |
  | 5 / 75 | 7,447 | 74,187 / 250,000 | 178 | x=103..397, y=95..378 |

  The current editor PNG SHA256 values are `ed362a4235d54e8120fb0e109a7f2ae53d01053d76a7fe9a4e84a8f74068e054` (105/45) and `14a4264775cb4edddf755147dac2c9cad1b5aba5176242b87f838476c4e977a5` (5/75). The corresponding wave hashes are `8d28d52feb20f12a350e1b55c3db55f6aa6a8a28ee60b1836d9acc8183ae04a1` and `bfcdcfddb1c4e1138ca808ea061891ce3554ced883e3c4d85c7148b1235187af`. The HUD files have zero differing bytes. The Release profile, DZN runtime/device, and `sample_tri_layer.vxd` input matched the wave witness. Evidence: `.tmp/boundscore/run4/captures-isolated2.log` and `1791549008-test-boundscore-native-captures-wave-byte-check-isolated.log`.
- **No-op rebuild:** queued rebuild completed successfully with `ninja: no work to do.` Evidence: `1791548360-build-boundscore-noop-rebuild.log`.

KFR follow-up call site remains read-only: `KernelFederationRenderer/app/src/session_renderer.cpp`, `SessionRenderer::BuildRenderGraph()` near line 28. This lane made no KFR edits.

## STOPs and remaining gaps

- **STOP / REFER-TO-ORCHESTRATOR — editor capture compatibility.** The required capture comparison remains red after an isolated, matched-Release rerun, so the merge is not landable yet. The pinned kernel's generated `SdfCoreKernels.g.glsl` now includes `precise` return temporaries; that is a plausible source of the changed editor pixels, but this run does not prove causation. The owner must decide whether to preserve the wave's byte output in the pinned-kernel shader path, or accept this as an intended arithmetic change and approve a new visual baseline. Do not recapture the baseline as a workaround. The wave baseline exists and is reproducible; this is not a baseline-unobtainable classification.
- **Known SVO exception:** T-1449/opcode 94 remains the only failing SVO case and stays open. T-1450 also stays open; this lane records the merged-tree cost table but does not close it.
- **No unrelated test failures** were excluded from the required scope.

## SPT DISPOSITION

No SPT task was closed. T-1449 and T-1450 remain open.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Queue reservation scan can observe a vanished memory-reservation file

The explicit kernel-root and worktree-cache configure inputs were already covered by earlier VIXEN consolidation proposals, so this lane did not duplicate those entries. The schema catalogue was discovered automatically.
