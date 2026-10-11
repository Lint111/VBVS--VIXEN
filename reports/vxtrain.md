# Lane vxtrain report

Checkpoint: **2026-10-11 11:24:54 UTC** (14:24:54 IDT), before the three-hour limit. The merged train and completed fixes are committed; native, look-dev, and three render witnesses remain open.

## LANDABLE NOW

The starting wave base was `5dd78d8675160314f5831e4b3d10b3fdefd8c164`. Merge commits, in requested order:

| Lane | Merge commit | Landed tip |
|---|---|---|
| gpuyield | `1e26ddec` | `e7d8a198` |
| lights0 | `8e31dbb1` | `e6555dcf` |
| shellasync0 | `90ec57b4` | `d3b2cf9f` |

There were no gpuyield/lights0 conflicts. In shellasync0's include conflict in `BodyOctreeSceneNode.cpp`, both `EmitterPower.h` and `Recipe/RecipeBaker.h` were retained. The overlapping MSVC repairs were consolidated: one `(std::max)(...)` macro-safe spelling remains, and shellasync0's GLSL rename `flat` to `flatLookup` remains. The required merge commits have the requested `--no-ff` shape and Claude co-author trailer.

Follow-on commits before the checkpoint change are `1d05ed93` (publish `EmitterRootPowers` with the async shell generation), `4e0f4371` (emit SDI integer push vectors with their reflected types), and `7d4540d1` (include Visual Studio Build Tools in `build.bat` discovery). The checkpoint commit adds a graph-free shell producer fallback, this report, and the SPT inbox.

Witnesses:

- Queued Linux build passed with all 26 generation checks (22 base checks plus lights0's four): log `1791709858-build-vxtrain:build-corrected-sdi.log`.
- `sdi_tool merge-variants shaders/sdi-variants.json --check` passed after its generator fix: all merged SDI headers are current.
- Focused gpuyield tests: 47/47. Emitter, SDI, and async power publication tests: 17/17. Shell focused tests: 3/3, including the four-frame publish, eight-frame post-flip render, and worker-count byte parity. Logs: `1791710012-test-vxtrain:gpuyield-41.log`, `1791710022-test-vxtrain:emitter-and-sdi-tests.log`, `1791711003-gpu-vxtrain:shell-focused-dozen.log`, and `1791711359-gpu-vxtrain:async-emitter-power-dozen.log`.
- After the graph-free fallback, 10 of 13 previously failing RenderGraph cases pass on fresh rebuilt binaries. They include recipe-pool rendering, mip residency, baked/virtual parity, editor documents, tier crossing, recipe authoring, and AppFlow toggle/undo. Build log: `1791717287-build-vxtrain:standalone-shell-fallback-build2.log`; focused log: `1791717616-gpu-vxtrain:standalone-shell-fallback-focused2.log`.

## STOPs

- **RenderGraph/SVO suite is not green.** The full run completed all 2,171 cases with 14 failures; opcode-94 `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` is the accepted red. Thirteen other tests initially failed because graph-free `BodyOctreeSceneNode` callers could not schedule shell generation. The fallback fixed ten; these three still produce zero shader hits after logging both `[ShellDerive]` and `[Rematerialize]`: `BodyInstanceRayMarchRenderTest.RenderStoredSdfBodiesNoHoles`, `RenderStoredSdfMultiChannel`, and `RenderRecipeBakedBody`. All three also fail on Lavapipe, so this is not isolated to Dozen or GPU queue sharing. The full-suite log is `1791716002-gpu-vxtrain:rendergraph-svo-full.log`; the Lavapipe diagnostic is `1791717817-gpu-vxtrain:body-sdf-lavapipe-diagnostic.log`. They remain **unclassified**, not accepted baseline reds. A full-suite rerun after the fallback did not fit before the checkpoint.
- **Native MSVC build and native R6 are unverified.** The queued `build.bat all vxtrain-native` reached the pinned kernel snapshot `c3fb3847979e3f9eef04783510c42b70d7058ae7`, then hung in Windows CMake `file(LOCK)` before target compilation. The WSL checkout had no stale snapshot/archive; five-second probes hung for existing and fresh lock files and for both `\\wsl$` and `\\wsl.localhost` paths. The queue stopped the build at its idle hard limit; log `1791712130-build-vxtrain:native-msvc-preset.log`. Recovery A used the documented `build.bat` entry through the live queue and checked the mapped path, toolchain, cache, and pin overrides. Recovery B was not applicable: no generated snapshot existed to regenerate, and the lock probe itself hung. Recovery C is unclassified: there is no native target result or matching clean-wave result. The required external action is a supported Windows CMake lock/cache route for WSL worktrees. Native R6 must follow that repair.
- **Fresh look-dev comparison is incomplete and not byte-identical on Dozen.** Midday and late-afternoon four-angle captures passed each test's internal repeat check, but all 8 captures differed from the saved wave-base references. Overcast and night-service were not run; no native capture could be made while the native build is blocked. The Dozen mismatch is unclassified as a renderer/source delta. Compare all four states on the repaired native path before calling the look-dev witness green.

## SPT DISPOSITION

No tracked SPT tasks were attached. Proposal `fa6fb7067691` covers the graph-free shell materialization contract. Other incidental workflow gaps found during the lane are recorded in `.spt-proposals/vxtrain.jsonl`.

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
