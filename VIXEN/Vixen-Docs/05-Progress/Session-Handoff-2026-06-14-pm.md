---
title: Session Handoff — 2026-06-14 (pm) → next session
aliases: [Handoff 2026-06-14 pm]
tags: [handoff, progress, session]
created: 2026-06-14
---

# Session Handoff — 2026-06-14 (pm) → next session

Supersedes [[Session-Handoff-2026-06-14]]. **Live plan = [[Maturation-Backlog-2026-06]]** (all AR
statuses below are recorded there in detail). `origin/main` at handoff = **`53df2d4c`**. Working tree
clean; everything below is merged + pushed.

## Done this session (all merged + pushed)

| Item | Outcome |
|---|---|
| **AR#8** de-singletonize `MainCacher` + `CapabilityGraph` | `MainCacher::Instance()` removed (host-owned, `EngineContext` owns one when none injected; back-pointer in `CacherBase`/`DeviceRegistry`; `GetMainCacher()` fallback gone; fixed a latent exit-time UAF). `CapabilityGraph`'s 4 static availability vectors → per-graph instance state (instance-level self-populated from the loader). **ProfilerSystem deliberately NOT done** — benchmark-only, not a multi-instance blocker. |
| **AR#9** ExternalWindowNode | **Evaluated + parked.** `SwapChainNode` consumes a `GLFWwindow*` and self-creates the surface (GLFW-coupled); a real host-owned window needs surface-injection surgery; no consumer needs it (UNDERTOW embeds its UI *inside* VIXEN's window). |
| **AR#12** embedding docs | [[Hosting-VIXEN]] (`06-Embedding/`): `find_package(VIXEN)` → `EngineContext` → own-the-loop → shutdown. |
| **AR#13** version + supported headers | Generated `<VixenVersion.h>` (single source = root `project(VERSION 0.1.0)`); documented supported public-header set. Umbrella `<Vixen.h>` deferred. |
| **AR#3/#4** namespace cleanup | Legacy all-caps `VIXEN::RenderGraph` namespace killed → `Vixen::SVO` (SceneGenerator + VoxelOctree + VoxelTraversal + consumers; ~12 files). |
| voxel/SVO tests | `SVOBuilderTest.EmptyMesh` fixed (empty mesh → empty octree). |

## The main unfinished thing: voxel/SVO unit-test debt (16 failures remain)

These are **long-standing, pre-existing** failures (predate this session; unchanged since the project
restructure `6429262d`). The **live render path works** (the app renders voxels per-frame). They were
triaged + root-caused this session but need **design/intent decisions** to finish correctly. Do NOT
weaken/disable tests to go green (engineering rule) — each needs a real fix or a justified intent call.

### Scene generators (5) — generators don't meet their **documented** density spec
SceneGenerator.h's own comment specs: Cornell ~10%, Cityscape ~80–95%. Actual:
- **Urban_64/128/256:** `CityscapeSceneGenerator` produces **~28%**, spec says **80–95%**.
- **CornellBox_64:** **23%** vs ~10% (walls don't scale with resolution; 128³/256³ pass at ~10%).
- **Cave_CustomDensity:** `CaveSystemGenerator`'s 3rd param is named `densityThreshold` but used as
  `wallThickness` (inverse effect: 0.3→93.8%, 0.7→8.9%); the test expects real density control.
- **DECISION NEEDED:** are the documented density targets still intended (→ fix/tune the generators,
  changes rendered scenes) or did the generators get a deliberate redesign (→ update spec+tests)?

### `test_svo_builder` — 1 of 2 left (`GeometricError`)
The mesh→SVO build (`SVOBuilder::shouldTerminate`) is **attribute(color)-driven**;
`geometryErrorThreshold` is only consulted when `enableContours` is true, so a uniform-color cube
terminates at the root (`totalVoxels==1`). Left asserting intended behaviour with a documenting
comment. **DECISION NEEDED:** should geometric/surface error drive subdivision standalone? (Note: the
live app uses `buildFromVoxelGrid`, not `build(mesh)` — mesh path is secondary/WIP.)

### `test_voxel_octree` (7) — **legacy** `SparseVoxelOctree`
`SparseVoxelOctree::BuildFromGrid` returns 0 nodes. This type is tagged "Legacy - will be removed";
the live path uses the modern `Vixen::SVO::Octree`. **DECISION NEEDED:** remove the deprecated code +
its 7 tests, or keep + fix?

### Not yet root-caused (2 + 2)
- `test_voxel_injection` (2): `SparseVoxels`, `MultipleVoxelsSpread` — ray-injection hits.
- `test_cornell_box` (2): `FloorHit_FromAbove`, `LeftWallHit_Red` — ray-traversal hit coords.

## Gotchas

- **Flaky build failure:** `gtest_discover_tests` has a **5 s `TEST_DISCOVERY_TIMEOUT`**; under
  parallel-build load a Vulkan-init test (e.g. `test_group_dispatch`) can time out → spurious
  `MSB3073` build failure. The exe runs `--gtest_list_tests` fine directly; **re-running the build
  clears it**. Not a real failure.
- **Build/run:** `"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel 16`;
  app via `cd binaries && timeout 20 ./VIXEN.exe` (exit 124/143 = timeout-killed = OK). `GRAPH_LOG_*`
  + `std::cout` reach captured stdout; `NODE_LOG_*` do not.
- **WSL ⇄ cmake.exe paths** ([[wsl-cmake-windows-paths]]); **rtk** mangles `git diff/show/reset` — use
  `rtk proxy git …` for ground truth. Git root is the **parent** of `VIXEN/` — stage with cwd-relative
  paths (`Vixen-Docs/...`) or `git add -u`, not `VIXEN/...`.
- CRLF: RenderGraph/CashSystem files are CRLF in the working tree; `sed` `$`-anchors miss the trailing
  `\r` (drop the `$` anchor for line-edits on those).
- `project-rules` skill must be invoked first thing each turn (UserPromptSubmit hook enforces it).
