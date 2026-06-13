---
title: VIXEN Maturation Backlog (Game-Renderer Track)
aliases: [Maturation Backlog, Consolidated TODO, Game-Renderer Backlog]
tags: [roadmap, planning, backlog, game-renderer, consolidation, audit]
created: 2026-06-13
status: active
priority: critical
supersedes-direction-of: Production-Roadmap-2026 (Workstreams 3-4 deferred under this track)
---

# VIXEN Maturation Backlog — Game-Renderer Track

Consolidated, deduplicated gap/TODO list built from every current audit and the UNDERTOW
consumer feedback. **Direction chosen 2026-06-13: mature VIXEN into an embeddable, moddable
game renderer** (the [[01-Architecture/Architecture-Review-Game-Renderer-2026-06-12|Architecture Review]]
target), driven by [[05-Progress/features/consumer-feedback-undertow|UNDERTOW]] as the first real consumer.

> [!important] This is a re-plan, not an addition
> The architecture review's own verdict: adopting the game-renderer target is *"a re-plan, not an
> addition."* It collides on the calendar with the old [[05-Progress/Production-Roadmap-2026|Production Roadmap]]
> Workstreams 3–4 (~936h of physics / multi-GPU / VR, **none of it presentation or embedding work**).
> Under this track those workstreams are **deferred**, and the SIGGRAPH paper track runs as a
> parallel maintenance concern (keep the benchmark suite green — it is the only consumer exercising
> several code paths).

## Strategic framing

VIXEN is a **mature research renderer but an immature game renderer.** As the single-purpose
Vulkan voxel ray-marching research platform it was built to be, it is in good shape (1000+ tests,
paper draft done, all research phases complete). Against the game-renderer bar the review scores
**every subsystem at maturity 2–4, none at production-5**, with **10 confirmed blockers + 11
confirmed majors**. Its words: *"the bones are unusually good — better than a typical research
codebase… but the boundary does not exist."*

> [!warning] The roadmap is stale; this doc is the live plan
> The [[05-Progress/Production-Roadmap-2026|Production Roadmap]] changelog died 2026-01-10 and
> [[05-Progress/features/Sprint8-Timeline-System|Sprint 8]] is still "PLANNING" (nothing built).
> The live strategic intent is the **2026-06-12 architecture review + UNDERTOW feedback**, not the
> roadmap. Reconciling the roadmap is itself a housekeeping task below.

## Source documents

| Source | Date | Role | Tag used below |
|---|---|---|---|
| [[01-Architecture/Architecture-Review-Game-Renderer-2026-06-12\|Architecture Review: Game-Renderer Readiness]] | 2026-06-12 | **Authoritative** — 53 claims, 2 adversarial rounds | `AR#n` |
| [[05-Progress/features/consumer-feedback-undertow\|UNDERTOW Consumer Feedback]] | 2026-06-12 | First consumer's integration pain | `FR-n` |
| [[05-Progress/features/Sprint8-Timeline-System\|Sprint 8 — Timeline System]] | planning | The explicit NEXT (144h, unbuilt) | — |
| [[05-Progress/audits/audit-2026-01-10-18-15-WIP-TaskProfile\|TaskProfile WIP Audit]] | 2026-01-10 | 4 critical races found | `TS-n` / `ARCH-n` |
| [[05-Progress/features/Sprint6.5-Consolidation-Audit\|Sprint 6.5 Consolidation Audit]] | 2026-01-09 | Fragmentation consolidated (mostly done) | — |
| `ARCHITECTURE_CRITIQUE_2026-01-03.md` (repo root, ×3) | 2026-01-03 | Pre-allocation theme (status now unverified) | `MEM-n` |
| `memory-bank/progress.md` + library code grep | — | ~110 real code-debt markers | `code` |

## Cross-doc convergence (highest-signal themes)

Findings that recur across **3+ independent docs** — this is where the real debt concentrates:

1. **Process-fatal error model** — `std::exit(1)` / throw scattered (`RenderGraph.cpp:224`,
   `VulkanSwapChain.cpp:249`, ~20 in texture loaders). [AR#1 blocker] and the root of UNDERTOW's
   opaque crashes [FR-2/FR-3]. *Prerequisite gate for everything mod-facing.* **Phase 1 done 2026-06-13**
   (every process-fatal `exit()` removed — [[Error-Model-Refactor-2026-06]]); the lifecycle→`RenderFrame`
   propagation channel remains [phase 2].
2. **Latent thread-safety debt, all unmasking at once** — 4 critical TaskProfile races
   [TS-1…4, "1hr critical path"] + TypedCacher lock-during-wait **guaranteed deadlock** [AR#51] +
   racy `VoxelInjectionQueue`. Masked by single-threaded use today; **all surface the moment
   anything parallelizes** [AR#88].
3. **EventBus is observable-not-operable** — ~~`__COUNTER__` event IDs diverge across translation
   units *within one build* [AR#66]~~ **FIXED 2026-06-13** (consumer resize work, merge `7615897`):
   `AUTO_MESSAGE_TYPE` now hashes the definition site (`StableMessageTypeId` = FNV-1a of `__FILE__`+`__LINE__`),
   which is TU-stable — this was silently breaking `WindowResized`→swapchain-recompile (FR-11). Review
   caveats (follow-up, low risk): relies on `__FILE__` expanding identically across TUs; 32-bit hash
   collisions are undetected (a debug-time collision assert would close it). Still open from AR#66/ARCH-2:
   God-Object coupling forces 200+ file recompiles; no queue pre-allocation [MEM-2]; no command layer.
4. **Connection system silently mis-wires** — typed connections accept type mismatches and fault
   at runtime [FR-4 "genuinely nasty silent footgun"]; registry closed to consumers [AR#23–25];
   ~20 Connect calls with no presets [FR-9].
5. **Everything is swapchain-bound** — no render-to-texture [AR#28 blocker]; extent desync [FR-5];
   STORAGE_BIT assumption [FR-2]; ~~`MAX_SWAPCHAIN_IMAGES=3` overrun [FR-3]~~ **FIXED 2026-06-13**
   (`266bfa3`: per-image sync arrays now sized to the actual image count, SwapChainNode-owned).
6. **Cache identity broken for custom content** — custom generators silently regress to the cornell
   box [AR#52]; keys unpublishable [AR#55].

---

## The backlog

Priority tiers map onto the architecture review's Phase 0–5 sequencing. `[AR#n]` etc. cross-reference
the source findings above.

### P0 — Cheap unblockers & correctness (do now, ~1–2 weeks)

Correctness bugs + the only-consumer's pain + legal hygiene. Wasted on no possible future.

- [x] **Fix critical TaskProfile races + TypedCacher deadlock** [TS-1…4, AR#51] — **DONE 2026-06-13** (TDD red→green, full solution build green, all affected suites pass):
  - **AR#51 TypedCacher deadlock** — `GetOrCreate` waited on the pending `future.get()` *while holding* `m_lock`; the creator needs that lock to fulfil the promise → guaranteed mutual deadlock. Fixed: copy the `shared_future`, unlock, then wait. Regression test reproduced it (5012 ms → 125 ms); 43 existing cacher tests still pass.
  - **TS-4 Sampler dtor/move could `std::terminate`** — `RecordMeasurement` can throw `bad_alloc`; dtor & move-assign are `noexcept`. Fixed with a `noexcept` `RecordResult` helper (also collapses DRY-1's three duplicated record blocks).
  - **TS-3 deferred-action flags** — `pendingDecrease_/pendingIncrease_` were plain `bool` written in bus handlers, read in `ProcessDeferredActions` → data race. Fixed: `std::atomic<bool>` + `exchange`. Added the first deferred-action coverage.
  - **TS-2 `Begin()/End()` unsafe** — deprecated the legacy shared-state timing API; migrated its only (dead-but-documented) caller `VirtualTask::BeginProfiling/EndProfiling` to per-task `Sampler`s (via a `shared_ptr` holder, keeping `VirtualTask` copyable).
  - **TS-1 move-assign "race"** — investigated, **dismissed as a false positive**: Samplers are thread-local (every `Sample()` site is a stack-local), so the move-assignment is not a data race; the audit's reorder would risk losing measurements.
- [~] **Replace process-fatal error model** [AR#1] — **IN PROGRESS (phase 1 done).** Design + phased
  plan: [[Error-Model-Refactor-2026-06]]. ✅ **Phase 1 — ALL process-fatal `exit()` de-fataled (2026-06-13):**
  `ConnectNodes` duplicate-connection `std::exit(1)` → `throw` (commit `339096f`); the 20 texture-loader
  `exit(1)` → `VulkanResult`/`VulkanStatus`, leak-free, single-owner cleanup (commit `ce4cab00`);
  `VulkanSwapChain` zero-extent `exit(-1)` → recoverable `VK_ERROR_OUT_OF_DATE_KHR`, SwapChainNode throws
  for the deferred-recompile path to retry (commit `1c68ed4d`). Adopts the existing `std::expected`
  `VulkanResult`/`VulkanStatus` + `VK_CHECK` family. **Remaining (phase 2 — "its own session"):** the
  architectural channel — `NodeInstance::Execute`/`Compile` return `void` and `RenderFrame()` only returns
  `VK_SUCCESS`, so node failures can't propagate; protect the initial `Compile()` in `Prepare()`. (Phase 3:
  adopt `VK_CHECK`/status across the ~396 `throw`s where recovery beats fail-and-report.) **Gate for all
  mod-facing work.**
- [~] **UNDERTOW quick wins** — **PARTIAL (2026-06-13).** Done: cross-platform validation gate via
  `VIXEN_VULKAN_VALIDATION` (not the MSVC-only `#ifdef _DEBUG`) [FR-1, commit `6519b77`]; reusable
  `vixen_stage_assets()` CMake helper [FR-10, commit `91bba98`]; per-image sync arrays sized to the
  actual swapchain image count via SwapChainNode ownership [FR-3, commit `266bfa3` — the pure fix, not
  the `MAX_SWAPCHAIN_IMAGES` band-aid; also closed AR#16 post-cleanup-execute crash surfaced by the
  validated run]. **Remaining:** **reject connection type mismatches** instead of silent implicit
  conversion [FR-4, touches the connection system — note the 3 pre-existing `test_connection_rule`
  binding failures live in that area]; discover `WindowNode` by type not the magic name `"main_window"`
  [FR-6].
- [x] **License cleanup** [AR#6] — **DONE 2026-06-13** (commit `3b5494e`): 32 Sprint-6 files'
  `GPL-3.0` headers → `MIT` to match the canonical root LICENSE + README badge. Header-only, no code change.
- [x] **Cache generator identity** [AR#52] — **DONE 2026-06-13** (commit `8a6267b`, red→green test):
  added `VoxelSceneCreateInfo::customGeneratorName`, folded it into `ComputeHash`/`operator==` (distinct
  custom generators → distinct keys; empty name keeps built-in keys), and `GenerateScene` now invokes the
  named generator instead of the silent cornell fallback for `SceneType::Custom`.

### P1 — The pivotal decision (cheapest, highest-leverage item in the review)

- [ ] **Re-scope [[05-Progress/features/Sprint8-Timeline-System|Sprint 8]] against public mod-API
  requirements *before writing its 144h of code*** [AR#76 / Decision #1]. Sprint 8 ("Everything Is
  A Node": GraphSerializer, GraphEditorNode, ValidationNode, SnapshotNode) already builds ~80% of
  the mod-API mutation machinery. If it ships with raw handles / throw-on-error / no string
  addressing, the mod API is rebuilt later at far higher cost. Also reverse the
  `Sprint8-Timeline-System.md:178` line positioning VIXEN as *"not a plugin host."*
  **Cost = editing a planning doc. Risk if skipped = the most expensive failure mode in the review.**

### P2 — Engine boundary (game-renderer foundation, review Phase 1)

- [ ] **Extract instantiable `EngineContext`** (kill `VulkanGraphApplication` singleton + once_flag;
  lift `NodeTypeRegistry`/`MessageBus`/`RenderGraph`/`CalibrationStore` into `Vixen::EngineContext`
  + `EngineConfig`; `BenchmarkRunner` is the factoring reference) [AR#7]
- [ ] **De-singletonize** `MainCacher` / `CapabilityGraph` / `ProfilerSystem` (static state → one
  device per process; blocks game+editor instances) [AR#8]
- [ ] **Ship a consumable artifact** — `install(EXPORT)` / `VIXENConfig.cmake` / `find_package(VIXEN)`;
  *"Undertow physically cannot link VIXEN today"* [AR#2]
- [ ] **Sever build-layering leaks** — 3 core libs PUBLIC-include `application/main/include`; ~32
  library files `#include "Headers.h"`; break core↔nodes↔SVO entanglement + CMake cycles; relocate
  shared decls into `libraries/Core` (which currently has no real core layer [AR#74]) [AR#3/#4]
- [ ] **Host-supplied window/surface injection** (`ExternalWindowNode`; feasible without engine
  edits — SwapChainNode already consumes HWND via typed slots) [AR#9]
- [ ] **Distinct recompile-vs-shutdown lifecycle hooks** — `CleanupImpl` runs on both; naive
  impl **deadlocks on resize** and destroys persistent state [FR-7]. Plus the "render-to-swapchain"
  authoring recipe so node authors stop re-hitting FR-5/FR-7 [FR-8].
- [ ] **Bring the sprint branch onto main's merged GLFW port** [AR#11] — windowing/input is already
  GLFW end-to-end on `main`; `production/sprint-6-timeline-foundation` predates it (Win32-only).
- [ ] **Embedding docs + API stability story** — `Vixen-Docs/06-Embedding/Hosting-VIXEN.md`;
  generated `VixenVersion.h`; designated supported-header set [AR#12/#13]

### P3 — Presentation layer (review Phase 2)

- [ ] **`RenderTargetNode` / render-to-texture** [AR#28, Decision #3] — *"essentially the entire
  grand-strategy presentation layer hangs off this single change"*: minimap, portraits, picking
  ID-buffers, fog-of-war, post-processing, multi-view. Fix is a producer node, not new
  infrastructure (VMA/Direct allocators already make arbitrary images). Also un-stubs
  `AllocateResources()` + `FindMemoryType` [AR#18].
- [ ] **Many-entity draw path** — instancing / draw lists / `vkCmdDrawIndirect` (today exactly one
  `vkCmdDraw` in the whole tree) [AR#31]
- [ ] **Per-frame dynamic content** — `StreamingBufferNode`/`DynamicBufferNode`; drain the dead-ended
  `BatchedUpdater` (`VulkanDevice::RecordUpdates` has zero callers) [AR#33]; per-frame dynamic
  geometry / multi-draw for text+UI (`UIDrawListNode`) [AR#34]
- [ ] **Alpha blending** — one hardcoded `blendEnable=VK_FALSE` blocks all UI-over-3D; add
  `BLEND_MODE` string param (CULL_MODE pattern) [AR#32]
- [ ] **Multi-view / multi-camera** + flexible camera (ortho projection, zoom-to-cursor, camera-
  relative transform) — current camera is orbit-only, perspective-only, swapchain-bound [AR#29/#30]
- [ ] **Sync model: allow >1 submitting pass per frame** [AR#21] — every leaf node independently
  `vkQueueSubmit`s the frame's single binary semaphore → only one submitting pass composes today.
  Surfaces the moment a second view is wired. Sprint 8 `TimelineNode` is the named fix.
- [ ] **Picking/selection** — CPU click+drag-select is buildable today (`queryRegion`,
  `getEntityByMorton`, `CameraData` inv matrices all exist); GPU pixel-exact ID-buffer is later
  [AR#35]. Note: `MouseButtonEvent` is declared but **never published by InputNode** — fix that.

### P4 — Deep-sim / voxel pillar (review Phase 3)

- [ ] **Complete SVO incremental update** [AR#41 blocker] — `updateBlock()` never patches
  `ChildDescriptors`, so voxels added to a previously-empty region are unreachable by ESVO
  traversal and the GPU never sees mutations until a full O(world) `rebuild()`. Implement
  dirty-subtree descriptor patching + per-brick recompression + double-buffered swap + far-pointer.
- [ ] **Invert world ownership / sim→render change bridge** [AR#48] — only flow today is a one-shot
  bake; `GaiaVoxelWorld` emits zero change events. Needs Morton-keyed dirty journal flushed as bus
  events at FrameStart + a consuming node + the genuinely-missing GPU delta-upload leg. Building
  blocks (`VoxelInjectionQueue`, dirty-volume observers) exist but are unwired.
- [ ] **Chunked multi-volume worlds + fix scale ceilings** [AR#43/#44, Decision #5] — the 15-bit
  childPointer corrupts silently past 32,767 descriptors (the **first wall hit, below strategic-map
  scale**); Morton ±2²⁰/axis with no range validation; single root block. Compose bounded volumes
  rather than widening one global octree. Forces the terrain decision (voxel vs heightmap) early.

### P5 — Scale & mod ecosystem (review Phases 3–5)

- [ ] **Streaming + eviction** [AR#42/#53] — `ISVOStreamingManager` has zero implementations;
  nothing evicts; galaxy-scale world *"exhausts VRAM and RAM by construction."* (Note:
  `ShaderCacheManager` is an in-repo template that already has string keys + LRU eviction.)
- [ ] **Wire VMA** before `DirectAllocator` hits `maxMemoryAllocationCount`≈4096 at game scale;
  add `VK_EXT_memory_budget` runtime re-detection + defragmentation [AR#63/#64/#65]
- [ ] **Image upload path + dedicated transfer queue** — `BatchedUploader` is buffer-only; only
  image path is blocking init-time load; device creates exactly one queue [AR#61]
- [ ] **Shader mod pipeline** — promote runtime reflection to first-class (demote SDI headers to
  internal optimization), add VFS/includer seam + namespaced shader identity, pre-baked SPIR-V
  distribution [AR#56/#57/#79]; spec-constant reflection is an empty stub [AR#58]
- [ ] **EventBus → operable** — runtime event-name registry (stable hashes, not `__COUNTER__`);
  first-class command layer with reply routing; honored subscriber priority / stop-propagation
  (documented but unenforced — UI must claim clicks before world picking); mod-reserved category
  range; pooled/coalesced payloads [AR#66–71]
- [ ] **Mod ops API** — generational opaque handles (replace raw vector indices + typeid keys)
  [AR#78]; `GraphSerializer` as a public format [AR#75]; introspection/enumeration surface [AR#81];
  capability manifests documented+versioned [AR#80]; thin script-binding facade [AR#77]

### Housekeeping (orthogonal, low effort, schedule anytime)

- [ ] **Reconcile the 5-month roadmap drift** — Sprint 8 vs Workstream-3 duplicate TimelineNode/
  FrameHistory work with conflicting hours; stale "IN PROGRESS" headers on completed Sprint 4;
  Phase-1 MultiDispatchNode table lists already-shipped tasks as Planned.
- [ ] **Verify which 2026-01-03 pre-allocation items actually shipped** [MEM-1…11] — status unknown
  post-Sprint-6; confirm before trusting the critique's "at risk" framing.
- [ ] **Split the TaskProfileRegistry God Object** [ARCH-1] — 715 lines, 6 responsibilities, +30s/
  change compile; introduce `IPressurePolicy` to break EventBus coupling [ARCH-2].
- [ ] **Code-debt triage** [code] — 83 TODOs; finish `DescriptorSetNode` MVP stubs (3 dynamic
  descriptor-update entry points non-functional); CashSystem cacher serialization stubs;
  `GaiaVoxelWorld` relationship-pair queries return placeholders; `RenderGraph::AllocateResources`
  is a pass-through (no transient memory aliasing); re-enable loop propagation in the live app.

---

## Sequencing & dependencies

```
P0 (correctness + consumer + license)  ──┐
                                          ├─► P1 Sprint 8 re-scope ─► P2 boundary ─► P3 presentation
P1 gates Sprint 8 implementation ─────────┘                                              │
                                                                                         ▼
                          P4 deep-sim pillar (SVO update + world bridge + chunking) ◄─────┘
                                                                                         │
                                                                          P5 scale + mod ecosystem
```

- **P0 first, always** — the races/deadlock are real latent landmines; P0 unblocks honest error
  handling that everything downstream depends on.
- **P1 before any Sprint 8 code** — it is a doc edit that prevents a 144h rebuild.
- **Threading review before P4** [AR#88] — Phase 3 unmasks every concurrency bug at once; do the
  P0 thread-safety fixes and a deliberate review first.
- **Keep the benchmark suite green throughout** [AR#87] — it is the regression net and the only
  consumer exercising the fragment pipeline + headless bring-up.

## Maturity scorecard (from the review)

| Subsystem | Maturity (1=sketch…5=prod) | | Subsystem | Maturity |
|---|---|---|---|---|
| RenderGraph-Core | 3 | | Voxel-Stack | 3 |
| RenderGraph-Connection | 3 | | Gaia-ECS | **2** |
| RenderGraph-Nodes | 3 | | ResourceManagement | 3 |
| VulkanResources | **2** | | Infra | 3 |
| CashSystem | 3 | | Profiler | 3 |
| ShaderManagement | 3 | | Application-Layer | **2** |
| | | | Docs/Roadmap | 4 |

**Gap tally (2 adversarial rounds, 53 claims):** 10 confirmed blockers, 11 confirmed majors, 32
partial (mostly *narrower* than claimed — "missing piece is wiring and a boundary, not a from-scratch
capability"), 0 fully refuted. **6 came back worse:** custom-generator cornell fallback;
`CleanupByTag` executing dead nodes; one-submitting-pass-per-frame; `TextureCacher` VK_NULL_HANDLE
stub; `AUTO_MESSAGE_TYPE` ID divergence within one build; spec-constant reflection empty stub.

---

*Created 2026-06-13 by Claude Code — consolidation of the 2026-06-12 architecture review, UNDERTOW
consumer feedback, the Sprint 6.5 / TaskProfile audits, the 2026-01-03 architecture critique, and
library code-debt markers. Living document: tick items as they land; promote chosen tiers into
[[05-Progress/Production-Roadmap-2026|the roadmap]] / HacknPlan once reconciled.*
