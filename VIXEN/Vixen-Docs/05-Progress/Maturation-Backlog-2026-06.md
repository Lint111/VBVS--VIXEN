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
- [x] **Replace process-fatal error model** [AR#1] — **DONE (phases 1-3 complete; merged to main 2026-06-13).**
  Design + phased
  plan: [[Error-Model-Refactor-2026-06]]. ✅ **Phase 1 — ALL process-fatal `exit()` de-fataled (2026-06-13):**
  `ConnectNodes` duplicate-connection `std::exit(1)` → `throw` (commit `339096f`); the 20 texture-loader
  `exit(1)` → `VulkanResult`/`VulkanStatus`, leak-free, single-owner cleanup (commit `ce4cab00`);
  `VulkanSwapChain` zero-extent `exit(-1)` → recoverable `VK_ERROR_OUT_OF_DATE_KHR`, SwapChainNode throws
  for the deferred-recompile path to retry (commit `1c68ed4d`). Adopts the existing `std::expected`
  `VulkanResult`/`VulkanStatus` + `VK_CHECK` family. ✅ **Phase 2 — host-facing propagation channel
  (2026-06-13, Option A — status at the boundary, since UNDERTOW is a C# host where C++ exceptions are UB):**
  `RenderFrame()` catches node-Execute failures → status (`754cfd51`); `Prepare()` catches initial-compile
  failures → `IsPrepared()`/`GetLastError()`, no rethrow→exit (`3be2a92c`); `Render()`/`Update()` catch-all
  guards (`32e689ce`). No exception can now escape `Prepare`/`Update`/`Render`/`RenderFrame` to the host.
  ✅ **Phase 3 — device-loss recovery (2026-06-13):** reframed away from the misleading "convert ~396
  throws" (most are correct fail-fast invariants) to the one high-value recoverable failure — GPU device
  loss. Detect `VK_ERROR_DEVICE_LOST` → rebuild the whole graph on a fresh device (ordering-correct
  teardown-reverse/rebuild-forward; instance+surface+window persist) → resume rendering. Validated via a
  fault-injection harness: full rebuild + ~73-89s continuous post-recovery rendering, zero validation
  errors. Fixed two pre-existing UAFs the rebuild surfaced (dangling GPU-profile pointer via
  CalibrationStore.Load; rebuildable ConstantNode). See [[Device-Loss-Recovery-2026-06]]. **Gate for all
  mod-facing work — CLOSED.** (Future, non-blocking: OOM/shader-compile recovery, unrecoverable-loss
  terminal path, defer-initial-compile-retry.)
- [~] **UNDERTOW quick wins** — **PARTIAL (2026-06-13).** Done: cross-platform validation gate via
  `VIXEN_VULKAN_VALIDATION` (not the MSVC-only `#ifdef _DEBUG`) [FR-1, commit `6519b77`]; reusable
  `vixen_stage_assets()` CMake helper [FR-10, commit `91bba98`]; per-image sync arrays sized to the
  actual swapchain image count via SwapChainNode ownership [FR-3, commit `266bfa3` — the pure fix, not
  the `MAX_SWAPCHAIN_IMAGES` band-aid; also closed AR#16 post-cleanup-execute crash surfaced by the
  validated run]. **Remaining:** **reject connection type mismatches** instead of silent implicit
  conversion [FR-4, touches the connection system — note the 3 pre-existing `test_connection_rule`
  binding failures live in that area]; discover `WindowNode` by type not the magic name `"main_window"`
  [FR-6].
- [x] **UNDERTOW consumer integration merge** — **DONE + AUDITED, merged to main 2026-06-13** (consumer
  branch `claude/wsl-build-portability`, 13 commits). Embedding seams for the C# host (`SimLoop` 30Hz
  logic loop, live `GetWindowHandle()`, `MarkVoxelSceneDirty`, `SetHudData`); RmlUi HUD data model +
  headless smoke test; **WSL2/Dozen GPU enablement** (auto-provision Mesa Dozen, ICD selection, swapchain
  UNORM-for-STORAGE + instance-extension filtering — all gated on Dozen/WSL, zero native impact) [FR-19/20/21];
  a genuine `CreateVulkanInstance` VkResult-propagation bug fix (was → SIGABRT); process-wide logger
  min-level filter + per-frame node-log re-leveling to DEBUG. Audit: high quality, every change gated +
  documented, no silent error-swallowing, includes a test. Validated on merged main: clean build, HUD
  smoke 3/3, task-profile 61/61, device-loss recovery + normal rendering both green (0 validation errors).
  See [[consumer-feedback-undertow]].
- [x] **License cleanup** [AR#6] — **DONE 2026-06-13** (commit `3b5494e`): 32 Sprint-6 files'
  `GPL-3.0` headers → `MIT` to match the canonical root LICENSE + README badge. Header-only, no code change.
- [x] **Cache generator identity** [AR#52] — **DONE 2026-06-13** (commit `8a6267b`, red→green test):
  added `VoxelSceneCreateInfo::customGeneratorName`, folded it into `ComputeHash`/`operator==` (distinct
  custom generators → distinct keys; empty name keeps built-in keys), and `GenerateScene` now invokes the
  named generator instead of the silent cornell fallback for `SceneType::Custom`.

### P1 — The pivotal decision (cheapest, highest-leverage item in the review)

- [x] **Re-scope [[05-Progress/features/Sprint8-Timeline-System|Sprint 8]] against public mod-API
  requirements *before writing its code*** [AR#76 / Decision #1] — **DONE 2026-06-13.** Sprint 8's
  mutation machinery (GraphSerializer, GraphEditorNode, ValidationNode, SnapshotNode — ~80% of the
  mod-API machinery) is now specified against public-API requirements so it ships *as* the mod API.
  Edited the Sprint 8 plan: (1) **reversed the anti-plugin Framework Positioning** (`:178` "not plugin
  hosts" → embeddable mod host, UNDERTOW = consumer zero); (2) added a **Public Mod-API Requirements**
  section — R1 string addressing, R2 Result-not-throw (the error-model prerequisite is **done** — AR#1),
  R3 generational opaque handles, R4 persisted connection records, R5 thin handles/strings/POD boundary,
  each cross-referenced to the Architecture Review §6 layers; (3) amended the Phase 4–5 specs
  (string-addressed `EditCommand`/GraphSerializer, `Result`-returning deserialize, +6h connection-record
  task ahead of serialization). **Cost = editing a planning doc (the cheapest, highest-leverage item in
  the review); risk-if-skipped = the most expensive failure mode (moddability retrofit).** This closes
  **P1**.

### P2 — Engine boundary (game-renderer foundation, review Phase 1)

- [x] **Extract instantiable `EngineContext`** [AR#7] — **DONE 2026-06-14** (2 increments):
  *(inc 1)* killed the `VulkanGraphApplication` singleton (`GetInstance`/`once_flag` gone, ctor
  public, main.cpp owns it via `unique_ptr`). *(inc 2)* lifted `NodeTypeRegistry`/`MessageBus`/
  `RenderGraph`/`CalibrationStore` into `Vixen::RenderGraph::EngineContext` + `EngineConfig` in the
  **RenderGraph library** (resolved the home Q: it already owns registry/calibration/profile-registry,
  and Profiler→RenderGraph is one-way → no cycle, no new links). Resolved the device-ownership Q: the
  graph creates its OWN instance/device via in-graph nodes (InstanceNode→DeviceNode), so EngineContext
  needs **no** device injected. Node registration is caller-supplied via `EngineConfig::registerNodeTypes`
  (app=31, benchmark=26). The app owns one `EngineContext` + non-owning views (call sites unchanged).
  Validated: 20s smoke, graph builds + renders, zero crashes. Already consumer-available via the AR#2
  SDK (it ships inside the exported RenderGraph lib). Follow-up: BenchmarkRunner could adopt
  EngineContext for dedup.
- [x] **De-singletonize** `MainCacher` / `CapabilityGraph` [AR#8] — **DONE 2026-06-14** (core two of
  three; **ProfilerSystem deliberately deferred** — see end of item). This was the last global-state
  blocker to running >1 `EngineContext` (game + editor) in one process.
  - **`MainCacher::Instance()` removed.** The cacher is now an ordinary object owned by its host:
    `EngineContext` creates + owns one when the host injects none (`EngineConfig::mainCacher`);
    `BenchmarkRunner` owns a method-local one. CashSystem-internal cachers + `DeviceRegistry` reach
    sibling cachers through a back-pointer (`CacherBase::SetMainCacher`, set by MainCacher at every
    creation path) instead of the global. `RenderGraph::GetMainCacher()` lost its `Instance()`
    fallback (asserts an injected cacher). Bonus fix: `EngineContext::~EngineContext` now calls
    `mainCacher_->Shutdown()` before the bus is destroyed, closing a latent exit-time use-after-free
    the singleton path had (its bus subscription outlived the bus). Removed 3 dead singleton-coupled
    registration helpers + 1 dead, unbuilt, already-broken test (`test_cash_system.cpp`).
  - **`CapabilityGraph`** — the 4 process-wide **static** availability vectors (instance ext/layer,
    device ext/feature) → per-graph instance state (one `CapabilityGraph` per `VulkanDevice`). Leaf
    capability nodes consult their owning graph; device-level sets are supplied by `VulkanDevice`,
    instance-level sets self-populate from the loader (`vkEnumerateInstance*Properties` is global, no
    `VkInstance` needed) — so the old `InstanceNode`→static→device bridge is gone.
  - **Validated:** full Debug build green; cacher tests (45) + device/capability tests (48) pass;
    25 s app smoke renders voxels per-frame with no `VK_ERROR`/validation errors. Pre-existing,
    AR#8-unrelated failures (over code paths untouched here): `test_swap_chain_node.ConfigHasTwoInputs`
    (stale slot-count), `test_cornell_box.LeftWallHit_Red` (SVO ray geometry),
    `test_profiler.ConfigToMetricsExportFlow` (profiler metrics validity).
  - **ProfilerSystem NOT done (deferred, decision 2026-06-14).** It is **benchmark-only** — the app and
    `EngineContext` never touch it — so it is *not* a blocker to multiple engine instances, and
    de-singletonizing it would re-open the deliberately-deferred `BenchmarkRunner` surface for zero
    engine-side gain. Same call as "BenchmarkRunner adopts EngineContext": leave it until a concrete
    in-engine profiling need appears.
- [x] **Ship a consumable artifact** [AR#2] — **DONE 2026-06-14** (fat self-contained SDK).
  `cmake -B build -DVIXEN_INSTALL_EXPORT=ON && cmake --install build --prefix <sdk>` produces a
  unified `VixenTargets` export + `VIXENConfig.cmake`; an external project then does
  `find_package(VIXEN)` + links `Vixen::RenderGraph`. Validated end-to-end: throwaway consumer
  configures, generates, and **links consumer.exe**. All 14 VIXEN libs + the vendored deps
  (glm/glfw/stb/VMA/magic_enum/nlohmann_json/miniz/rmlui_core/ProjectHash) are bundled into the
  export; gli/freetype/gaia ship their own configs inside the SDK; only Vulkan/TBB/Threads are
  resolved externally. Machinery lives in `cmake/VixenInstall.cmake` + `cmake/VIXENConfig.cmake.in`,
  gated behind `option(VIXEN_INSTALL_EXPORT)` (default OFF — packaging-only, dev build untouched).
  *Undertow can now link a prebuilt VIXEN.* (Super-build / add_subdirectory consumption was already
  unblocked by the [AR#3/#4] cycle-breaking.)
- [x] **Sever build-layering leaks** [AR#3/#4] — **DONE 2026-06-14** (3 increments, all merged):
  (A) relocated `Headers.h`/`VixenHash.h`/`MeshData.h` → `libraries/Core`; the 3 core libs now link
  `Core::Core` instead of PUBLIC-including `application/main/include`.
  (B) broke the `RenderGraph↔CashSystem` CMake link cycle by relocating `SceneGenerator` down to
  `SVO` (the layer both already link) → one-directional DAG.
  (C) decoupled the graph core from concrete leaf nodes — `RenderGraph.cpp` now `#include`s zero
  `Nodes/` headers (dead SwapChain/Present find-loop deleted; CommandPoolNode coupling replaced with
  the `ICommandBufferPreallocator` capability interface, mirroring `IGraphCompilable`).
  Follow-up nit **DONE 2026-06-14**: the legacy all-caps `VIXEN::RenderGraph` namespace is **gone**.
  It was bigger than "SceneGenerator" — it also held `VoxelOctree`/`VoxelTraversal` (RenderGraph/Data)
  with an inconsistent `VIXEN` vs `VIXEN::RenderGraph` nesting and contradictory fwd-decls. All voxel/SVO
  data types (`SceneGenerator*`, `VoxelGrid`, `SparseVoxelOctree`, `OctreeNode`/`ESVONode`/`VoxelBrick`/
  `VoxelMaterial`, `Ray`/`AABB`/`DDAState`) now live in **`Vixen::SVO`** (unified with the modern SVO ns);
  node types stay in `Vixen::RenderGraph`. ~12 files (5 defs + 7 consumers incl. 3 tests). Verified:
  full build green; the rename diff is namespace-lines-only (proven logic-free), so the **pre-existing**
  voxel/SVO unit-test failures it surfaced (test_scene_generators density, test_voxel_octree node-count=0,
  test_voxel_injection, test_svo_builder, test_cornell_box) are unrelated subsystem test debt, not
  regressions. Fully closes AR#3/#4.
- [ ] **Host-supplied window/surface injection** (`ExternalWindowNode`) [AR#9] — **evaluated +
  parked 2026-06-14.** Recon correction to the original note: `SwapChainNode` consumes a
  **`GLFWwindow*`** and creates the surface itself via `glfwCreateWindowSurface` — it does NOT take a
  `VkSurfaceKHR`, so it is **coupled to GLFW**. A true host-owned (non-GLFW) window therefore DOES need
  engine surgery (decouple `SwapChainNode` to accept a `VkSurfaceKHR`). No consumer needs it yet —
  UNDERTOW embeds the *other* way (its RmlUi UI inside VIXEN's window). Deferred until a concrete
  editor / host-owned-window need appears; the pure shape when taken is **surface injection** (host
  supplies a `VkSurfaceKHR`, or a native handle that `ExternalWindowNode` turns into one). See the
  "Host-owned window is not done yet" note in [[Hosting-VIXEN]].
- [x] **Recompile-vs-shutdown lifecycle + render-to-swapchain recipe** [FR-7/FR-8] — **DONE 2026-06-14
  (docs).** The *mechanism* already existed: `CleanupImpl(ctx)` carries `CleanupReason` (Recompile /
  DeviceLost / FinalTeardown, `NodeContext.h`); reference nodes (WindowNode, SwapChainNode,
  UIRenderNode, ConstantNode) branch on it correctly. FR-7/FR-8 were a **discoverability** gap, not a
  missing API — so (per the consumer's "document prominently" suggestion) the contract is now in the
  author-facing `TypedNodeInstance::CleanupImpl` doc-comment + new §3.1/§3.2 in [[RenderGraph]]
  (recompile must be lightweight, no device wait; consume RenderPass/Framebuffers as inputs, don't
  build them in-node). A structural hook-split was considered + rejected (high blast radius across all
  nodes; debatable vs the working `ctx.reason` design).
- [ ] **Bring the sprint branch onto main's merged GLFW port** [AR#11] — windowing/input is already
  GLFW end-to-end on `main`; `production/sprint-6-timeline-foundation` predates it (Win32-only).
- [x] **Embedding docs + API stability story** [AR#12/#13] — **AR#12 docs DONE 2026-06-14:**
  [[Hosting-VIXEN]] (`06-Embedding/Hosting-VIXEN.md`) documents the full embedding flow —
  `find_package(VIXEN)` (the AR#2 fat SDK, 14 libs) → construct `EngineContext`/`EngineConfig` →
  register node types → build graph → own the loop via `Graph().RenderFrame()` → publish
  `ApplicationShuttingDownEvent` + deterministic teardown; includes the SDK packaging command, a
  consumer `CMakeLists`, the `EngineConfig::mainCacher` injection note (AR#8), and the
  build-portability gotchas. **AR#13 DONE 2026-06-14:** generated `<VixenVersion.h>` (from a single
  source of truth — root `project(... VERSION 0.1.0)` now feeds both the C++ macros via
  `cmake/VixenVersion.h.in` and the package-version file; `VixenInstall.cmake` no longer hardcodes
  the version), on `Vixen::RenderGraph`'s public include + installed into the SDK; and a documented
  **supported public-header set** in [[Hosting-VIXEN]] (everything else = internal/may-change). A
  curated umbrella `<Vixen.h>` was deliberately **not** done — premature at 0.1.0 with one consumer;
  revisit when the API stabilizes or a second consumer appears.

### P3 — Presentation layer (review Phase 2)

- [x] **`RenderTargetNode` / render-to-texture** [AR#28, Decision #3] — **DONE 2026-06-14** (merged to
  main, `271a461f`). The P3 presentation-layer keystone: offscreen render targets are now a first-class
  graph concept. Design+plan: [[RenderTarget-Design-2026-06]] + [[RenderTarget-Implementation-Plan-2026-06]].
  Shipped: `IRenderTarget` interface (`IRenderTarget.h`); `SwapChainPublicVariables` implements it;
  color-only `RenderTargetNode` producer (offscreen color images via real memory-type selection, FR-7
  lifecycle, registered + config-tested, 24 tests); and the **full slot migration** — all 13 recording-node
  slots (12 inputs + SwapChainNode output) `SwapChainPublicVariables*` → `IRenderTarget*`, with the 10
  consuming `.cpp`s moved to the interface accessors (`GetView(i)`/`GetExtent()`/`GetImageCount()`/…).
  Verified: full build green; render-graph/node test suites pass (render_target 24, swap_chain 12,
  rendergraph 3, device 22); app smoke clean (0 VK_ERROR/VUID, ~85k render events). `FrameCapture` kept
  `SwapChainPublicVariables*` (genuine swapchain-specific PNG capture — not a render-graph slot).
  **Follow-ups (deferred, noted in the design):** `followSwapchainExtent`/resize; headless pipeline;
  `CompositeNode` view fan-in; consolidate RenderTargetNode's local `DEFAULT_FRAMES_IN_FLIGHT` +
  `FindMemoryType` copies (added to dodge a claimed MSVC `LNK1163`); fix DepthBufferNode's
  `memoryTypeIndex=0` placeholder [AR#18] (NOT done in this pass — its slot migrated but the
  placeholder remains).
- [~] **Many-entity draw path** [AR#31] — **increment 1 DONE 2026-06-14** (on branch
  `claude/ar31-instancing-increment`): hardware **instancing** — one cube mesh, **64 instances** via an
  SSBO of per-instance `mat4` transforms indexed by `gl_InstanceIndex`. New `InstanceBufferNode` produces
  the transform SSBO (FR-7, real memory-type selection, 25-test config suite); rendered in an isolated
  `VIXEN_INSTANCING_DEMO` graph (mirrors `BuildUIGraph`, voxel path untouched) with dedicated
  `InstancingDemo.vert/.frag` (binding-0 SSBO; `Draw.*` couldn't be reused — frag binding-1 sampler
  collision + no MVP-UBO producer). Verified: build green, 0 VK errors, 48631 render events,
  instanceCount=64. Design: [[Instancing-Increment-Design-2026-06]]. **Remaining increments:** heterogeneous
  multi-mesh **draw lists**, `vkCmdDrawIndirect` (GPU-driven), GPU culling, per-frame dynamic transforms.
  NOTE: gotcha — WSL bash does not pass env vars to Windows `.exe`; run the demo via
  `cmd.exe /c "set VIXEN_INSTANCING_DEMO=1&& VIXEN.exe"`.
- [ ] **Per-frame dynamic content** — `StreamingBufferNode`/`DynamicBufferNode`; drain the dead-ended
  `BatchedUpdater` (`VulkanDevice::RecordUpdates` has zero callers) [AR#33]; per-frame dynamic
  geometry / multi-draw for text+UI (`UIDrawListNode`) [AR#34]
- [x] **Alpha blending** — **DONE 2026-06-14** [AR#32]. `BLEND_MODE` string param on
  `GraphicsPipelineNode` (CULL_MODE pattern): `None` (default, opaque — prior behavior), `Alpha`,
  `PremultipliedAlpha`, `Additive`, `Multiply`. Design: [[BlendMode-Design-2026-06]]. The hardcoded
  `blendEnable=VK_FALSE` lived in **two** paths (the `PipelineCacher` live path + the node's manual
  fallback); both now consume one `MakeColorBlendAttachment(mode)` recipe (RenderGraph
  `VulkanStructHelpers.h`, throws on unknown). Blend state is threaded through `PipelineCreateParams`
  (defaulted opaque/write-RGBA so existing callers are unchanged) **and** the pipeline cache key
  (`ComputeKey`) so distinct blend modes never collide on one cached pipeline. Tests: `test_blend_mode`
  (8, pure recipe+config), `test_pipeline_blend_key` (4, cache-key distinctness + opaque default); build
  green, all CashSystem suites pass, app smoke clean (0 VK errors). Follow-ups: per-attachment/MRT blend,
  `logicOp`/dual-source/blend-constants — deferred (single attachment today).
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
