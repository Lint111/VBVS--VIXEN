# vel-024-vixen

| id | title | verdict | evidence |
|---|---|---|---|
| T-4923 | Look-dev SDF captures inherit host shadow diagnostic overrides | STALE-ON-BRANCH | lane-vel019vixen@e44e40b2 (T-4922: post-pass appends the two unsets to every LookdevCapture/ReconstructionInputs/ExposureTonemapParity case; 73 tests checked). The glyph/SDF cases on this base already unset them. |
| T-4925 | (same title) | DUPLICATE-OF T-4923 | same root cause and fix |
| T-5001 | scope-compatible systemd-run wait semantics | FIXED (53f7c96e) | `tools/run-capped.sh <cap> -- cmd`: scope without --wait, exit status propagated, memory.peak/oom_kill read inside the scope. MEASURED: exit 3 propagated, peak 5210112, oom_kill 0. |
| T-5478 | Tone-map reference flag treats zero as enabled | FIXED (f6ea09e5) | BuildRenderGraph.cpp reads it with envFlagEnabledOrDefault (explicit 0/1); system-mode parity and saturation CTest cases unset the variable. MEASURED: `ctest --show-only=json-v1` shows the unset on system cases only; BuildRenderGraph.cpp.o compiles. No GPU run. |
| T-5586 | Queued GPU capture needs manual Vulkan env | STALE | CTest cases get VK_ICD_FILENAMES/VK_LAYER_PATH/LD_LIBRARY_PATH from `VIXEN_CTEST_GPU_ENVIRONMENT` (VixenTesting.cmake, 6dc14bee 2026-10-01, cb3903e8); standalone harnesses use tools/vixen_gpu_env.py (T-2614). Only a raw `vixen_lookdev_capture` run outside ctest lacks it. |
| T-6369 | Provision WSL Vulkan loader env for queued captures | DUPLICATE-OF T-5586 | same fix evidence |
| T-6410 | Look-dev review crops must use the presentation path | BLOCKED | the change is a line in `~/scripts/.../_visual-lane-rules.md` (not editable here); the repo has no crop script defaulting to canonical. The harness side already exists (VIXEN_LOOKDEV_CAPTURE_SAMPLING=presentation, `Application.LookdevCapture.Presentation.<state>`, test_lookdev_capture.cmake:61). Brief: add the rule line and say which path the crops came from. |
| T-6564 | Capture wrappers should expose persistent output dirs | NEEDS-LANE | 24 `runtime-captures` sites bake `${CMAKE_BINARY_DIR}`; the "common capture-root contract" with the queue's `/tmp/undertow-box-captures` redirect is queue-side (no definition in this repo) and needs a ruling. Brief: define one `VIXEN_CAPTURE_ROOT` env/cache var honoured by look-dev, planet, cel and editor CTest registrations, and have the queue copy rather than redirect. |
| T-6903 | Runtime shader cache ignores included GLSL/SDI changes | STALE (test added, f6ea09e5) | ShaderCompiler::CompileCached keys on the preprocessor-inlined source (22a37b22, 2026-10-22), so includes/SDI are covered. New test `CompileCache_IncludedFileChangeIsAMiss`: unchanged include hits, edited include recompiles; MEASURED 7/7 `CompileCache*` pass. ShaderCompilationCacher::ComputeKey hashes only one file but has no runtime caller. Not checked: the VkPipelineCache layer and the epoch dirs in the look-dev cmake (cache-r768*), which are manual workarounds that can now be dropped. |
| T-4727 | Standalone configure should derive UNDERTOW_ROOT | STALE | VIXEN/CMakeLists.txt:20-45 derives it (paired lane, then sibling); 37855c79. MEASURED: configure in this worktree with UNDERTOW_ROOT unset succeeded. |

## LANDABLE NOW
53f7c96e (branch lane-vel024vixen, contains f6ea09e5 and 53f7c96e on top of 0d7cf275). Touches BuildRenderGraph.cpp, test_lookdev_capture.cmake, test_shader_compiler.cpp, tools/run-capped.sh. It overlaps lane-vel019vixen in test_lookdev_capture.cmake (different hunks; expect a trivial merge).

## CONSOLIDATION ISSUES
- Several SPT items were filed against behaviour that was already fixed on the base; the duplicate hints are all anchor-only and don't separate fixed from open. A fixed-in commit link on close would save re-triage.
- `reports/` and `tools/` under the repo root: `VIXEN/.gitignore` ignores `VIXEN/tools/`, so helper scripts there need `git add -f`.
