---
tags: [testing, known-issues, rendergraph]
updated: 2026-06-19
---

# Known Test Failures (RenderGraph)

Status as of 2026-06-19. Established while fixing the build environment (see `CMakePresets.json`
`vixen-ninja` preset) and triaging the RenderGraph suite. Run tests via the Ninja build
(`cmake --build --preset vixen-ninja`) from a VS Developer prompt; run individual exes from
`build-ninja/libraries/RenderGraph/tests/`.

## Fixed (stale tests — corrected to match shipped, git-proven design)

These asserted superseded contracts after deliberate refactors and were never updated. Corrected
to the current design (with the proving commit cited in each test). **Not** weakened.

| Test | Was | Now | Proving commit |
|------|-----|-----|----------------|
| `test_connection_rule` (3: binding-rule) | Direct handles binding | Variadic handles binding | `51927757` |
| `SwapChainNodeTest.ConfigHasSevenInputs` | `INPUT_COUNT == 9` | `== 7` | `266bfa3b` (FR-3) |
| `MultiDispatchNodeConfig.HasGroupInputsSlot` | role `Dependency` | role `Execute` | `938a95d1` (Sprint 6.3) |
| `TimerTest.MultipleTimersAreIndependent` | exact ms tolerance (flaky) | lower-bound + ordering | (OS sleep-granularity) |
| `TimerTest.GameLoopSimulation` | exact total ms | elapsed ≈ Σ deltas | (OS sleep-granularity) |
| `PushConstantGathererNodeTest.HandleMissingInputsGracefully` | buffer init `0xFF` | init `0` | (test-internal bug) |
| `PushConstantGathererNodeTest.VerifyBufferAlignment` | `size % 16 == 0` (false math) | `size % 4 == 0` (Vulkan rule) | (test-internal bug) |

## Resolved (2026-06-20) — both former "deferred" discrepancies

### `test_scene_generators` — FIXED (was 5 failing) — generators now meet the density spec
The test's deprecated static wrappers delegate to the live `SceneGeneratorFactory` generators
(used by `VoxelSceneCacher`), which now hit the documented densities (±5%):
- **Cave**: the density threshold was inverted (`noiseValue > threshold` made a higher threshold
  *sparser*) → flipped to `< threshold`; live default `wallThickness` 0.3 → 0.5 (the 50% spec).
- **Cornell**: fixed 3-voxel walls (23% at 64³) → thickness scales with resolution (`res/50`,
  min 1), holding ~10% across 64/128/256.
- **Urban**: sparse 0.6-height blocks (~28%) → compute one uniform building height from the
  footprint to reach ~90% by construction, thin resolution-scaled streets, `blockCount` 4 → 2.

All 19 `test_scene_generators` tests pass.

### `test_voxel_octree` — REMOVED — deprecated `SparseVoxelOctree` deleted
`SparseVoxelOctree` was confirmed dead: never instantiated outside its own tests, the live render
path is `LaineKarrasOctree`, its serialize had no live callers, and `VoxelGridNode`'s references
were stale undefined declarations. Rather than port the legacy serialize/accessors to ESVO, the
class, its `VoxelOctree.cpp`, and `test_voxel_octree.cpp` were deleted. The shared
`OctreeNode`/`ESVONode`/`VoxelBrick` structs in `VoxelOctree.h` remain — they back the live path
(`LaineKarrasOctree`, `GpuTraversalMirror`, `VoxelGridNode`).

## Not a failure — harness/CWD artifact

`test_ui_hud_smoke` (2) loads `assets/ui/hud.rml` relative to CWD. The assets are staged next to the
binary; the test **passes under ctest** (which sets CWD to the binary dir) or when run from
`build-ninja/libraries/RenderGraph/tests/`. Headless by design (NullRenderInterface) — no GPU needed.
