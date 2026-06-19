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

## Deferred — REAL discrepancies (do NOT weaken to go green)

### `test_scene_generators` — 5 failing — generator does not meet documented density spec
The README/research test-matrix defines fixed scene densities (Cornell 10%, Cave 50%, Urban 90%),
and `test_scene_generators` asserts them (±5%). The generators do not produce them:
- **Urban 64/128/256**: target 90%, produces **~28%** (block-footprint prisms + streets can't reach 90% by construction).
- **Cave_CustomDensity**: density parameter is **inverted** (higher param → lower density).
- **CornellBox_64**: fixed 3-voxel walls don't scale, so 64³ reads ~23% vs the 10% target (128³/256³ pass).

These are a **generator/spec gap, not stale tests** — weakening the asserts to the produced values
would hide the defect and corrupt the research benchmark densities. **Decision needed:** fix the
generators to meet the documented densities, or formally revise the research spec. (Also flagged in
`05-Progress/Session-Handoff-2026-06-14-pm.md`.)

### `test_voxel_octree` — 7 failing — deprecated class, ESVO-accessor staleness
`SparseVoxelOctree::GetNodeCount()` / `GetMemoryUsage()` / `SerializeToBuffer()` read the legacy
`nodes_` vector, but the build default flipped to ESVO (`esvoNodes_`), so they return 0 even though
the tree built. `SparseVoxelOctree` is tagged *"Legacy — will be removed"*; the **live render path
uses `LaineKarrasOctree`**, which does not consume these accessors. **Decision needed:** make the
accessors ESVO-aware, or delete the deprecated class + its 7 tests (it is still `#include`d by
`VoxelGridNode`, so removal requires de-referencing there).

## Not a failure — harness/CWD artifact

`test_ui_hud_smoke` (2) loads `assets/ui/hud.rml` relative to CWD. The assets are staged next to the
binary; the test **passes under ctest** (which sets CWD to the binary dir) or when run from
`build-ninja/libraries/RenderGraph/tests/`. Headless by design (NullRenderInterface) — no GPU needed.
