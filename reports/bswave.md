# bswave — boundscore VIXEN wave integration

## LANDABLE NOW

**State:** the pin bump, boundscore merge, and latest authoring-wave merge are committed. Fresh Release generation and build checks pass on the latest merged tree. The merge is **not ready to land** because the required editor capture byte-identity gate remains red.

Witnessed product tree: `1879a24be0bc12a0141180a7487aaf5a973fbf57`, containing latest `origin/wave/authoring-convergence` at `dc2ea8828b887b71c183282d09f460463e63bc00`. That wave update brings in the shared `vixen_gpu_device` CTest resource lock. Boundscore source `lane-boundscore` was `97f2934bc2c8e1aa951c4c4f24d5b1038daa7d2f`; the wave base before its latest scheduling update was `bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360`.

Commits:

- Pin bump: `0a85b878631f57b792e83afd2422ebcb81a12081` — `build(vixen): track kernel b21 boundscore pin`.
- Merge `lane-boundscore`: `816fd5095b14e9091e77c9f10527f3c4e9071fa9` — `merge lane-boundscore: generated interval transfers and occupancy`.
- Merge latest authoring wave: `1879a24be0bc12a0141180a7487aaf5a973fbf57` — `merge latest authoring wave: serialize DZN GPU tests`.
- Kernel pin: full SHA `c3fb3847979e3f9eef04783510c42b70d7058ae7`.

### Generation and Release build

`recipe_simd_regen` ran against the pinned kernel after the latest wave merge. Its four outputs (`RecipeSimd.g.hpp`, `SdfRecipeEvalDispatch.g.inc`, `SdfRecipeCodegenGlslDispatch.g.inc`, and `RecipeTapeEval.g.glsl`) reported no changes.

A fresh Release build with `--clean-first` completed 1,147 actions successfully. All 22 `*_check` targets passed:

`octreeconfig_check`, `recipeparams_check`, `recipe_simd_check`, `sdf_core_kernels_check`, `recipe_opcode_mirror_check`, `lightingconfig_check`, `shadowconfig_check`, `accumulationconfig_check`, `prevcameraconfig_check`, `reservoirconfig_check`, `probegridconfig_check`, `lighttreebuffer_check`, `miningbeambuffer_check`, `reservoirrecord_check`, `view_hud_check`, `view_editor_layers_check`, `view_hud_markup_check`, `view_hud_blob_check`, `view_hud_writer_check`, `appflow_check`, `view_noun_enum_check`, and `callables_check`.

The CMake cache selected `Release`; `VIXEN_YEROKET_KERNEL_SHA_OVERRIDE` was empty. Configure used the explicit workspace kernel root and worktree-local FetchContent and kernel-codegen caches, and auto-discovered the Undertow schema catalogue. It provisioned Vulkan and X11 dependencies. The generated-file check and build logs are `1791549355-build-latest-wave-recipe-simd-regen.log` and `1791549851-build-latest-wave-release-clean-build-checks.log` under `/home/liory/.local/state/undertow/undertow-box-logs/`. A direct force-rebuild attempt using Ninja's `-B` flag failed before check execution (`1791549574-build-latest-wave-force-all-checks.log`); the supported CMake `--clean-first` build above recovered it.

### Test witnesses

- **RenderGraph:** full `RenderGraph` label, serial CTest run; 1,343 passed, zero failed, 11 skipped. The new DZN resource-lock registrations were active. Evidence: `1791550820-test-latest-wave-rendergraph-label-serial.log`.
- **SVO:** 761 passed, 1 failed, 8 skipped, 1 disabled. The sole failure is the known T-1449 exception, `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`: `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94`. No other SVO failure occurred. Evidence: `1791551412-test-latest-wave-svo-serial.log`.
- **Boundscore occupancy exactness and cost:** `RecipeOccupancy.GeneratedReductionMatchesOracleAndMeasuresCellWork` passed all four fixtures; each generated reduction was byte-identical to its dense oracle.

  | Fixture | Exact / sampled cells | Points dense → generated | Median dense → generated |
  |---|---:|---:|---:|
  | Sphere | 1,160 / 2,936 | 262,144 → 201,584 | 1.84874 → 1.29650 ms |
  | Box | 760 / 3,336 | 262,144 → 221,375 | 2.03424 → 1.62207 ms |
  | Union | 832 / 3,264 | 262,144 → 220,764 | 1.66475 → 1.48545 ms |
  | Subtract | 1,452 / 2,644 | 262,144 → 187,872 | 2.57509 → 1.86485 ms |

  Evidence: `1791551498-test-latest-wave-occupancy-dense-oracle.log`.
- **rvcompact identity and cost:** `DeclaredPositionRenderTest.IntervalPrunedTileTapesMatchFullAndUnrolledGpuPixels` passed its full/pruned/unrolled GPU output checks. Whole-domain compaction reported zero differing output bytes against the unrolled oracle.

  | Fixture | Instructions before → compacted | Proof CPU | Full → compacted GPU median | Differing output bytes |
  |---|---:|---:|---:|---:|
  | Dead terms | 325 → 1 | 0.128688 ms | 0.11540 → 0.00628 ms | 0 |
  | Visible edit | 433 → 177 | 0.256417 ms | 0.15868 → 0.06856 ms | 0 |

  Evidence: `1791551507-test-latest-wave-rvcompact-output-identity.log`.
- **Canonical capture assertions:** the isolated selection passed 13/13 (11 canonical assertions and two producer fixtures). Its test list was passed by absolute path because CTest changes into `build/wsl`; the configured Vulkan/X11 `LD_LIBRARY_PATH` was inherited for the three headless-starfield checks. Evidence: `1791551103-test-latest-wave-canonical-captures-abslist.log`.
- **Capture byte identity:** comparison against the wave run 3 Release `capture-a` set failed. The editor image differences reproduce the earlier isolated run exactly; all three HUD PNGs match byte-for-byte.

  | Editor captures | PNG bytes differing | Pixels differing | Maximum channel delta | Bounding box |
  |---|---:|---:|---:|---|
  | 105 / 45 | 6,558 | 81,496 / 250,000 | 178 | x=103..397, y=95..378 |
  | 5 / 75 | 7,447 | 74,187 / 250,000 | 178 | x=103..397, y=95..378 |

  Current editor SHA256 values are `ed362a4235d54e8120fb0e109a7f2ae53d01053d76a7fe9a4e84a8f74068e054` (105/45) and `14a4264775cb4edddf755147dac2c9cad1b5aba5176242b87f838476c4e977a5` (5/75). Corresponding wave hashes are `8d28d52feb20f12a350e1b55c3db55f6aa6a8a28ee60b1836d9acc8183ae04a1` and `bfcdcfddb1c4e1138ca808ea061891ce3554ced883e3c4d85c7148b1235187af`. All three HUD files have zero differing bytes. The profile was Release; the DZN device and driver reported by the capture runs match, as does the `sample_tri_layer.vxd` input SHA. Evidence: `1791551398-light-latest-wave-capture-byte-identity-light.log`.
- **No-op rebuild:** after the tests and restoring the test-mutated `.edited.vxd`, a queued build completed with `ninja: no work to do.` It used `--credits 1` after the default estimate waited on the queue. Evidence: `1791551950-build-latest-wave-final-noop-rebuild-credit1.log`.

KFR follow-up call site remains read-only: `KernelFederationRenderer/app/src/session_renderer.cpp`, `SessionRenderer::BuildRenderGraph()` near line 28. This lane made no KFR edits.

### Verification recovery notes

An initial all-label CTest invocation omitted the loader path. It exposed three immediate `HeadlessStarfieldGraph` `VK_ERROR_INCOMPATIBLE_DRIVER` failures and was stopped with wrapper status 76 before the unrelated long lookdev suite finished (`1791550079-test-latest-wave-rendergraph-serial.log`). The required `RenderGraph` label and the canonical capture selection passed after inheriting the configured Vulkan/X11 library path. This environment omission is already covered by an earlier headless-capture SPT proposal.

The first canonical capture selection also used a relative `--tests-from-file` path and exited before running tests (`1791551093-test-latest-wave-canonical-captures.log`); the absolute-path retry passed 13/13. That CTest working-directory issue is already covered by an earlier SPT proposal.

A forced check invocation forwarded `-B` to the local Ninja frontend and exited before running checks. The documented CMake `--clean-first` build path recovered and passed all 22 generated checks. A final no-op build request initially waited on a cold-build reservation estimate; the supported one-credit queue override completed it successfully.

## STOPs and remaining gaps

- **STOP / REFER-TO-ORCHESTRATOR — editor capture compatibility.** The required comparison remains red after an isolated matched-Release rerun on the latest merged tree, so the merge is not landable yet. The pinned kernel's generated `SdfCoreKernels.g.glsl` includes `precise` return temporaries; this is a plausible source of the editor pixel changes, but this run does not prove causation. The owner must decide whether to preserve the wave byte output in the pinned-kernel shader path, or accept the arithmetic change and approve a new visual baseline. Do not recapture the baseline as a workaround. The wave baseline exists and is reproducible; this is not a baseline-unobtainable classification.
- **Known SVO exception:** T-1449/opcode 94 remains the only SVO failure and stays open. T-1450 also stays open; this lane records the merged-tree cost table but does not close it.
- **No unrelated required-scope failures** were excluded. The initial all-label CTest attempt is documented above as a recovered invocation/environment issue.

## SPT DISPOSITION

No SPT task was closed. T-1449 and T-1450 remain open.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Queue reservation scan can observe a vanished memory-reservation file
- proposed: Queued Ninja frontend rejects the force-rebuild flag
- proposed: Queued no-op CMake builds inherit the cold-build reservation estimate

The explicit kernel-root and worktree-cache configure inputs were already covered by earlier VIXEN consolidation proposals, so this lane did not duplicate those entries. The schema catalogue was discovered automatically.
