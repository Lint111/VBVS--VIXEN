# upscaleinputs — R604(c), T-4585

Temporal input foundations are implemented and opt-in (`VIXEN_RECONSTRUCTION_DEPTH=1`). Motion vectors and jitter remain unfinished. The existing primary-ray, canonical capture, and picking paths are unchanged. T-4585 is PARTIAL.

Evidence root: [upscaleinputs-evidence](/home/liory/codeman-cases/undertow/reports/upscaleinputs-evidence/). Raw logs and captures stay outside the repository.

## LANDABLE NOW

| Commit | Independently useful change | Verification |
|---|---|---|
| `8b8949f2` | Snapshot the previous camera matrix before updating the current camera | Production-camera regression failed before and passed after; core suite |
| `127a5763` | Make Windows C++ min/max and mutex dependencies independent of PCH | Affected MSVC shader/SVO objects compile; fresh WSL build and focused tests |
| `9f1e4741` | Preserve optional catalogue inputs in renderer-provider configurations | Native configure; existing required inputs retain the same generated commands |
| `5cc30fee` | Repair Windows-mapped codegen path translation and freshness inputs | Launcher CPU test, native configure, three real pinned native drift checks |

Merge `eef7cfbd` imported the contract wave `b546c302`; final merge `f248e9f7` imports the profiles wave `f0cda0c84806fb082e1eccf381116e6a4449a335`. The native build was stopped before changing the capability headers, then both builds restarted on the merged tree. Provider selection advertises only actually constructed Depth/Exposure resources, alongside Color. No MotionVectors/Jitter inputs are advertised. `UpscalerModules.*` and CapabilityGraph were not edited by this lane.

## COMMITTED INPUT SLICES — native witness incomplete

These changes are committed and pass the scoped WSL checks. Their required native capture/cost witness could not finish, so they are not reported as fully witnessed landable input slices:

- `da3e0fdf`: internal-resolution depth resources and consumer conventions.
- `a2b6a3eb`: viewport history ownership/reset primitives and linear exposure.
- `34fdde34`: skipped-write history invalidation, red/green regression.
- `23b23095`: history allocation GPU assertion corrected for graph-derived view usage.

## What changed and its limits

**Depth:** `ReconstructionDepth.comp` reads the actual primary HitRecord stream. Positive view depth is `dot(worldPos - cameraPos, cameraDir)`, removing the off-axis factor in Euclidean ray distance. Normalized depth solves the camera's actual inverse-projection equation. A consumer convention table declares forward/reversed [0,1] and [-1,1] encodings (R608.1), with no SDK-specific branches. Misses preserve view depth `1e30` and use the selected consumer's far depth. Two R32F images follow the internal target's live extent. Shader metadata comes from the real SDI generator and its manifest/drift gate.

**History:** each graph group owns two distinct RGBA16F images at output extent, each with one physical image. The CPU state node exposes reset, validity, frame index, write index and reset reasons. Projection/extent/recompile changes invalidate automatically. The viewport owner can signal camera cuts, viewport switches, scene/provider changes through `Invalidate`; a provider must call `CommitHistoryFrame` only after recording/submitting a successful current write. A missing acknowledgement invalidates correspondence on the next frame. No temporal provider exists in this slice, so capture correctly sees reset=1/valid=0. Application cut/switch event wiring and provider write acknowledgement remain integration work; this is the tested ownership/reset primitive, not a completed temporal lifecycle.

**Exposure:** a 1×1 R32F graph image contains the meter's positive linear multiplier, `exp2(ev100 + exposureCompensationEV)`. Scene radiance has pre-exposure 1. The shader bounds pathological/nonfinite inputs; the GPU fixture compares normal auto/fixed exposure against the independently read meter. Real buffer read/write hazards order it after the reducer.

**Camera:** the old previous-camera output referred to a matrix overwritten later in the same Execute. It now snapshots the prior current matrix before updating current data. Translation and resize cases call production callbacks in a GPU-free regression.

Integration names in `sceneProviders`: `viewDepthImage`, `normalizedDepthImage`, `exposureImage`, `reconstructionHistoryA/B`, `reconstructionHistoryReset/Valid/WriteIndex`. Resources are exposed through named subgraph output ports. Downstream providers must bind their actual image usages and frame dependencies; the history images intentionally have no unused storage views before a provider consumes them.

## Verification

Required scope: production camera callbacks, RenderGraph state/resources, the new shaders and generated SDI, application graph wiring, provider selection after merging the concurrent contract lane, and deterministic/native capture. No Undertow or kernel source was edited. Kernel pin remains `96570e48d607bc6f1fa23d0007e5f27eb3f5d472`; Program.Legacy delta is zero.

Fresh baseline at `ad9fa0bdd8b0bf811cc07d9d15e6c2bb0253e074`: WSL configure/build, 10 subgraph tests, original SDI drift check, and canonical Dozen capture passed. All 32 PNGs were byte-identical to the stage-1 wave reference (`baseline-capture-identity.json`). No published baseline matched these new application guide checks, so fresh target binaries and captures were required.

Final checks on the merged tree:

- `final-merged-cpu.log`: fresh merged binaries pass all 22 core tests and all four provider-selection tests; capability records pass 12 tests with one existing full-mode identity skip. Separate GTest JSON files are `final-merged-core.json`, `final-merged-selection.json`, `final-merged-capabilities.json`. The full core reruns followed the skipped-write fix and the dependency/header change from the profiles merge.
- `final-merged-sdi.log`: fresh tool on the merged tree confirms all 24 merged SDI program headers current.
- `final-launcher-args.log`: launcher/payload separation, path spaces, a real payload root, and invalid launcher rejection pass.
- `final-merged-native-codegen.log`: OctreeConfig, LightingConfig and PrevCameraConfig checks pass through the actual generated Windows→WSL commands. `native-codegen-input-contract.json`: 30 commands have nonempty arguments/native dependency paths and exclude launcher cwd from input scans.
- `final-merged-guides-native.log`, `final-merged-guides-reduced-odd.log`, `final-merged-guides-fixed-exposure.log`: all three GPU cases pass; `final-dozen-guide-results.json` and `final-dozen-guides/` retain the per-pixel/exposure results and raw images. Full extent 501×333: 46,138 hits, 120,695 misses, zero failures; reduced 251×167: 11,597 hits, 30,320 misses, zero failures. Maximum view error 1.91e-6, normalized error 2.39e-7. History images are distinct, output-sized and invalid without a producer.
- `final-merged-canonical.log`: canonical Dozen repeat-pixel capture passes. `final-capture-identity.json`: all 32 PNGs byte-identical to both the fresh lane baseline and the stage-1 wave reference, with jitter off. `final-lookdev/` retains the outputs.
- Native build: **incomplete**. The queue cancelled at the last logged step 403/409, exit 75 (`profiles-merged-native-build.log`). Native depth readback and GPU timing are **not run**. `native-final-status.json` records the recovery ladder and exact continuation point.

GPU pixel oracle uses forward projection on CPU, independent of the shader's inverse equation. It checks every hit/miss, finite hit outputs, miss sentinels, and off-axis ray-distance conversion, then reads the actual exposure meter. No Dozen timing is used as performance evidence.

## Native cost and visual inspection

**No native cost numbers or native guide images are claimed.** The build compiled the new history node and reconstruction graph code, then the queue watchdog cancelled during final application compilation/librarian work. The Windows process snapshot showed active compiler/librarian processes although the queue reported zero Linux CPU. After cancellation, even `cmd.exe /c echo native-command-ready` timed out, including all three available WSL interoperability endpoints. Cleanup of the recorded Windows root process was requested but could not be confirmed. Linux-side pending diagnostic/cleanup launchers were terminated explicitly.

Required owner action: restore Windows interoperability, verify whether recorded root PID `178812` and its descendants are still alive before cleaning them up, then resume `build/native-mapped` through the real queue. The prepared `upscaleinputs-native-progress.ps1` reports actual descendant CPU every 15 seconds with a 600-second bound; it is **not yet exercised**. Continue with queued native guide captures and the per-pass CSV analysis. Do not rebuild the passing WSL baseline.

The depth preview maps finite view depth to `255*exp(-depth/20)` with black sky; original R32F bytes are retained. Dozen inspection shows continuous depth across the platform/hull, with the center figure, right foliage and left rocks distinguishable. Native inspection remains outstanding. The `native` subfolder inside `final-dozen-guides/` means full internal resolution on Dozen, not a native-hardware capture. There is no motion-vector image: inventing zero/object-index motion would not satisfy R494.

The complete temporal reconstruction ≤2.0 ms gate is **not measured**: no temporal backend is called. Guide-only timings cannot establish that gate. R494's native per-pass caps remain the acceptance reference (primary 6.0, lighting/visibility/GI 2.0, denoise 1.0, tone map/sky/UI 0.5 ms; graph 12.5 ms).

## Recovery and findings

All heavy commands used `/home/liory/.local/bin/with-test-lock.sh --agent upscaleinputs`; builds were nice 10, parallel ≤4; correctness captures used `gpu`, native timing uses `gpu --gpu-benchmark`. No push, stash, branch switch, or pattern kill occurred.

| First red / cause | Recovery and evidence |
|---|---|
| Baseline build exit 75, watchdog STALL_KILLED at 649/666 steps; output redirected inside payload | Moved redirection outside queue command; fresh baseline build passed. `baseline.json`, `baseline-build-recovered.log`. Inconclusive interruption, not a compiler baseline red. Existing T-2057 covers the cause. |
| CodeGraph explore could not find an isolated-worktree index | Recorded the diagnostic and used source searches; no expensive index build. Existing T-1435 covers this cause. |
| Native compiler link probe cannot use a UNC cwd | `pushd` maps this assigned worktree; all source/build files stay inside it. `native-configure.log` → `native-configure-mapped.log`. |
| Windows Git rejects WSL-owned FetchContent directories | Two narrow, process-only safe.directory entries for this worktree's native fetch/dependency roots. `native-configure-trusted.log`, `native-configure-dependency-trust.log`. |
| Native configure tries WSL Dozen provisioning | Supported `VIXEN_AUTO_PROVISION_WSL_VULKAN=OFF` on native configure. |
| Mapped Z: paths translate to empty codegen flags | Repair drive/share-root translation, validate nonempty paths, cache conversions, and run WSL from `/`. Native configure succeeds and actual codegen checks pass. |
| Launcher `/` accidentally enters freshness directory scan | Separate launcher prefix from payload; retain the whole command in stamp identity. `native-configure-path-bridge.log` and `native-configure-root-cache.log` were watchdog cancellations, then `native-configure-payload-inputs.log` passed. Two verified orphan Windows CMake PIDs were stopped explicitly; `native-orphan-cleanup.json`. |
| MSVC lowercases PCH paths on this case-sensitive mapped worktree, C1083 | Disabling ccache reproduced the same failure; supported `USE_PRECOMPILED_HEADERS=OFF` made Core/tool build pass; RmlUi also required the global `CMAKE_DISABLE_PRECOMPILE_HEADERS=ON` setting. `native-inputs-build.log`, `native-core-no-cache.log`, `native-configure-no-pch.log`, `native-no-pch-core.log`. |
| Windows `max` macro breaks `std::max` without PCH, C2589/C2059 | Declare `NOMINMAX` for Windows C++ in root CMake and guard the central header fallback. `native-global-no-pch-build.log` records the failure; `native-no-pch-platform-build.log` records the retry. |
| Shader compiler uses mutex types without including `<mutex>`, C2039/C4430 | Add the direct standard-library include; `native-no-pch-platform-build.log` → `native-mutex-include-build.log`. |
| My initial `NOMINMAX=1` conflicts with older empty definitions under `/WX`, C2220/C4005 | Use an empty `NOMINMAX` compile definition matching existing headers; retry in `native-empty-nominmax-build.log`. This introduced failure is attributed to and fixed by this lane. |
| Native cache-phase delays on mapped WSL files | Four observed ccache processes had ~3 minutes wall/~15 seconds CPU, with no active compiler at the snapshot. Stop only verified cache PIDs and resume with `CCACHE_DISABLE=1`; interrupted files compile. `native-process-progress.json`, `native-cache-cancel.json`, `native-cache-bypass-build.log`. My edit of a running batch file also caused a CMD parsing error; retries use a separate fixed script. |
| Wave advanced during native compilation | Stop four verified compiler PIDs before merging new headers; resume through queue. `native-profile-merge-interruption.json`. The interrupted target result is inconclusive, not a compiler red. |
| Final native queue cancellation and unavailable Windows execution | Exit 75 at 403/409, despite live Windows compiler/librarian processes. Default cmd health probe timed out after 10 seconds; endpoints `1644_interop`, `461_interop`, `2_interop` each timed out after 5 seconds. Windows cleanup requests did not return. `native-final-status.json` gives A/B/C outcomes; no native images or timings are substituted from Dozen. |
| New GPU fixture demanded distinct views for unconsumed history images | Corrected assertion to validate distinct nonnull physical images and distinct views when present. Graph-derived usage correctly omits unused transfer-only views. Initial red `final-depth-full.log`; corrected witness recorded above. |

A/B/C ladder: initial invocation/provisioning was repaired as above; final Windows command execution remains unavailable (A). SDI was regenerated from source through the configured tool, then drift-checked (B); no required dependency red was excluded or labeled pre-existing without evidence (C). No baseline-unobtainable STOP is claimed. The native helper and exact commands are copied into the evidence directory for reproduction.

## STOPs

**REFER-TO-ORCHESTRATOR — instance identity producer (R494 rule 2).** `ShellOctreeGpu.h:536` defines the existing 144-byte host submission as transform+material; `BodyOctreeSceneNode::SetInstances` accepts only those rows. This checkout has no generated schema Ref/generation field in that stream. Decide which existing schema kind's generated Ref owns a rendered instance and the generated upload facet that supplies its generation. Recommendation: extend that actual typed producer/upload boundary in its authorized content lane, then join previous/current transforms by Ref+generation and emit invalid motion for first use/reuse/disocclusion/sky. A renderer ID registry or array-position history deviates from R494 and is rejected. The previous-camera prerequisite is complete; per-instance transforms and the motion image are not.

## Remaining engineering

**Jitter and deterministic picking (R494 rule 1, R485/R493).** GPU selection reads the primary raymarcher's ID image (`VoxelSelectionProviderNode.cpp:141`), so jittering primary rays moves authoritative picks. The implementation alternatives are a distinct unjittered authoritative pick ray path or disabling jitter whenever that path is active. Recommended continuation: a separate deterministic pick producer, plus relaxation metadata/validation on the named visual output port so canonical capture/parity/picking cannot consume it. Current graph ports do not expose that relaxation contract. No local SDK toggle or unvalidated flag was substituted. Sequence/resource/sampling jitter and the port validation are unfinished engineering, not an unexplained baseline red or a new determinism ruling request.

The missing identity producer and the preferred picking architecture were also asked through the session's asynchronous clarification interface. Independently completed slices remain committed and reviewable. No scope authorization is assumed for another repository or the protected provider modules. Existing T-2859 also applies: `ReconstructionHistory.cpp` had to be listed separately in VixenApp compile sources and node-manifest scan sources (`application/main/CMakeLists.txt:29,53`); derive the latter from the target source list. This is additional evidence for that issue, not a duplicate proposal.

## Additional evidence for existing consolidation issues

Five whole duplicate inbox entries were withdrawn after the broader SPT search. Every remaining entry is byte-identical to the CLI-produced line; no entry payload or hash was edited. `withdrawn-duplicate-proposals.json` retains their original payloads and reasons. The following notes stay in this assigned worktree's report for the controller to attach to existing issues:

- **T-2646 / T-4124:** this lane again reproduced the UNC compiler-link probe failure; mapping the assigned worktree with `pushd` recovered it (`native-configure.log`, `native-configure-mapped.log`).
- **T-2833:** both native FetchContent and the separate libunibreak dependency root required narrow process-only Git trust (`native-configure-trusted.log`, `native-configure-dependency-trust.log`).
- **T-2678:** the native host again attempted Dozen provisioning; the supported native-only override recovered it. No production provisioner fix is claimed.
- **T-2845 / T-2677:** the bridge is removed from data-file dependency collection by `5cc30fee`, while its complete command still participates in the stamp signature. The launcher test, 30-command input audit, and actual pinned commands pass. The native Ninja checked-target run remains outstanding after the final queue/interop failure; these issues are not closed by this report.
- **Imported upscalecontract proposal `769b630953c4`:** this lane adds verified Windows child-process cleanup evidence in `native-orphan-cleanup.json` and `native-cache-cancel.json`; it does not need a second task proposal. The live queue also reported `cpu=0s` after 2,409 seconds while Windows compilation was progressing, so its Linux-side CPU counter cannot classify native compiler activity; the Win32 process snapshots were needed for that diagnosis.

## SPT DISPOSITION

- **PARTIAL — T-2845 / T-2677:** launcher dependency collection is fixed and validated by the CPU fixture, native input audit and direct pinned checks. The actual native Ninja checked-target witness remains after interoperability recovery.
- **PARTIAL — T-4585:** depth conversion/resources, camera history prerequisite, viewport history ownership/reset primitives, and exposure are implemented. Per-instance Ref+generation motion, disocclusion/sky motion policy, deterministic jitter resource/primary sampling with output-port validation, event/provider history integration, native guide readback/timing, the native motion image and complete temporal-call budget remain.

## Continuation

Continuation: [continuation.json](/home/liory/codeman-cases/undertow/reports/upscaleinputs-evidence/continuation.json), validated by `continuation-validation.log` with seven reusable passing checks. Native build cache: `build/native-mapped`; do not clean it. Source implementation is at `f248e9f7`, with inbox deduplication at `8b81ed26`.

Checkpoint point: no more feature items were taken on after the final merged witnesses. The native queue/interop recovery was stopped at the final status recorded above; motion, jitter and history integration remain exactly as listed. All product work is committed. Native process cleanup needs external confirmation before restarting that native tree.

Canonical-shape audit: R412 mechanical added-line checks and `git diff --check` are recorded in the evidence directory. No new schema kinds/Refs/IDs/attributes, simulation state, save migration, or hard-wired content tables were introduced. The depth convention table is explicitly required by R608.1. Camera/history classes are transient renderer state, not schema mirrors. New merged SDI headers are manifest-registered and generated. No kernel pin bump or kernel suite is required for this VIXEN-only change.

## CONSOLIDATION ISSUES

- proposed: Expose typed float guide readback through the graph debug capture surface
- proposed: Resolve mapped Windows paths before the codegen WSL bridge
- proposed: Derive graph guide hazards from reflected SDI resource access
- proposed: Avoid MSVC PCH path case folding on mapped case-sensitive WSL worktrees
- proposed: Make Windows platform macro definitions independent of optional PCH
- proposed: Declare shader compiler mutex dependency without relying on SDK headers
- proposed: Select a native compiler-cache route for WSL-mapped build trees
