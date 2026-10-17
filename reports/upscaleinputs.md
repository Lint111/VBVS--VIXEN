# upscaleinputs — run 2, R604(c), T-4585

Motion guides, deterministic primary jitter, native-resolution unjittered picking, and viewport history integration are committed. Motion/jitter/history functional checks, canonical byte identity, merged native build and six native captures pass. Native 1080p primary, frame-span and submission budgets remain unmet; this is not realtime or temporal-provider performance qualification. No design STOP remains after the controller's instance-identity ruling.

Evidence: `/home/liory/codeman-cases/undertow/reports/upscaleinputs-evidence/`. Raw captures, logs, verdicts and reproduction helpers stay there. The run-1 report is preserved in commit `bcd24efd`; this report supersedes its unfinished engineering and identity STOP.

## LANDABLE NOW

| SHA | Change and dependency |
|---|---|
| `dfefc098` | Deterministic Halton(2,3) primary-jitter sequence; canonical, parity and picking return zero |
| `a0ac90cc` | Materialize bridged CodegenTool caches inside WSL; actual Windows launcher and fresh pinned-tool build pass |
| `8484c16a` | Reuse immutable completed kernel snapshots without taking the publication lock; red/green regression and actual Windows reader pass |
| `776bb428` | Isolate jitter-only UV/footprint code; guide-on/off PNGs become byte-identical (zero differing pixels) |
| `827260c6` | Shared GPU fixture target/pool contract repair; five shadow tests pass with zero Vulkan validation errors/warnings |
| `e7548ed9` | Named group output ports declare visual relaxation; deterministic consumers and nested laundering are rejected |
| `6ee2a36d` + `415fd198` | Typed producer-side correspondence, motion resources, optional previous-transform upload, jittered visual rays, unjittered native picks, current-frame depth ordering, and viewport history writes/resets. Land together: `415fd198` restores the 96-byte hot transform ABI and moves previous transforms to their own optional stream |

`b0127390` merges wave `3f591f3b` before final witnesses. Previous committed depth/exposure/history foundations (`da3e0fdf`, `a2b6a3eb`, `34fdde34`, `23b23095`) remain in the series. The final native compile/link and all five pinned generation checks now pass on `a0ac90cc`. No push, stash, branch switch, or other lane's worktree edits occurred.

## Behaviour and limits

**Identity and motion.** `BodyInstanceSource<TKindRef>` takes the source kind's generated Ref and its slot generation; the producer owns `InstanceMotionHistory<TKindRef>`. It retains previous transforms across row reorder, rejects duplicate Refs, generation reuse (including high generation bits), deletion and material/recipe deformation, and keeps no renderer ID registry. No generated schema-row producer exists in the current VIXEN instance upload callers: editor preview and scene instances are procedural. Those rows remain correspondence-invalid and need the R543 stable-procedural-seed follow-up. Generic test keys are fixture values, never production identities.

The raymarcher emits RG32F `previousUV-currentUV`, right/down, at internal extent, using unjittered current/previous cameras and the previous object transform for valid correspondence. Its R32F validity is geometric correspondence, not proof of prior visibility. Consumers must reject disocclusion against previous depth. First use/reuse/deformation, bad/out-of-bounds previous projections and sky have invalid correspondence; sky motion is zero. Unknown procedural correspondence retains a camera-only candidate with validity zero. Current transform/material streams retain their existing 96/48-byte ABI; generated motion is a separate optional 80-byte per-frame stream.

**Jitter and picking.** The 16-sample Halton sequence is expressed in internal-resolution pixel units. It is enabled only for the explicit interactive visual path, restarts with history, and leaves camera matrices unjittered. `VIXEN_RECONSTRUCTION_JITTER=1` opts the interactive app in; canonical capture/parity remain off. Jitter requires HDR and the supported non-proxy primary traversal. The group's `VISUAL_HITS` output declares R494 relaxation; consumers must explicitly accept it. `NATIVE_PICKING` stays deterministic: it traces separate unjittered rays at presentation extent with the native ray footprint. The raw pick buffers at render scales 1.0 and 0.5 both match the unjittered 501×333 reference byte for byte.

**History and exposure.** Each viewport graph owns two distinct output-sized RGBA16F images. An optional pass seeds the selected image from current scene-linear HDR without blending it into the visible image. The owner acknowledges only a successfully submitted current write; failed/skipped writes invalidate the next frame. Projection/extent/recompile, explicit cuts and viewport activation changes reset history and jitter. Camera pose-sequence cuts advance the epoch; continuous motion does not. The application exposes cut/activation entry points; this checkout has no separate multi-viewport switch dispatcher. A future temporal provider replaces the seed and write acknowledgement with its own reconstructed-history write. The existing positive linear exposure input remains `exp2(ev100 + exposureCompensationEV)`.

No temporal reconstruction backend is called in this lane. A complete-call ≤2.0 ms qualification cannot be inferred from guide or history-seed timings.

## Verification

Required scope: camera/history callbacks, typed source correspondence, graph boundary validation, instance stream consumers, generated GPU records/SDI, application wiring, provider selection, and deterministic/native capture. No Undertow/kernel sources or pins changed. Kernel pin: `96570e48d607bc6f1fa23d0007e5f27eb3f5d472`. The merged text interface is consumed read-only from `/home/liory/projects/undertow`; `run2-text-provider-inputs.json` records its commit and exact generated input hashes.

- `run2-merged-target-build.log`: fresh Release merged application, core/subgraph/selection/capability/SVO/ABI/shadow targets and BodyInstanceMotion/PrevCameraConfig generation checks pass. `run2-jitter-isolation-sdi.log`: all 25 merged program interfaces are current.
- `run2-frozen-cpu.log` plus separate GTest JSON: core 28, subgraphs 12, selection 4, capability 12 plus one accepted FULL-mode identity skip, SVO streams 9, and actual PrevCameraConfig shader ABI 1 pass. The camera record is 80 bytes, sampling offset 64 and padding offset 76. `run2-text-interface-test.log`: two generated text-interface dependency tests pass.
- `run2-isolation-history.log`, `run2-isolation-jitter-full.log`, `run2-isolation-jitter-reduced.log`: current-frame depth/exposure/motion and bit-exact HDR-to-history oracles pass; cut/viewport resets and subsequent valid writes pass. `run2-frozen-picking-identity.json` records full-D pick equality for both jitter scales (1,334,664 bytes each).
- `run2-frozen-canonical.log` passes in 456.9 seconds; `run2-frozen-capture-identity.json` confirms all 32 freshly written PNGs byte-identical to the wave reference on `776bb428`, with jitter off. `run2-frozen-lookdev/` retains them. The repeat follows the jitter-isolation fix; the preceding merged capture also passed.
- `run2-guide-color-identity.json`: fresh guide-on/off 501×333 fixed-exposure PNGs are byte-identical after isolating the jitter-only compile path. The preceding 16-pixel / one-channel-level difference and both input images remain in `run2-jitter-isolation-before/`. R427 uses its mechanism-lane byte-identity rule.
- Shadow fixture: five pixel tests pass and validation errors are cleared. `run2-frozen-shadow-clean.log` confirms zero validation errors/warnings after deriving descriptor-pool capacities.
- R412 shape audit and protected-area comparison pass on `a0ac90cc`: 14 checks, protected source matches the merged wave.

There was no matching published baseline for these new guide/lifecycle tests. The unchanged run-1 baseline was reused through its seven-check continuation record; no unnecessary full kernel or product baseline was rebuilt. All build/test/generation commands use the live global queue, native timing uses `gpu --gpu-benchmark`, builds are nice 10 and parallel ≤4.

## Native witness

The merged MSVC Release build on `a0ac90cc` passes, including OctreeConfig, merged Recipe SIMD/GLSL, LightingConfig, PrevCameraConfig and BodyInstanceMotion checks, executable linking and runtime-DLL staging. `run2-native-wsl-materializer.log` ends in exit 0. `run2-native-executable.json` records the exact source/binary hash. Keep `build/native-mapped` and `.tmp/kernel-cache-lf`; do not clean or rebuild this baseline.

All six native captures pass: 384 frames, including odd-size motion/history, 1080p half/full internal resolution, 1080p half-resolution jitter, a same-build guides-off control and repeated guide-on control. `native-final/` retains PNGs, float/half-float buffers, JSON verdicts and CSVs. `run2-native-capture-summary.json` records readback and inspection; `run2-native-guide-costs.json` contains every exported per-pass statistic. `run2-native-evidence-analysis.log` validates the artifacts through the live queue.

The log confirms AMD Radeon 8060S, AMD proprietary Vulkan driver 25.20.18 (LLPC), API 1.4.320; Windows driver 32.0.22018.5. Each case is a separate `gpu --gpu-benchmark` queue job: 64 frames, steady CSV frames 33–64, fixed exposure and LOD `0.00165685425`, last-completed dispatch durations, percentile interpolation `(N-1)*p`. CSV primary timestamps span the pass, including gaps between submissions; the GPU-yield log separately sums submission intervals. Both are reported. Motion and jitter run inside primary: the separate-pass sum below covers **depth + exposure + history seed only**.

| Native case (input→output) | Primary span p50 / p95 ms | Sum of primary submissions p95 ms | Separate guide passes p50 / p95 ms | Registered frame span p95 ms | Max submission, all 64 frames ms |
|---|---:|---:|---:|---:|---:|
| Motion 251×167→501×333 | 3.11 / 4.30 | 4.53 | 0.027 / 0.036 | 6.76 | 2.475 |
| Motion 960×540→1920×1080 | 42.12 / 68.16 | 48.09 | 0.337 / 0.354 | 74.07 | 3.383 |
| Jitter 960×540→1920×1080 + native picks | 228.69 / 351.90 | 227.65 | 0.446 / 0.568 | 375.61 | 3.740 |
| Motion 1920×1080→1920×1080 | 215.00 / 330.98 | 209.55 | 1.156 / 1.667 | 357.62 | 4.283 |
| Guides off 960×540→1920×1080 | 65.11 / 104.90 | 66.53 | off | 116.87 | 3.975 |
| Motion half-resolution repeat | 44.09 / 73.90 | 58.48 | 0.348 / 0.429 | 80.70 | 2.890 |

**R494 result: budgets are not qualified.** At 1080p the 6 ms primary cap is missed even when measured as the sum of submissions. The 12.5 ms registered frame cap is missed, as is the absolute 2 ms submission cap. The half-resolution guide-on steady maximum is 1.961 ms initially, but the repeat reaches 2.890 ms; full resolution reaches 4.220 ms and jitter 3.740 ms in steady records. The guides-off control itself misses these caps. It does not prove an original-base regression or a performance improvement; run-to-run variation is too large to infer a small guide-overhead delta.

| Exported pass | R494 starting cap ms | Motion half p95 ms | Motion full p95 ms | Jitter half p95 ms |
|---|---:|---:|---:|---:|
| Depth | — | 0.2733 | 1.5173 | 0.4128 |
| Exposure input | — | 0.0041 | 0.0079 | 0.0110 |
| History seed | — | 0.0809 | 0.1348 | 0.1531 |
| Visibility | 2.0 | 2.9206 | 19.3389 | 5.0528 |
| Spatial reuse | 2.0 | 0.4679 | 2.8352 | 0.8814 |
| Tone map | 0.5 | 0.0397 | 0.2086 | 0.0748 |
| HUD/UI | 0.5 | 0.0020 | 0.0028 | 0.0040 |

Sky/denoise lack a dedicated exported metric; zero optional dispatches do not certify active work. Whole-frame span covers the registered sources. No complete temporal reconstruction call exists here, so the ≤2.0 ms complete-call criterion remains unmeasured. A cheap sum of separate guide passes cannot certify it. The jitter path's full-D unjittered picking trace is included in its large primary cost and needs further work before realtime use.

**Inspection.** I viewed native depth, motion and history PNGs at odd, half and full resolution. Depth has smooth gray distance bands and correctly matching scene silhouettes, with black sky misses. Camera-motion previews have subtle green on the left and purple on the right, consistent with yaw; at the small size they are nearly neutral gray. Raw full-resolution motion checks 479,156 nonzero vectors with maximum UV error `4.84427e-7`; full depth checks 479,452 hits with maximum view error `1.90735e-6`, all zero failures. Static-camera jitter motion is neutral gray with zero meaningful camera vectors, so jitter is not encoded as object/camera motion. It exposes jitter `[-0.46875, 0.0925926]` input pixels while its picking extent remains 1920×1080. Both history images contain the current scene-linear seed; half-resolution history has expected nearest-sampled edge steps, and full-resolution edges are finer. These are unblended seeds, not temporally reconstructed or accumulated images.

All native processes exit normally. `run2-native-final-process-cleanup.json` records no remaining lane processes at 05:11:50 UTC. No native process is left for the controller to clean up.

## Recovery and findings

- Native progress/status bridge: the first resumed compiler red was the stale `outputCount` name in group finalization; it was corrected to the actual output vector size. PowerShell's null `ExitCode` hid that compiler status, so a per-process exit marker now fails closed; an injected exit 23 was observed as 23. Existing T-2057/T-4151 cover native progress/status handling.
- The 40-minute native run reached 73/108 before its explicit bound. It killed only its verified process tree; root PID 189792 was subsequently absent. This is an interrupted build, not a baseline compiler failure. All produced objects were retained.
- The required wave merge adds a text-interface root. The first WSL regeneration failed with `Text requires UNDERTOW_ROOT`; passing the documented root recovered the fresh build. A verified native tree (root 31736) was stopped before retrying with the same read-only root. Imported textshader proposal `fd1084d8df9e` covers forwarding it; no duplicate was filed. The native no-new-mutex skip is covered by imported proposal `24fd53dd9c6d`.
- Native CMake accepted the backslash UNC root but generated an invalid `VerifyGlobs.cmake` pattern (`\\wsl.localhost\Ubuntu.../core/src/*.cs`, invalid `\U` escape). `run2-merged-native-build-text-root.log` records exit 1 after generation. The recovery uses `UNDERTOW_ROOT=//wsl.localhost/Ubuntu/home/liory/projects/undertow` and an explicit supported configure before build; generated files are never hand-edited. This extends the native-runner root/path family T-4499, specifically existing member I0128 / proposal `daeca823a4d9`.
- The normalized-root native build completed all C++ compilation through 57/64, then its OctreeConfig check failed because the bundled Ninja is PE32 and its 32-bit `cmd.exe` cannot execute `C:/Windows/System32/wsl.exe`. The same launcher succeeds from 64-bit PowerShell. `run2-native-wsl-cmd32-probe.log` proves the documented `C:/Users/liory/AppData/Local/Microsoft/WindowsApps/wsl.exe` alias succeeds from that exact 32-bit context. Recovery configures supported `-DWSL_EXE=.../WindowsApps/wsl.exe`, retaining every compiled object; T-2677 remains a launcher-selection finding rather than a new proposal.
- After the alias repair, native Recipe SIMD generation reported stale outputs with empty text diffs. `run2-native-snapshot-integrity.json` proves the shared native snapshot has CRLF bytes where the exact pinned Git blob and WSL snapshot have LF; its pin-only stamp accepted different bytes. The documented snapshot function provisions `.tmp/kernel-cache-lf` from the unchanged pin, and supported `VIXEN_KERNEL_CACHE_DIR` selects that owned source/tool cache. Shared caches and generated files remain untouched. Proposal `b79324b09f5e` records this distinct cache-integrity hole.
- The owned-cache configure and an independent native `file(LOCK ... TIMEOUT 2)` probe both hung on the WSL-backed path. Verified own roots/probe processes were terminated (`run2-native-lock-cleanup.log`). T-2818/T-4153 already describe this exact issue. Completed immutable snapshots now reuse without a writer lock; `run2-snapshot-reuse-before.log` fails, `run2-snapshot-reuse-after.log` and `run2-native-snapshot-read.log` pass. Bridged tool materialization is moved to WSL so its creation lock, compiler and copy run on the same side; `run2-wsl-tool-cache-route.log` confirms the ordinary WSL path remains unchanged. The actual native-generated tool-build recipe passes a fresh 16.67-second build with zero warnings/errors (`run2-bridged-tool-recipe.log`), and the real Windows alias→WSL CMake invocation reuses and materializes it successfully (`run2-bridge-materializer-invocation.log`). This uses an owned pre-provisioned source snapshot; it does not claim that native publication locks on WSL paths are repaired.
- Test invocation mistakes were diagnosed from output, not exit alone: a misspelled CTest name selected no tests; subsequent commands use the registered `Application.ReconstructionInputs.*` names and `--no-tests=error`. `sdi_merged_drift_check` is a CTest case, not a Ninja target; the actual generation check passes through CTest.
- Initial history readback differed at 1,964 pixels; initial jitter depth readback differed at 46,087. Explicit current HDR→history and primary→depth completion edges fixed them without weakening the pixel oracles. Existing T-2874 and this lane's SDI hazard proposal cover deriving those edges.
- The guide-toggle control exposed 16 one-level pixel differences even with jitter zero. Compiling UV addition and the mutable pick footprint only under interface-invariant `VIXEN_PRIMARY_JITTER` restored exact PNG identity. The existing query-footprint proposal covers deriving this sampling specialization. Native SPIR-V header regeneration also produced CRLF-only working-file differences; Git normalization confirms no blob delta, extending imported textshader proposal `8a221cbca5e9`.
- The shadow consumer fixture exposed a 96-byte march block used for a 112-byte shading block, missing output image transitions, Vulkan 1.3 test SPIR-V on Dozen 1.2, and stale descriptor-pool counts. It now uses the generated shading block, transitions every bound output, targets Vulkan 1.2 consistently, and derives pool capacities. These extend T-2863 and T-4157; they are incidental fixture fixes, not a new facade feature.
- Manual native extent projection extends T-4278/T-4157; duplicate node-source/manifest registration extends T-2859. CodeGraph's absent worktree index remains covered by T-1435; its fallback was used, without indexing.

The per-instance cache is host producer state, not a schema mirror. The neutral GPU declaration follows the existing `LightingConfig.cs`/generated-upload pattern. No kind, schema Ref, ID space, migration, simulation state or new attribute was invented. Depth conventions remain the explicit R608.1 consumer table.

## STOPs

None. The controller's R494 identity ruling is applied. R543 procedural identities are a reported follow-up, not an unresolved design decision.

## SPT DISPOSITION

- **PARTIAL — T-4585:** motion/jitter/history implementation and requested deterministic/native witnesses are complete. Realtime budget acceptance is unmet. Current procedural producers still need the stated R543 stable-seed identity follow-up; they remain correspondence-invalid. Complete temporal-call qualification belongs to an actual temporal backend.
- **CLOSE — T-2863:** matching test shader targets and derived descriptor capacities pass five actual consumer tests with zero validation errors/warnings.
- **CLOSE — T-2845:** the final native Ninja invocation passes all five pinned checks with the owned canonical source snapshot; no generated golden or pin was changed.
- **PARTIAL — T-2677 / T-2818:** the Windows Apps alias and WSL materializer recover the actual checked targets. Launcher selection and Linux pre-provisioning of the source cache remain manual; native creation locks on WSL-backed paths are not repaired.

The independent hit-record projection witness (`run2-primary-sample-alignment.json`) fits the static unjittered camera from 513 world-hit/pixel pairs, checks all 46,138 control hits, then verifies the exposed current jitter against 46,102 full-size and 11,599 half-size hit positions. Maximum error is 0.000056 input pixels; both cases have zero failures. It uses the saved raw hit records and changes no renderer code.

## Checkpoint and remaining work

Stopped after completing all six native captures and inspecting the requested native depth/motion/history images, before taking on performance optimization. Remaining: reduce native primary/submission cost (especially the jitter path's separate full-D picking trace), supply R543 stable-procedural-seed identities in current procedural producers, and qualify a real temporal backend's complete call. No design STOP remains. The shared source-Ref/generation producer path is implemented, but this procedural scene is not yet a correspondence-valid temporal producer.

Continuation records beside the evidence: `continuation.json` preserves renderer results at their actual tested commit `776bb428`; `continuation-native-run2.json` records cache/generation/native witnesses at `a0ac90cc`. No application, library, shader or SDI source changed between those commits; later changes are cache provisioning and proposals. The read-only generated text input hashes remain unchanged. Retained reproduction helpers are in `reproduction-run2/`. Resume with the continuation checker and keep the completed build/capture products.

## CONSOLIDATION ISSUES

- proposed: Expose typed float guide readback through the graph debug capture surface
- proposed: Resolve mapped Windows paths before the codegen WSL bridge
- proposed: Derive graph guide hazards from reflected SDI resource access
- proposed: Avoid MSVC PCH path case folding on mapped case-sensitive WSL worktrees
- proposed: Make Windows platform macro definitions independent of optional PCH
- proposed: Declare shader compiler mutex dependency without relying on SDK headers
- proposed: Select a native compiler-cache route for WSL-mapped build trees
- proposed: Declare per-query ray footprints instead of shader-global LOD overrides
- proposed: Derive declared instance streams and their frame-ring upload wiring
- proposed: Derive history write acknowledgements and viewport resets from group contracts
- proposed: Derive GPU timing sources from declared render-graph stages
- proposed: Verify pinned kernel snapshot bytes before reusing generator caches
