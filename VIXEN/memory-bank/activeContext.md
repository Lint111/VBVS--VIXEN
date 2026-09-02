> **✅ 2026-09-02 — Photon world-cell cache C0/C1 implemented in `lane-photon1` (not merged/pushed).**
> Photon behavior is node-owned: `PhotonCellTableNode` owns the fixed 131072×64B SSBO and
> diagnostics, `PhotonCellParamsConfigNode` owns the persistent 4-slot/48B params ring and
> generation/config emission, and `PhotonCellDepositNode`/`PhotonCellFoldNode`/`PhotonCellClearNode`
> own shader registration, metadata, dispatch, and clear passes. `BuildRenderGraph.cpp` and
> `VulkanGraphApplication.cpp` retain composition-only instantiation/wiring and optional hooks.
> The CPU/GLSL twin follows `docs/design/2026-09-02-photon-cells.md`: level-0 size 0.5wu, keyLo
> witness `0xC3EFFC3E`, fixed-point scale 1024, clamp 256, EWMA α=0.25, 20-bit generations,
> max age 1024, six zeroed SH reservation words, and default-off `VIXEN_PHOTON_CELLS`. Writer #1
> deposits post-shadow-wave exitant diffuse radiance from existing HitRecord/lighting/shadow data;
> writer #2 (staged/proxy) remains the documented later seam. Dedicated node targets passed; the
> witness build and full regression/codegen gates were queued for fresh verification at handoff.

> **✅ 2026-09-01 — Current engine status (reconciled on `wave/authoring-convergence`).**
> Raster-proxy hybrid Slice A is landed; B1 shipped and is DEFAULT-ON after the 2026-08-03 native
> gate (`b3c09d32`, 60% iteration cut; `14e835ee` default-on). B2 is the next rescope and remains
> pending the controller's seam ruling; it is not claimed as shipped (`undertow-ctl/docs/in-progress.md:3378-3407`).
> The shadow-mask word split is landed (`178b838b`; `VIXEN/shaders/SceneBindings.glsl:140-200`), so
> camera-visibility cull bits no longer suppress shadow traversal.
> Voxel authoring Inc1 is integrated in the current tree (`a72ffba2`; `VIXEN/application/editor/CMakeLists.txt:1-32`,
> `VIXEN/application/editor/include/EditorDocumentModel.h:16-74`),
> and compiler-emitted SIMD4 materialization is landed (`e37c9dab`; `VIXEN/libraries/SVO/include/Recipe/generated/RecipeSimd.g.hpp:1-3,1254-1268`).
> The current direction records also include the data-movement audit and multicore dispatch-unification
> direction (`cafa30dc`, `29f2feed`); multicore implementation still has eight owner decisions pending.
> The banners below are retained as historical context.

> **⚠️ 2026-08-03 — Raster-proxy hybrid SLICE A COMPLETE (not merged/pushed; live tree = undertow submodule).**
> Shell derive now emits the raster-proxy artifact + the dirty path has a real producer. Built, TDD (red→green each):
> (1) `ShellProxyAabb` (32B, template-local [0,1]³ = traceBounds convention) + `ShellDeriveResult::proxyAabbs`,
> emitted in `DeriveShell`'s compact loop, per-octree through `DeriveShellPool` (`libraries/SVO/include/ShellDerive.h`);
> (2) `ApplyBrickSdfEdit` — THE value-edit entry point; `BodyOctreeSceneNode::EditSourceBrickSdf` wraps it +
> marks `dirtyBricks_` in one call (first real producer for the §C revalidate path; membership edits remain
> full-rederive by contract, and proxies are PROVEN byte-invariant under value edits);
> (3) `proxyAabbBuffer_[2]` double-buffered upload in `UploadShellSlot` (same distinct-VkBuffer hazard-keying
> pattern; no graph output slot yet — slice B2 binds it). Tests: test_shell_derive 11/11 (+3 new),
> shell_octree 11/11, shell_octree_gpu 9/9, shell_revalidate_node 2/2, criticalnodes_infra2 128/128 — all
> freshness-verified after TWO vacuous-pass traps (batched ninja died on unknown target `RenderGraph` →
> stale-binary "passes"; and `build/wsl/libraries/RenderGraph/tests/test_body_octree_lifetime` is an ORPHAN
> binary of a retired target — lifetime tests live in `test_rendergraph_criticalnodes_infra2` now; lib target
> is `RenderGraphCore`). Design: undertow `docs/plans/2026-08-03-raster-proxy-hybrid-seam.md`. **Next:** slice B1
> (march depth image → HiZ → per-instance cull writing InstanceSkipMaskBuffer), then B2 raster pre-pass binds the
> proxy buffer. EditSourceBrickSdf wrapper is 4-line glue over two tested halves — no dedicated device test (flagged).
> The 2026-07-03 banner below is prior context.

> **⚠️ 2026-07-03 — Voxel authoring editor Inc1 (VoxelDocument format + `vixen_editor` app) COMPLETE, all 5**
> **milestones (M1–M5) Opus-approved. NOT yet merged/pushed** (isolated worktrees, awaiting a separate
> integration step). VIXEN worktree `.claude/worktrees/voxel-authoring-inc1`, branch
> `feat/voxel-authoring-inc1`, HEAD `743c60aa`+docs; Yeroket worktree
> `Yeroket-Fantasy/.worktrees/voxeldoc-inc1`, branch `feat/voxel-document-inc1`, HEAD `16a007a2`.
> Built: VDC1 layered-document format (canonical C# `Yeroket.GraphFramework.VM.VoxelDocument`, generated
> `VoxelDocument.g.h` + `voxel_document.py`/`sdf_op_codes.py`, cross-language golden byte-identical) +
> `Vixen::SVO::FlattenVoxelDocument` (document → VRC1 blob, consumed unchanged by the existing
> RecipeRegistry→pool→render path) + new `application/editor` target `vixen_editor` (load/render/toggle/save
> a `.vxd`, RmlUi layer-list panel, dirty-flag re-flatten on toggle, live gate + windowed smoke both passed).
> Design/plan: `Vixen-Docs/01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07.md` (+ `-Plan-…`, has a
> "Next steps" section with Inc2 candidates). How-to: `Vixen-Docs/02-Implementation/Voxel-Document-Authoring.md`.
> **Next:** Inc2 — per the umbrella design's own sequencing, Slice 3 (drawn layers/brushes, both editor-sculpt
> and runtime dig/build) is next, then Slice 4 (query-aware config) and Slice 5 (Blender addon). Also worth
> resolving before Inc2 builds further on the current bake convention: `BakeSdfWorld` has no center-offset
> (object-centered documents only land correctly in the grid's positive-octant corner; every render-gate test
> hand-works around it today).
> The 2026-06-24 banner below is prior context.

> **⚠️ 2026-06-24 — SDF body rendering Inc1+Inc2+Inc3 + the Inc3 hole-fix DONE, merged + PUSHED to `origin/main` (`9c8d549e`).**
> Bodies render as smooth per-voxel **multi-channel Stored SDF** (sdf+color+roughness) via a generic SoA channel
> pool the shader reads by semantic; providers = Procedural + Stored + (next) Materialization. Plan/Progress:
> `Vixen-Docs/01-Architecture/Voxel-MultiChannel-Stored-Inc-2026-06-Plan.md` (+ `…-Design.md`, content-contract
> `Voxel-Content-Format-Contract-Design-2026-06.md`). **Inc3 hole-fix:** brick-aligned flecks root-caused to
> `GaiaVoxelWorld::querySolidVoxels()`'s `Density>0` filter silently dropping a Stored-SDF body's fully-interior
> bricks (Density = signed distance, negative inside) → fixed by occupancy-based brick selection
> (`queryOccupiedVoxels` + `LaineKarrasOctree::setSignedDistanceField`); binary/Cornell path byte-unchanged.
> Verified on the lavapipe offscreen render gate (smooth+displaced clean) + user real-GPU + MSVC no-regression.
> **GOTCHA (Materialization / any Density-as-field body):** "solid = Density>0" is a BINARY-voxel assumption —
> field-valued bodies MUST bin bricks by occupancy. Build: `vixen-wsl` preset (lavapipe) for SDF render tests;
> MSVC `cmd.exe /c C:\cpp\_wt_build.bat VIXEN` for the app (launch via `run_stored_demo.bat`). Next: Materialization.
> The 2026-06-21 banner below is prior context.

> **⚠️ 2026-06-21 — loose ends cleared + consumer/WSL branch MERGED, all PUSHED to `origin/main` (`7d1de593`).**
> Live handoff: `Vixen-Docs/05-Progress/Session-Handoff-2026-06-21.md`.
> This session: (1) benchmark `idOutputImage` binding-9 VUID **fixed** (PickIdTargetNode in
> BenchmarkGraphFactory; verified 0 validation errors) — no longer a known issue; (2) dead
> `StructSpreaderNode`/`SwapChainStructSpreaderNode` **deleted**; (3) scene generators **fixed to
> spec** (Cave inversion, Cornell wall-scaling, Urban 90% redesign) — `test_scene_generators` 19/19;
> (4) deprecated **`SparseVoxelOctree` deleted** (live octree path = `LaineKarrasOctree`; shared
> structs kept); (5) **merged `claude/wsl-build-portability`** (36 commits — entity/body octree
> rendering, UI/HUD selection, typed accumulation-gather [closes the 2026-06-15 gap], validation
> wiring), then **fixed its WSL→MSVC build breakage** (windows.h `far/near/min/max` macros; gated
> lavapipe/UNDERTOW-consumer tests). Build GREEN; node-registration/generators/shell-octree/gpu-parity
> all pass. **Build with the `vixen-ninja` preset** (`cmd.exe /c _ninja_preset_build.bat`), NOT
> `cmake -B build`; test exes live under `build-ninja/` at the **repo root**.
> Next frontier (Maturation backlog): P3 auto-sync FrameGraph epic [AR#21] → multi-view.
> Everything below is stale (Sprint 6, Jan 2026).

---

# Active Context - Sprint 6 Phase 3

**Last Updated:** 2026-01-09
**Branch:** `production/sprint-6-timeline-foundation`
**Status:** Build PASSING | Sprint 6.3 COMPLETE | Accumulation semantics enforced

---

## Current Position

**Sprint 5: CashSystem Robustness** - COMPLETE (104h)
**Sprint 5.5: Pre-Allocation Hardening** - COMPLETE (16h)
**Sprint 6.0.1: Unified Connection System** - COMPLETE (118h)
**Sprint 6.0.2: Accumulation Slot Refactor** - COMPLETE (3h)
**Sprint 6.1: MultiDispatchNode** - COMPLETE (56h)
**Sprint 6.2: TaskQueue System** - COMPLETE (72h)
**Sprint 6.3: Timeline Capacity System** - COMPLETE (All 7 phases + additional fixes)

### Just Completed (2026-01-09)

#### Sprint 6.3: Timeline Capacity System - COMPLETE

**Key Achievement:** Adaptive scheduling infrastructure + accumulation semantics enforcement.

**Phases Completed:**
- Phase 0: GPUQueryManager Infrastructure
- Phase 1: TimelineCapacityTracker Foundation
- Phase 2: TaskQueue Integration
- Phase 3: Prediction & Calibration
- Phase 4: Event-Driven Architecture
- Phase 5: System Decoupling Analysis
- Phase 6: Frame Lifecycle Decoupling
- Phase 7: Persistence & Polish (CalibrationStore)

**Additional Fixes (2026-01-09):**
- Fixed SEH exception in accumulation + field extraction tests
- Added `skipDependencyRegistration` flag to ConnectionContext
- **Accumulation Slot Semantics:**
  - Removed Role parameter from ACCUMULATION_INPUT_SLOT macros
  - Accumulation slots now ALWAYS use SlotRole::Execute
  - Result lifetime is Transient (rebuilt each frame)
  - Source lifetime can be Persistent (enables field extraction)

**Test Results:** 222+ tests passing (TimelineCapacityTracker + connection tests)

---

### Previously Completed (2026-01-07)

#### Sprint 6.2: TaskQueue System (72h) - COMPLETE

**Key Achievement:** Budget-aware priority task queue for timeline execution.

| Task | Hours | Status |
|------|-------|--------|
| #339: TaskQueue Template | 16h | DONE |
| #340: API Documentation | 8h | DONE |
| #341: MultiDispatchNode Integration | 16h | DONE |
| #342: Budget-Aware Dequeue | 16h | DONE |
| #343: Stress Tests | 16h | DONE |

**Technical Achievements:**
- Priority-based task scheduling with stable sort
- Budget enforcement (strict/lenient modes)
- MultiDispatchNode integration (backward compatible)
- 43 tests total (28 unit + 15 integration)
- Complete API documentation

---

### Previously Completed (2026-01-06)

#### Sprint 6.0.1: Unified Connection System (118h) - COMPLETE

**Key Achievement:** Single `Connect()` API for all connection types.

| Phase | Tasks | Hours | Status |
|-------|-------|-------|--------|
| Phase 1: Infrastructure | 6.0.1.1-6.0.1.9 | 68h | DONE |
| Phase 2: Unified API | 6.0.1.10-6.0.1.19 | 50h | DONE |

**Commits:**
- `c3ad535` - Unified Connection System infrastructure
- `e7d79a2` - VariadicConnectionRule + UnifiedConnect API
- `e37d5bb` - Complete unified Connect() API with modifiers

**Technical Achievements:**
- Unified `Connect()` API (Direct, Variadic, Accumulation)
- 3-phase ConnectionModifier pipeline
- Fluent `ConnectionMeta{}.With<Modifier>()` API
- **NEW:** Variadic modifier API (eliminates `ConnectionMeta{}` boilerplate)
- 102 tests passing

#### Sprint 6.0.2: Accumulation Slot Refactor (3h) - COMPLETE

**Commit:** `73c8d36` - Sprint 6.0.1 + 6.0.2 complete

**Problem Solved:** Type system lie (element type declared, container type returned)

**Solution:**
- Added `SlotStorageStrategy` enum (Value/Reference/Span)
- Created `ACCUMULATION_INPUT_SLOT_V2` macro with container types
- Updated `TypedNodeInstance::In()` for V1/V2 dispatch via `Iterable` concept
- Migrated `BoolOpNode` to V2 API

**Key Changes:**
```cpp
// Old V1 (element type - type system lie)
ACCUMULATION_INPUT_SLOT(INPUTS, bool, 1, ...)  // Type = bool, wraps at runtime

// New V2 (container type - honest)
ACCUMULATION_INPUT_SLOT_V2(INPUTS, std::vector<bool>, bool, 1, ..., SlotStorageStrategy::Value)
// Type = std::vector<bool>, no runtime magic
```

**Status:** Build PASSING, tests passing, backward compatible

---

## Sprint 6.3: Timeline Capacity Tracker - COMPLETE

**Goal:** Real-time budget monitoring and capacity planning
**Tasks:** Task #344 (parent) + phases
**Status:** ✅ ALL PHASES COMPLETE (2026-01-09)

**Subtask Documentation:**
- SPRINT-6-3-QUICK-REFERENCE.md (quick lookup guide)
- .hacknplan-sprint-6-3-subtasks.md (detailed specs for HacknPlan)
- .hacknplan-sprint-6-3-import.json (batch import format)
- SPRINT-6-3-CREATION-REPORT.md (complete handoff docs)

### Architecture Review Complete (2026-01-07)

**Reviewer:** architecture-critic agent (a15e365)
**Decision:** GO with modifications
**Key Changes:**
- Compose GPUPerformanceLogger (eliminate duplication, -8h implementation)
- Create GPUQueryManager for query coordination (+12h new infrastructure)
- Use member pointer pattern in MultiDispatchNode (not context accessor)
- Include damped hysteresis in Phase 1 (±10% max, 5% deadband)
- 6 phases total (was 3 phases)

### Implementation Phases (100h)

**Phase 0: Query Infrastructure (12h) - NEW**
1. GPUQueryManager Core (8h) - Shared query pool coordinator
2. Integration with GPUPerformanceLogger + ProfilerSystem (4h)

**Phase 1: Core Measurement (24h) - REVISED**
1. TimelineCapacityTracker foundation with damped hysteresis (8h)
2. Measurement recording (composes GPUPerformanceLogger) (6h)
3. History & statistics (6h)
4. Damped hysteresis system (±10%, 5% deadband) (4h)

**Phase 2: TaskQueue Integration (16h)**
1. TaskQueue extensions (SetCapacityTracker, RecordActualCost) (6h)
2. MultiDispatchNode integration with member pointer (10h)

**Phase 3: Feedback Loop (16h)**
1. Prediction error tracking (8h)
2. Estimate adjustment (exponential moving average) (8h)

**Phase 4: Adaptive Scheduling (20h)**
1. Capacity queries (CanScheduleMoreWork, GetRecommendedTaskCount) (6h)
2. RenderGraph lifecycle integration (8h)
3. Adaptive task scheduling (6h)

**Phase 5: Documentation & Polish (12h)**
1. API documentation (TimelineCapacityTracker.md) (4h)
2. Update related docs (MultiDispatchNode.md, etc.) (4h)
3. Example usage (BenchmarkGraphFactory) (4h)

### Prerequisites ✅
- ✅ Accumulation slots (V2 API complete)
- ✅ Storage strategies (Value/Reference/Span)
- ✅ Connection modifiers (3-phase pipeline)
- ✅ Variadic API (streamlined syntax)

### Key Design Decisions
1. **Group Key Extraction:** Member pointer (like FieldExtraction)
2. **Dispatch Ordering:** Sequential first, parallel as optimization
3. **Resource Sharing:** Reference via Persistent slots

### HacknPlan Tasks

| Task ID | Task | Hours Est | Hours Logged | Status |
|---------|------|-----------|--------------|--------|
| #313 | DispatchPass Structure | 8h | 1.5h | ✅ COMPLETE |
| #312 | MultiDispatchNode Core | 16h | 12.5h | ✅ COMPLETE |
| #311 | Integration Tests | 16h | 0h | Planned |
| #310 | Documentation & Examples | 8h | 0h | Planned |
| #314 | Pipeline Statistics | 8h | 0h | Planned |

### Completed (2026-01-06)

**Task #313: DispatchPass Structure (1.5h)** - COMPLETE
- Added `groupId` field to DispatchPass for group-based dispatch
- File: [DispatchPass.h:56-58](libraries/RenderGraph/include/Data/DispatchPass.h#L56-L58)

**Task #312: MultiDispatchNode Core (12.5h)** - COMPLETE

**Phase 1: Infrastructure (6h)**
- GROUP_INPUTS accumulation slot (V2 API with Value storage)
- DispatchPass type registration in compile-time resource system
- GroupKeyModifier for partitioning by member pointer
- Files: GroupKeyModifier.h (new), MultiDispatchNodeConfig.h (+29), CompileTimeResourceSystem.h (+5)
- Commit: `2b1b2d1`

**Phase 2: Group Dispatch Implementation (6.5h)**
- CompileImpl: Reads GROUP_INPUTS, partitions by groupId into groupedDispatches_ map
- ExecuteImpl: Per-group dispatch with auto-barriers between/within groups
- Backward compatible: QueueDispatch() API still works (legacy mode)
- Files: MultiDispatchNode.h/cpp (+96 lines)
- Commit: `b509ef6`

---

## Architecture (Post-Sprint 6.0.1)

### Connection System (Complete)

```
Connect() API
    +-- Type detection via C++20 concepts
    |   +-- SlotReference<T> -> Direct/Accumulation
    |   +-- BindingReference<T> -> Variadic
    +-- ConnectionRuleRegistry
    |   +-- DirectConnectionRule
    |   +-- VariadicConnectionRule
    |   +-- AccumulationConnectionRule
    +-- ConnectionPipeline (3-phase)
    |   +-- PreValidation (modifiers)
    |   +-- Rule Validation + Resolve
    |   +-- PostResolve (modifiers)

Modifiers (stackable):
    +-- AccumulationSortConfig - Explicit ordering
    +-- SlotRoleModifier - Role override
    +-- FieldExtractionModifier - Struct field access
    +-- DebugTagModifier - Debug metadata

Variadic API (NEW):
    Connect(src, slot, tgt, slot, mod1, mod2, ...)
    -> 80% less boilerplate
```

### Files Added (Sprint 6.0.1)

- `Connection/ConnectionTypes.h` - Core type definitions
- `Connection/ConnectionRule.h` - Rule base class
- `Connection/ConnectionRuleRegistry.h` - Registry
- `Connection/ConnectionModifier.h` - Modifier interface
- `Connection/ConnectionPipeline.h` - 3-phase pipeline
- `Connection/Rules/DirectConnectionRule.h`
- `Connection/Rules/VariadicConnectionRule.h`
- `Connection/Rules/AccumulationConnectionRule.h`
- `Connection/Modifiers/FieldExtractionModifier.h`
- `Connection/Modifiers/AccumulationSortConfig.h`
- `Connection/Modifiers/SlotRoleModifier.h`
- `Connection/Modifiers/DebugTagModifier.h`

---

## Test Coverage

| Sprint | Tests Added | Total |
|--------|-------------|-------|
| Sprint 4 | 156 | 156 |
| Sprint 5 | 62 | 218 |
| Sprint 5.5 | 19 | 237 |
| Sprint 6.0.1 | 102 | 339 |

---

## Uncommitted Work

**Staged:**
- Variadic API implementation
- Research documents (3)
- BoolOpNode fix (partial)

**Unstaged:**
- BoolOpNode.cpp additional fixes
- BoolOpNodeConfig.h additional fixes

**Untracked:**
- `accumulation-slot-proper-design.md`

### Recommended Action

```bash
# Commit Sprint 6.0.1 completion
git add .
git commit -m "feat(Sprint6.0.1): Variadic API + BoolOpNode fix + Accumulation design"
git push
```

---

## Build & Test Commands

```bash
# Build everything
cmake --build build --config Debug --parallel 16

# Run connection tests (102 passing)
./build/libraries/RenderGraph/tests/Debug/test_connection_rule.exe --gtest_brief=1

# Run resource management tests (157 passing)
./build/libraries/ResourceManagement/tests/Debug/test_resource_management.exe --gtest_brief=1
```

---

*Updated: 2026-01-06*
*By: Claude Code*
