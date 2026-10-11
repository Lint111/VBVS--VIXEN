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

The fresh wave-base suite completed all 2,124 selected cases. Its summary was `99% tests passed, 1 tests failed out of 2123`; the failing-name set was exactly `{RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes}` (opcode 94, the accepted red). All three zero-hit tests passed on base. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791736360-gpu-wave-base-rendergraph-svo-suite.log`.

At final tip `0b731312` (product tree `853b659c`, report-only commit on top), the identical serial command completed all 2,171 selected cases, including `vixen_multiinstance_cache_gate`, which passed in 634.58 seconds. CTest's summary line was `99% tests passed, 1 tests failed out of 2170`; its failing-name set was exactly `{RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes}`. The base and final sets are equal, so no new failing name requires attribution or a product fix. Log: `/home/liory/.local/state/undertow/undertow-box-logs/1791739674-gpu-final-tip-rendergraph-svo-suite-complete.log`; total CTest time was 1,169.11 seconds.

The wave-base native R6 and base night-service comparisons are outside this gate. The wave base cannot complete the native build because generated `RecipeSimd.g.hpp` triggers C4457 under `/WX` (the gpuyield native fix resolves this), and the base has no R6 test or `[GPUYIELD]` instrumentation. The base night-service capture lost its DZN device; no base set was produced.

### Look-dev captures

All four 500x500 midday PNGs are byte-identical across base → gpuyield → lights0 → shellasync0 → fixed/current tip: every comparison reports zero differing bytes and zero differing pixels. The captures and `tools/compare-capture-pixels.py` results are documented in [the visual attribution record](vxtrain-visual/attribution.md).

Current-tip overcast and night-service each have four saved first-pass angles. Overcast's internal repeat completed byte-identically for all angles; night-service's repeat timed out after two repeat angles, which match. The three available base overcast angles match the tip by SHA-256; base angle 3 is missing after DZN device loss. No full base night-service set was captured. There are no observed unexplained pixel changes; midday is fully attributed at each merge boundary.

## STOPs

None. The completed final-tip suite has only the accepted opcode-94 red, matching the wave-base failing-name set. The requested base native R6 and base night-service comparisons are out of scope; their limitations are recorded above.

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
