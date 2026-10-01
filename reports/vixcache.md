# VIXEN incremental builds and compiler caching

Lane: `vixcache`; branch: `lane-vixcache`; base `6a116291`, merged with `origin/wave/authoring-convergence` (including lane vixinfra, `4750b66e`) at `cfa99553`. The run was cut off twice by external "model at capacity" errors. A Claude worker finished it after the second cut-off. Commits:

| Commit | What |
|---|---|
| `13851272` | Tested helpers for snapshot caching and incremental checks (cache-contract fixtures) |
| `46c569f7` | Incremental (stamp-gated) checks; pinned CodegenTool snapshot reuse across build trees |
| `d5cd6098` | Concurrent cache-client test for pinned snapshot publication |
| `2599b761` | Native compile commands cacheable across worktrees (prefix maps, no module scanning, PCH settings, Linux ccache discovery) |
| `cfa99553` | Merge of `origin/wave/authoring-convergence` (vixinfra) |
| `1c5ac0af` | Keep vixinfra's runtime-cache path and asset staging out of the rebuild path |

All evidence (JSON records with commit, working tree, build-logic hash, ccache counters, and full logs) is under `build/vixcache-evidence/`. The `/home/liory/scripts/codex-briefs/_lane-rules.md` file named in the continuations is absent; the lane rules embedded in the original brief were followed.

## What was wrong (baseline, `6a116291`)

- **No-op builds were not no-ops.** All 21 CodeGenTool golden checks, the no-new-mutex guard and the CodegenTool build were phony, always-run targets. A build with no source change took 109.1 s and re-ran 23 units. The checks alone took 199.5 s, 42.8 s of it the CodegenTool build.
- **Every build tree re-extracted and rebuilt the pinned kernel snapshot and its CodegenTool.**
- **Compile commands were tree-specific.** All 851 compiles used absolute paths without prefix maps, and 54 embedded worktree paths in `-D` values.
- **154 compiles could not be cached at all.** CMake's C++ module scanning added `-fmodules-ts`, and VIXEN declares no modules. Separately, ccache rejected PCH builds 195 times.
- **ccache was never picked up on Linux.** The configure's ccache probe ran on Windows only: "ccache/sccache not found" even with ccache on PATH.

## Changes

**Incremental checks (`46c569f7`).** Each check now records a success stamp and an input signature. The signature covers schemas, catalogues, kernel recipe sources, the generated artifacts being compared, command arguments, tool readiness and the complete input list. Adding or removing a source with an old timestamp changes the signature. A failed check never advances its stamp. Explicit regen targets stay explicit. Configure-time node manifests and invariant runtimeconfigs are written only when their content changes.

**Pinned snapshot + CodegenTool reuse (`46c569f7`, `d5cd6098`).** The kernel snapshot is archived once per full SHA, under a lock. The CodegenTool binary cache key combines:
- the pin
- SDK/host identity
- target framework and configuration
- the invariant-globalization setting
- a build-recipe digest

Construction is serialized, and each build tree gets its own copies plus a local readiness output. The default location is an untracked user cache; `VIXEN_KERNEL_CACHE_DIR` overrides it (the measurements used `<lane>/build/kernel-cache`). The fixtures cover:
- concurrent publication
- concurrent tool clients
- restoration of a deleted local DLL
- isolation from mutable kernel-clone bytes

The merged peer configure printed `[codegen] reusing pinned kernel snapshot`.

**Cacheable native commands (`2599b761`).**
- `-ffile-/-fdebug-/-fmacro-prefix-map` map the engine, build, FetchContent and Vulkan SDK roots to stable names (`/vixen`, `/vixen-build`, `/vixen-deps`, `/vixen-vulkan`).
- Module scanning is off by default; an explicit caller setting and `CXX_MODULES` file sets are still respected.
- PCH includes use a stable spelling. PCH targets get ccache `sloppiness=pch_defines,time_macros`, and Clang also gets `-fno-pch-timestamp`.
- Linux/macOS find the installed ccache and use the caller's ccache environment.

All 850 merged compile commands carry prefix maps, and none has `-fmodules-ts`.

**vixinfra interaction (`1c5ac0af`).** The merge brought in two regressions:
- **Runtime-cache path on every command line.** vixinfra added `VIXEN_RUNTIME_CACHE_DIR="<build>/runtime-cache"` as a global compile definition. ccache hashes literal definitions, so this would have split the cache per build tree. `Vixen::RuntimeCacheDirectory()` is now defined out of line in `libraries/Core/src/RuntimeCachePaths.cpp`, and only that source gets the definition (source-scoped, kept out of the PCH). The lookup order is unchanged: the `VIXEN_CACHE_DIR` env var, then the build-tree default, then the relative `cache` fallback for consumers built outside VIXEN's CMake.
  - *Why this option:* a per-target definition would still reach ~50 TUs (CashSystem, RenderGraph, VixenApp all include `MainCacher.h`/`RuntimeCachePaths.h`). A generated header would put the literal into the preprocessed output of every includer, which ccache also hashes. A runtime-only value would change vixinfra's behavior (tests would need the env var). One out-of-line TU keeps the behavior and leaves exactly one tree-specific compile.
- **Always-run asset staging.** `vixen_stage_assets` now copies through a stamp that depends on the globbed asset files (`CONFIGURE_DEPENDS`, so added or removed files re-glob). The shared-target deduplication from vixinfra is kept.
- No remaining compile command embeds the runtime-cache path except `RuntimeCachePaths.cpp`.

## Measurements before the merge (base and `2599b761`; queued runs, earlier session environment)

| Scenario | Before | After |
|---|---|---|
| Cold full build | 851 compiles; interrupted by the queue watchdog at 202 s, then 1,608.9 s recovery (≈1,811 s total); 163 uncacheable (options), 195 PCH rejections | 1,750.8 s, 851 compiles, 0 uncacheable |
| No-op full build | 109.091 s, 23 units | **0.342 s, 0 units** |
| Leaf touch (`test_ui_hit_mask.cpp`) | 144.330 s, 25 units | **1.015 s, 1 compile + 1 link** |
| No-op / leaf, RenderGraph+SVO targets only | 0.164 s / 0.934 s | 0.217 s / 0.932 s |
| All 22 checks | 199.481 s, always | 156.059 s first run (incl. reconfigure + first tool build), then stamp-gated |
| Warm rebuild, same tree | — | 211.7 s; 851 compiles, 659 hits / 192 misses |

## Measurements on the merged tree (`cfa99553` + the `1c5ac0af` change; direct `nice -n 10 … -j4`)

| Scenario | Result |
|---|---|
| First build after merge (same tree, warm pre-merge cache) | 820.9 s; 213 compiles (headers changed by the merge), 213 misses |
| No-op full build | **0.263 s, 0 units** |
| No-op, RenderGraph+SVO targets | **0.254 s, 0 units** |
| Leaf touch, full | **3.82 s, 1 compile + 1 link** |
| Leaf touch, RenderGraph+SVO targets | **1.002 s, 1 compile + 1 link** (ccache hit) |
| All 22 checks forced (stamps deleted) | 121.7 s, all pass; immediate re-run **0.14 s, 0 units** |
| Second worktree, cold, **no shared cache** (fresh peer worktree at `1c5ac0af`, empty `CCACHE_DIR`) | 1,498.4 s; 852 compiles, 0 hits |
| Second worktree, cold, **shared warm cache** (clean lane build with the cache the peer filled; `CCACHE_BASEDIR`=each worktree root) | **489.8 s; 850 compiles, 743 hits / 107 misses (87.4%)** |

Every one of the 107 remaining misses has a known cause:
- **69: absolute tree-path `-D` values.** These are VixenApp/editor/test fixture and shader paths (`VIXEN_SHADER_SOURCE_DIR`, `GLSL_RAYMARCH_SPV`, `VIXEN_SDI_OUTPUT_DIR`, …; 72 commands carry such a definition). They are deliberate, valid absolute paths; prefix maps do not rewrite `-D` values. One of them is `RuntimeCachePaths.cpp`, by design. Moving them into one TU each, as was done for the runtime-cache path, is follow-up work.
- **38: TBB built differently in once-configured and reconfigured trees.** This is a pre-existing defect; see STOPs.

The direct comparison of the merged peer against the pre-merge cache gave 154/850 hits (18.1%), all from entries written in this session. This is a measurement-environment effect, not a tree difference: ccache hashes `LANG`, `LC_ALL`, `LC_CTYPE` and `LC_MESSAGES`. The earlier session exported `LC_CTYPE=C.UTF-8`; this one does not. I reproduced the exact pre-merge manifest key for `miniz.c` by setting `LC_CTYPE=C.UTF-8`. Without it, the lane and the peer produce identical keys. See the queuemem note below.

## Functional witness (merged tree, `build/after`)

- **All 22 `*_check` targets** pass on a forced run (`merged-checks.log`). They also passed inside the clean rebuild (`merged-lane-cold-shared.log`). `git status` shows no generated-file drift.
- **`ctest -L RenderGraph`:** **1,320 selected, 0 failed** (11 skipped), 158.1 s, run serially under a login shell with `VK_ICD_FILENAMES` set (`merged-ctest-rendergraph.log`).
- **`ctest -L SVO`:** 748 labeled, 747 run: **one failure, the accepted one**: `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` with `M4d_Output_IsPassthrough: recipe gradient capability mismatch: 94` (opcode 94, T-1449). 8 skipped, 1 disabled, 50.1 s (`merged-ctest-svo.log`).
- **Captures with `DISPLAY` unset:** **11/11 passed** with `DISPLAY`, `WAYLAND_DISPLAY`, `VK_ICD_FILENAMES`, `VK_LAYER_PATH` and `LD_LIBRARY_PATH` unset: both fixture producers (editor and HUD), the 4 `EditorToggleUndoCapture` and 3 `HudRenderCapture` assertions, `HeadlessUiGraph` and `HeadlessCornellGraph` (`merged-capture-no-display.log`).

## Where this meets other lanes

- **vixinfra (T-1461, T-1480, T-1505, …):** its FetchContent default, labels, aggregate target, capture fixtures and runtime-cache location are kept as they are. This lane changed only how the runtime-cache path reaches the compiler and when staging runs.
- **queuemem (T-1584)** owns the generic ccache configuration. To get the cross-worktree hit rate measured above, the shared configuration needs:
  - one `CCACHE_DIR` shared by all VIXEN worktrees, sized well above 5 GB (one cold VIXEN build is ≈0.4 GB of cache).
  - `CCACHE_BASEDIR` set to *each worktree's own root* (the measurement set it per build). A single common parent also works, as long as every worktree sits under it.
  - A normalized locale for compiler invocations, for example `LANG=C.UTF-8 LC_ALL=C.UTF-8` with `LC_CTYPE`/`LC_MESSAGES` unset, applied in the queue or launcher. Otherwise caches split by agent harness. Proposed for consolidation.
  - No `hash_dir` change is needed for Release; it matters only for `-g` builds, where the prefix maps already cover the debug paths.

## STOPs and remaining items

- **STOP — TBB linkage depends on configure count.** `dependencies/native` fetches oneTBB before the later dependencies (googletest, miniz, glfw, rmlui) declare `option(BUILD_SHARED_LIBS OFF)`.
  - On a tree's first configure, `BUILD_SHARED_LIBS` is undefined, so oneTBB defaults to **shared with LTO** (`-flto=auto`).
  - On every reconfigure, the cached `OFF` wins, and TBB becomes **static** (`__TBB_DYNAMIC_LOAD_ENABLED=0`, `__TBB_SOURCE_DIRECTLY_INCLUDED=1`), with oneTBB's "static library is discouraged" warning.

  Results therefore depend on how many times a tree was configured, and the two kinds of tree share no TBB cache entries (38 compiles). Choosing the linkage is a design decision, so I did not change it. Options:
  - (a) Pin static: `set(BUILD_SHARED_LIBS OFF)` scoped around `FetchContent_MakeAvailable(TBB)`. This matches every long-lived tree and every witness run so far.
  - (b) Pin shared: matches oneTBB's guidance and the "copy TBB DLL" step in `application/main/CMakeLists.txt`.

  **Recommendation: (b), shared**, scoped to TBB only. It is oneTBB's supported mode, and the DLL-copy step already assumes it. The GPU suites should be re-witnessed after the switch.
- Windows/MSVC and the Windows→WSL bridge paths are preserved but not functionally witnessed on this Linux host.
- The merged leaf-touch full-build compile was a ccache miss (3.82 s), while the targets-only repeat was a hit (1.0 s). That TU's only cache entry had been written under the earlier session's locale keys (see above), so the first touch stored a new entry and the second used it. The unit counts are the target either way.

## CONSOLIDATION ISSUES

- proposed: VIXEN Linux compiler-cache discovery misses installed ccache
- proposed: Queue watchdog cancels active native builds when a measurement runner captures output
- proposed: VIXEN reconfigure overwrites a worktree-local FetchContent cache unless its environment override is repeated
- proposed: Isolated VIXEN worktrees lack the required CodeGraph query index
- proposed: VIXEN worktrees need manual reuse of the identical provisioned Vulkan SDK and windowing payloads
- proposed: Shared ccache keys diverge between agent harnesses because ccache hashes locale variables
- A peer worktree's configure fails without `-DVIXEN_SCHEMA_CATALOG=<undertow>/core/src/Undertow.Authoring/Schema/schemas.json` (AppFlow/ViewNounId checks). This is documented behavior for standalone VIXEN, but every fresh lane worktree has to rediscover the path.
