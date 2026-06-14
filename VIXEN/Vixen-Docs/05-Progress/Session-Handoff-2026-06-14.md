# Session Handoff — 2026-06-14 → next session

Quick-orient note for the next agent. **Live plan = [[Maturation-Backlog-2026-06]]** (this is a
pointer + prioritization on top of it). Design source of truth = `Vixen-Docs/01-Architecture/
Architecture-Review-Game-Renderer-2026-06-12.md` (the AR#N item numbers below come from it).

`origin/main` at handoff = `985ad62c`. Working tree clean. All work below is merged + pushed.

---

## Where we are: the "embeddable engine" is in place

A host can now `find_package(VIXEN)` → link `Vixen::RenderGraph` → construct a
`Vixen::RenderGraph::EngineContext` to stand up the whole engine with **no global singleton**.
Three review items landed this run, all consumer/runtime-validated:

- **AR#3/#4** build-layering — clean DAG: RenderGraph↔CashSystem cycle broken (SceneGenerator
  moved to SVO), graph core decoupled from concrete nodes (`ICommandBufferPreallocator`), and the
  `application→Core` header leak severed (`Headers.h`/`VixenHash.h`/`MeshData.h` now in `libraries/Core`).
- **AR#2** consumable artifact — fat self-contained `find_package(VIXEN)` SDK. Validated end-to-end:
  a throwaway consumer configures, generates, AND links `consumer.exe`.
- **AR#7** instantiable `EngineContext` — singleton killed (inc 1), the 4 subsystems lifted into
  `EngineContext`+`EngineConfig` in the RenderGraph lib (inc 2).

(Earlier in the run: AR#1 error-model + device-loss recovery, UNDERTOW consumer integration,
P1 Sprint-8 mod-API rescope.)

---

## Next tasks (recommended order)

### 1. AR#8 — De-singletonize `MainCacher` / `CapabilityGraph` / `ProfilerSystem`  ← recommended next
The last global-state blocker to running **multiple engine instances** (game + editor, or >1
`EngineContext` in one process). EngineContext is instantiable now, but `CashSystem::MainCacher::
Instance()` (+ `CapabilityGraph`, `Vixen::Profiler::ProfilerSystem`) are still process-wide statics.
- **Start:** grep `MainCacher::Instance()` / `CapabilityGraph` / `ProfilerSystem::Instance()` across
  `libraries/` + `application/` — nodes, BenchmarkRunner, and `EngineContext::EngineContext` all reach them.
- **Approach:** thread the cacher as a dependency (EngineContext already takes `EngineConfig::mainCacher`
  and only falls back to `Instance()` — finish the inversion so nodes get it injected, not globally).
- Moderate refactor (many call sites). Pure fix = constructor/context injection, not a second singleton.

### 2. AR#9 — `ExternalWindowNode` (host-supplied window/surface)
Let a host pass its own `HWND`/surface instead of VIXEN creating the window. Recon from AR#7 noted
`SwapChainNode` already consumes a window/surface via typed slots, so this is feasible without engine
surgery — add a node that takes an external handle. Enables true in-host-window embedding.

### 3. AR#12 — Embedding docs
Document the embedding API now that the trio is done: `find_package(VIXEN)` → `EngineContext` →
own-the-loop. Short; high value for the UNDERTOW consumer.

### 4. Cosmetic — `SceneGenerator` namespace
`SceneGenerator`/`VoxelGrid` still carry `VIXEN::RenderGraph` while living in `libraries/SVO`
(relocated in AR#3/#4). Rename to `Vixen::SVO` (or a neutral voxel ns) to fully close AR#3/#4.
Quick, low-value, API-visible (touches ~5 files: SceneGenerator.h/.cpp + VoxelGridNode.h/.cpp +
VoxelSceneCacher.h).

### Deferred (do NOT force) — BenchmarkRunner adopting EngineContext
Investigated + declined this run. BenchmarkRunner shares registry/bus/cacher across all tests but
recreates the graph per test (create→reset→recreate dance, with `shouldClose` at shared scope).
EngineContext deliberately **bundles** the graph (+ graph-coupled CalibrationStore) with registry/bus
— right for the app + device-loss, wrong for the benchmark. A clean unification needs splitting
EngineContext (services vs per-session graph), which re-touches the just-validated app refactor and
reduces its bundling. BenchmarkRunner is the review's *reference* (already non-singleton), not a
required consumer — leave it unless a concrete need appears.

---

## Gotchas / facts the next agent will want

- **Build:** `"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel 16`.
  Run the app via a background timeout (`cd binaries && timeout 20 ./VIXEN.exe`); exit 124/143 =
  timeout-killed = ran fine. `GRAPH_LOG_*` + `std::cout` reach captured stdout; `NODE_LOG_*` do not.
- **WSL ⇄ cmake.exe paths:** cmake.exe is a *Windows* binary — a WSL `/tmp/x` arg lands at `C:\tmp\x`
  (= `/mnt/c/tmp/x`). Use `/mnt/c/...` paths for installs. See [[wsl-cmake-windows-paths]] (auto-memory).
- **Packaging the SDK (AR#2):** gated behind `option(VIXEN_INSTALL_EXPORT)` (default OFF — dev build
  untouched). Produce it with `cmake -B build -DVIXEN_INSTALL_EXPORT=ON && cmake --install build
  --prefix <sdk>`. Machinery: `cmake/VixenInstall.cmake` + `cmake/VIXENConfig.cmake.in`. Bundles the
  14 VIXEN libs + vendored deps; gli/freetype/gaia ship their own configs inside the SDK; only
  Vulkan/TBB/Threads are external. (gaia's upstream config is broken — `set_and_check(... "")` — so
  VIXENConfig includes gaia's `-targets.cmake` directly; don't "fix" that to find_dependency.)
- **EngineContext:** `libraries/RenderGraph/include/Core/EngineContext.h` (+ `EngineConfig.h`, +
  `src/Core/EngineContext.cpp`). Owns registry/bus/graph/calibration; node-type registration is
  caller-supplied via `EngineConfig::registerNodeTypes`. The graph creates its own Vulkan
  instance/device via in-graph nodes (InstanceNode→DeviceNode) — EngineContext needs no device.
- **rtk hook mangles** `git diff/show/reset/check-ignore` — use `rtk proxy git …` for ground truth.
- **`project-rules` skill** must be invoked first thing each turn (UserPromptSubmit hook enforces it).

---

## Session update — 2026-06-14 (later): AR#8 core-two landed

Branch **`claude/ar8-desingletonize`** (off `main`). AR#8 **MainCacher + CapabilityGraph** de-singletonized
and verified; **ProfilerSystem deliberately deferred**. Full detail in [[Maturation-Backlog-2026-06]] (AR#8
entry now `[x]`). Highlights:

- **`MainCacher::Instance()` deleted.** Cacher is host-owned: `EngineContext` creates+owns one when the host
  injects none (`EngineConfig::mainCacher`); `BenchmarkRunner` owns a local one. Internal cachers +
  `DeviceRegistry` reach siblings via `CacherBase::SetMainCacher` back-pointer (set by MainCacher at each
  creation path). `RenderGraph::GetMainCacher()` lost the `Instance()` fallback (asserts injected).
  `EngineContext::~EngineContext` now `Shutdown()`s the cacher before the bus dies — **fixed a latent
  exit-time UAF** the singleton path carried. Removed 3 dead registration helpers + dead `test_cash_system.cpp`.
- **`CapabilityGraph`** — 4 process-wide static availability vectors → per-graph instance state. Device-level
  sets from `VulkanDevice`; instance-level self-populated from the loader (`vkEnumerateInstance*Properties`
  is global), so the old `InstanceNode`→static→device bridge is gone. Leaf nodes consult their owning graph.
- **Verified:** full build green; cacher tests (45) + device/capability tests (48) pass; 25 s app smoke
  renders voxels per-frame, no `VK_ERROR`/validation errors.
- **⚠ Pre-existing failures (NOT from this work — over untouched code paths; don't blame your change):**
  `test_swap_chain_node.ConfigHasTwoInputs` (stale slot-count assertion), `test_cornell_box.LeftWallHit_Red`
  (SVO ray geometry, `hitPoint.x` boundary), `test_profiler.ConfigToMetricsExportFlow` (profiler metrics
  validity; uses `BenchmarkRunner::RecordFrame/Finalize`, not the changed `RunSuiteWithWindow`).
- **ProfilerSystem deferred:** benchmark-only (app/EngineContext never touch it) → not a multi-instance
  blocker; doing it re-opens the deferred BenchmarkRunner surface for no engine-side gain.

**Next task order unchanged:** AR#9 `ExternalWindowNode` → AR#12 embedding docs → cosmetic `SceneGenerator`
`VIXEN::RenderGraph`→`Vixen::SVO` rename. ProfilerSystem only if a concrete in-engine profiling need appears.
