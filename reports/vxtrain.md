# Lane vxtrain — controller review

Review continuation of checkpoint `231314f3`; no rebase or merge rewriting. Product tip at review start: `853b659c`.

## LANDABLE NOW

The merge train remains in the requested order on `origin/wave/authoring-convergence`:

| Lane | Merge commit | Lane tip |
|---|---|---|
| gpuyield | `1e26ddec` | `e7d8a198` |
| lights0 | `8e31dbb1` | `e6555dcf` |
| shellasync0 | `90ec57b4` | `d3b2cf9f` |

There were no gpuyield/lights0 conflicts. The shellasync0 conflict in `BodyOctreeSceneNode.cpp` kept both `EmitterPower.h` and `Recipe/RecipeBaker.h`. Overlapping native fixes were consolidated into one macro-safe `std::max` spelling, and the shader identifier `flatLookup` was retained. All three merges are `--no-ff` with the requested messages and co-author trailer.

Follow-on commits before this review: `1d05ed93` publishes `EmitterRootPowers` with the async shell generation; `4e0f4371` emits typed SDI integer push vectors; `7d4540d1` discovers Visual Studio Build Tools; `231314f3` preserves graph-free shell publication; `a400a730` publishes the complete shell generation and power lane at t0; `853b659c` makes RenderGraph/SVO fixtures MSVC-portable.

### Zero-hit attribution and repair

The three requested cases pass on the wave base and both earlier merge steps. Their first zero-hit result is after shellasync0:

| Snapshot | `RenderStoredSdfBodiesNoHoles` | `RenderStoredSdfMultiChannel` | `RenderRecipeBakedBody` |
|---|---:|---:|---:|
| Wave base `5dd78d86` | pass | pass | pass |
| After gpuyield `1e26ddec` | pass | pass | pass |
| After lights0 `8e31dbb1` | pass | pass | pass |
| After shellasync0 `90ec57b4` | zero hits | zero hits | zero hits |
| Checkpoint `231314f3` | zero hits | zero hits | zero hits |
| Root publication fix `a400a730` / current product tip | pass | pass | pass |

The shell producer now derives and publishes the initial complete generation synchronously during compile, including the power lane, before downstream handles bind. Later edits still publish asynchronously. Base/gpuyield/lights0/shellasync0 evidence is in logs `1791719582`, `1791720056`, `1791720247`, and `1791720417`; the repaired full suite log `1791720918` records all three passing after `a400a730`. The focused eight-case shell log `1791720524` also records the three requested cases passing, but one separate `RematerializeEditLoop` run failed there; the later full suite passed that case.

### Builds, generation, and focused tests

- Fresh Linux Release build passed after all source edits, including all 26 generation checks: `/home/liory/.local/state/undertow/undertow-box-logs/1791726980-build-linux-final-build-after-native-portability.log`.
- `sdi_tool merge-variants shaders/sdi-variants.json --check` passed; recipe/SDI generated outputs were current. The no-new-mutex gate passed from WSL: 844 files scanned, 83 mutex-syntax files, 47 owning declarations, 2 existing `UNCLASSIFIED` warnings (`1791735672-light-final-no-new-mutex-wsl-gate.log`).
- gpuyield focused tests passed 47/47 (including its 41 targeted cases); emitter/SDI/power tests passed 17/17; shell focused tests passed four-frame publication and eight-identical-frame checks. Logs are recorded in the checkpoint report and queue history: `1791710012-test-vxtrain:gpuyield-41.log`, `1791710022-test-vxtrain:emitter-and-sdi-tests.log`, `1791711003-gpu-vxtrain:shell-focused-dozen.log`, and `1791711359-gpu-vxtrain:async-emitter-power-dozen.log`.
- Fresh tip MSVC Release build passed on the Windows-filesystem checkout `C:\vixen-native\vxtrain`, with 1,160 targets: `/home/liory/.local/state/undertow/undertow-box-logs/1791725650-build-native-final-release-fresh-dir.log`. Native R6 passed on the integrated GPU: 120 bounded submissions, zero unbounded submissions, maximum 1.727840 ms against the 2.0 ms budget; log `1791729998-gpu-native-tip-r6-env-restored.log`.

### Full-suite name comparison

The fresh wave-base suite completed 2,124 labeled RenderGraph/SVO cases. Its only failing name is `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` (opcode 94), the accepted red. All three zero-hit tests passed on base. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791736360-gpu-wave-base-rendergraph-svo-suite.log`.

The identical final-tip command ran as `final-tip-rendergraph-svo-suite` (`ctest --test-dir build/wsl -L 'RenderGraph|SVO' --output-on-failure --parallel 1`), log `/home/liory/.local/state/undertow/undertow-box-logs/1791738451-gpu-final-tip-rendergraph-svo-suite.log`. It passed 2,170/2,171 cases, including all three stored/baked-body tests. At the three-hour checkpoint, `vixen_multiinstance_cache_gate` was in its `fresh-after-cold` stage. CTest received SIGINT and exited 130 at 2,170/2,171; the queue wrapper returned 130 and no suite summary was emitted. The only failure recorded before interruption was the accepted opcode-94 `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes`; the complete final-tree failure-name set is unavailable.

### Look-dev captures

All four 500x500 midday PNGs are byte-identical across base → gpuyield → lights0 → shellasync0 → fixed/current tip: every comparison reports zero differing bytes and zero differing pixels. The captures and `tools/compare-capture-pixels.py` results are documented in [the visual attribution record](vxtrain-visual/attribution.md).

Current-tip overcast and night-service each have four saved first-pass angles. Overcast's internal repeat completed byte-identically for all angles; night-service's repeat timed out after two repeat angles, which match. The three available base overcast angles match the tip by SHA-256; base angle 3 is missing after DZN device loss. No full base night-service set was captured. There are no observed unexplained pixel changes; midday is fully attributed at each merge boundary.

## STOPs

- **Final full-suite name comparison is incomplete.** The wave-base suite completed 2,124 cases and its only failing name is opcode-94. At the three-hour checkpoint, the final-tip run reached 2,170/2,171, with `vixen_multiinstance_cache_gate` at `fresh-after-cold`; CTest was interrupted with SIGINT and exited 130. Only opcode-94 had failed before interruption, but the final suite summary and complete failure-name set are unavailable. Do not land until the full tip run completes and the final name set is confirmed. Logs: `/home/liory/.local/state/undertow/undertow-box-logs/1791736360-gpu-wave-base-rendergraph-svo-suite.log` and `/home/liory/.local/state/undertow/undertow-box-logs/1791738451-gpu-final-tip-rendergraph-svo-suite.log`.
- **Wave-base native R6 comparison is unobtainable on this base.** The wave-base native Release attempt used the same Windows-filesystem Recovery A route, then failed while compiling `VIXEN/libraries/SVO/src/BulkMaterialization.cpp`: base-generated `RecipeSimd.g.hpp` warnings C4457 are errors under `/WX` (log `/home/liory/.local/state/undertow/undertow-box-logs/1791730739-build-native-base-editor-target-batch-quoted.log`). Recovery A reached target compilation but failed on that source diagnostic. Recovery B is not applicable: base codegen had fresh inputs and no stale generated output was found; regeneration cannot repair a compile warning in the base emitter's output without changing the baseline. Recovery C is not applicable because the base has no `GpuSubmissionsRespectDeclaredBudget` R6 test or `[GPUYIELD]` instrumentation. Tip build/R6 passed, but a same-route base measurement cannot be reported.
- **Base night-service visual comparison is incomplete.** The base Dozen capture lost its device; log `/home/liory/.local/state/undertow/undertow-box-logs/1791726137-gpu-wave-base-lookdev-night-service.log`. Tip's four angles are captured. Base overcast is partial at angles 0–2 and base late-afternoon at angles 0–1; all available PNGs are byte-identical to tip. Their images and failure status are saved in `reports/vxtrain-visual/wave-base/`.

## SPT DISPOSITION

No tracked SPT tasks were attached. Existing proposal `fa6fb7067691` covers preserving graph-free shell materialization. Incidental authoring/tooling gaps are listed below and in the committed `.spt-proposals/vxtrain.jsonl` inbox.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Provision native VIXEN tools in assigned WSL worktrees before the MSVC gate
- proposed: Run merged SDI freshness check in the default VIXEN build
- proposed: Include Visual Studio Build Tools in build.bat vswhere discovery
- proposed: Make native lane cache and Ninja job cap automatic in the VIXEN build
- proposed: Run native MSVC configuration through a mapped worktree path
- proposed: Scope Windows Git safe-directory trust to VIXEN's WSL FetchContent cache
- proposed: Detect WSL Dozen provisioning from the host instead of a shared source path
- proposed: Emit reflected integer vectors as typed SDI push-block members
- proposed: Resolve the pinned Yeroket root and snapshot cache per VIXEN worktree
- proposed: Keep native VIXEN compiler caches inside the assigned worktree
- proposed: Pass native CMake cache overrides through the locked VIXEN launcher
- proposed: Make native kernel snapshot locking work in WSL worktrees
- proposed: Preserve graph-free shell materialization behavior
- proposed: Provide a root-aware native look-dev capture runner
- proposed: Quote queued native batch arguments from WSL
- proposed: Select a Ninja-visible WSL launcher for native codegen
- proposed: Partition look-dev states under the CTest timeout
- proposed: Use portable environment mutation in RenderGraph tests
- proposed: Avoid Windows API macro names in the subgraph fixture
- proposed: Give the photon mirror target its required C++23 feature
- proposed: Avoid the Windows small macro in RecipeBaker tests
- proposed: Expose native build lock waits to the box queue
- proposed: Isolate native Release Ninja metadata from a failed restat
- proposed: Set look-dev timeouts for the costliest individual states
- proposed: Make native RecipeSimd generation checks self-consistent
- proposed: Serialize concurrent builds over a shared FetchContent build directory
- proposed: Avoid Dozen device loss during memory-pressured look-dev captures
- proposed: Provide a CodeGraph index for VIXEN worktrees
- proposed: Resolve cmd.exe in queued native test commands
- proposed: Preserve staged DLLs after vcvars in native CTest
- proposed: Declare the editor script as R6 CTest setup
- proposed: Let native R6 launch the configured editor target
- proposed: Run the mutex census from the native VIXEN build
