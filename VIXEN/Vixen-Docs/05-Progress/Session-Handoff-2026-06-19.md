---
tags: [session-handoff, rendergraph, build-system, testing]
date: 2026-06-19
branch: claude/rendergraph-node-build-decoupling
---

# Session Handoff — 2026-06-19

**Branch:** `claude/rendergraph-node-build-decoupling` (off `main` @ `90c07d37`)
**Focus:** Node-as-DLL / build-granularity refactor — but the session front-loaded prerequisites: fixed the WSL build environment, then triaged + fixed the RenderGraph test suite. Refactor M1 done; M2–M5 remain.
**Status:** Build GREEN (via Ninja preset) · Suite green EXCEPT 2 documented deferred items · 4 commits, tracked tree clean.

> **⚠️ READ FIRST — how to build.** Do NOT use `cmake.exe -B build` from WSL: it picks the Visual Studio generator, which is non-incremental (no-op rebuild recompiles ~120 TUs) and silently ignores sccache. Use the committed **`vixen-ninja` preset** instead (see Continuation Guide). This is logged in `~/.claude/friction.md`.

---

## What this session produced

### A. Two external docs validated + corrected GDD (in `~/Downloads`, outside the repo)
The user's two DeepSeek docs (`VIXEN analysis.txt`, `Moddable Voxel Rendering Architecture.md`) were validated against the actual code. **Both DeepSeek conversations never read the source** (confessed in their own thinking logs) — so their "evidence in the codebase" is README-inference, accurate where the README covered it and fabricated otherwise. A code-verified, status-tagged corrected GDD was written to `C:\Users\liory\Downloads\VIXEN-GDD-corrected.md` (Appendix A lists every correction with `file:line`). Memory saved: `deepseek-analysis-docs-not-code-grounded`.

Key corrections: no per-node JSON manifest (all compile-time C++ macros); macro is `REGISTER_COMPILE_TIME_TYPE` not `REGISTER_RESOURCE_TYPE`; connections use typed compile-time slot handles (NOT "string wiring"); connection type-compat is validated at graph-build/runtime (`DirectConnectionRule`), NOT by `constexpr`; **no graph serialization/loader/`.vxdelta`/license-metadata exists** (graphs are imperative C++) — so the whole modding pipeline is greenfield, not "extensions"; 32 registered node types (README says "19+").

### B. Node build-decoupling investigation + plan
The user's "single node as DLL" idea was investigated against the real build. Conclusion: it bundles two goals — (1) build granularity [build-system problem] and (2) DLL plugin nodes [runtime modding]. DLLs are the wrong tool for (1) and collide with VIXEN's compile-time-typed connections. **Option A** (build decoupling, no DLLs) was chosen. Plan: `VIXEN/docs/superpowers/plans/2026-06-19-rendergraph-node-build-decoupling.md` (M1–M5).

### C. Commits this session (all verified green via Ninja preset)
| Commit | What |
|--------|------|
| `d6e87ee5` | **Build-env fix** — `VIXEN/CMakePresets.json` `vixen-ninja` preset (Ninja+sccache+/Z7+PRE_TEST). Delivered the dominant granularity win: node-header edit **119 → 5 TUs**, no-op = 0. |
| `c4b087f2` | Fixed 3 stale binding-rule tests in `test_connection_rule` → match shipped Variadic routing (109/109). |
| `b7f627b8` | Fixed 6 more stale tests + created `Vixen-Docs/04-Development/Known-Test-Failures.md`. |
| `6b7edefb` | **M1** — deleted dead `RegisterBuiltInNodeTypes` (+ its 34 node includes + declaration). |

---

## Files Changed (committed)

| File | Change | Description |
|------|--------|-------------|
| `VIXEN/CMakePresets.json` | Created | `vixen-ninja` preset (the build-env fix) |
| `VIXEN/libraries/RenderGraph/tests/test_connection_rule.cpp` | Modified | 3 binding-rule assertions → Variadic (51927757) |
| `VIXEN/libraries/RenderGraph/tests/Nodes/test_swap_chain_node.cpp` | Modified | `INPUT_COUNT 9→7` (FR-3 266bfa3b) |
| `VIXEN/libraries/RenderGraph/tests/test_group_dispatch.cpp` | Modified | role `Dependency→Execute` (Sprint6.3 938a95d1) |
| `VIXEN/libraries/RenderGraph/tests/Core/test_timer.cpp` | Modified | timing asserts → OS-robust (contract, not exact ms) |
| `VIXEN/libraries/RenderGraph/tests/Nodes/test_push_constant_gatherer_node.cpp` | Modified | fixed 2 test-internal math/setup bugs |
| `VIXEN/libraries/RenderGraph/src/Core/NodeTypeRegistry.cpp` | Modified | M1: removed dead function + 34 includes |
| `VIXEN/libraries/RenderGraph/include/Core/NodeTypeRegistry.h` | Modified | M1: removed declaration |
| `VIXEN/Vixen-Docs/04-Development/Known-Test-Failures.md` | Created | deferred real-discrepancy registry |

**Untracked (intentionally not committed):** `_ninja_*.bat` (env-specific build helpers), `build-ninja/` (build dir), `build_*.log` (scratch), and the plan doc (commit it if continuing — see below).

---

## Outstanding / Deferred Issues

### Deferred REAL discrepancies (do NOT weaken — see `Known-Test-Failures.md`)
- [ ] **`test_scene_generators` (5 fail)** — generators miss the README research densities (Urban target 90%, produces ~28%; Cave density param **inverted**; Cornell-64 walls don't scale). This is a **generator/spec gap**, not a stale test; weakening corrupts the benchmark. **Decision needed:** fix generators or revise the spec.
- [ ] **`test_voxel_octree` (7 fail)** — deprecated `SparseVoxelOctree::GetNodeCount/GetMemoryUsage/SerializeToBuffer` read the legacy `nodes_` vector; build defaults to ESVO (`esvoNodes_`) so they return 0. **Off the live `LaineKarrasOctree` path.** **Decision needed:** make accessors ESVO-aware, or delete the deprecated class (+7 tests; it's still `#include`d by `VoxelGridNode`).

### Not a failure
- `test_ui_hud_smoke` (2) — CWD artifact; passes under `ctest` / from the binary dir. Headless by design.

---

## Design Decisions

1. **Build env: Ninja preset, not VS generator.** Root cause of "non-incremental + no sccache" was the VS generator (ignores `CMAKE_CXX_COMPILER_LAUNCHER`, mis-tracks codegen). Chose Ninja + `/Z7` (sccache can't cache `/Zi` PDBs; C1041 under parallel cl.exe) + `PRE_TEST` discovery. Encoded as a preset so it's reproducible and `build/` (VS) stays as a fallback.
2. **Stale tests corrected, not weakened.** Each of the 9 fixed tests asserted a contract that a *deliberate, git-proven* refactor superseded (binding routing 51927757; swapchain FR-3 266bfa3b; accumulation role Sprint6.3 938a95d1) or had a test-internal bug. The fixes cite the proving commit. `scene_generators` + `voxel_octree` were NOT weakened because they catch real gaps.
3. **Refactor = build decoupling (Option A), no DLLs.** DLL-per-node forfeits compile-time slot-handle checking and adds ABI cost; the granularity goal is a build-system problem. DLL plugin nodes remain a separate, later modding concern.

---

## Insights

- **The Ninja fix alone delivered most of the user's granularity goal** (119→5 TU blast radius). M2/M3 are incremental on top.
- **`RegisterBuiltInNodeTypes` was dead code** — the app registers its own types via `VulkanGraphApplication::RegisterNodeTypes` → `EngineContext` `registerNodeTypes` hook. Typed `AddNode<T>()` requires the type pre-registered (`typeRegistry->Get<T>()`, throws otherwise).
- **`NodeTypeRegistry` is per-`EngineContext`** (not a global singleton) — matters for M3 self-registration (needs a global manifest replayed into the instance registry + whole-archive so static registrars aren't stripped).
- **`GenerateTypeId()` is a global counter** — node-type IDs are unstable across runs; use `GetTypeName()` as the key.
- `sccache --show-stats` from WSL may not reflect a cmd.exe-launched build's server — trust incremental TU counts over hit-rate.

---

## Next Steps (prioritized)

### Immediate (resume the refactor)
1. [ ] **M2 — split `RenderGraphCore` | `RenderGraphNodes`** (+ back-compat `RenderGraph` interface alias). Verified earlier: Core has **zero** concrete-node dependency once M1's dead file was cleaned. Plan §Milestone 2. Verify: editing a node must NOT recompile `RenderGraphCore`.
2. [ ] **M3 — node self-registration** (`VIXEN_REGISTER_NODE` macro + global manifest + `RegisterAllNodes`), whole-archive the Nodes lib (static-lib stripping is the #1 risk — there's a TDD anchor test in the plan), then drop the app's hand-maintained `RegisterNodeTypes` list. Plan §Milestone 3.

### Short-term
3. [ ] M4 (optional) — split app graph construction into per-subgraph TUs.
4. [ ] M5 — re-measure (Ninja, not TU-count-on-VS) + document the Core/Nodes split.

### Separate decisions (not blocking the refactor)
5. [ ] Resolve `test_scene_generators` (fix generators vs revise density spec).
6. [ ] Resolve `test_voxel_octree` (fix accessors vs delete deprecated `SparseVoxelOctree`).

---

## Continuation Guide

### Build & test (CRITICAL — use the Ninja preset)
Builds run from WSL but need the MSVC env, so go through a `.bat` (vcvars + preset) via `cmd.exe`. The session left helper scripts at the repo root:
```bash
cd /mnt/c/cpp/VBVS--VIXEN
# configure + build (the bat calls vcvars64.bat then `cmake --preset vixen-ninja` + `--build --preset vixen-ninja`):
cmd.exe /c "C:\cpp\VBVS--VIXEN\_ninja_preset_build.bat"
# run a test exe (Ninja: no Debug/ subdir):
build-ninja/libraries/RenderGraph/tests/test_connection_rule.exe --gtest_brief=1
```
If recreating the `.bat`: `call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"` then `"C:\Program Files\CMake\bin\cmake.exe" --preset vixen-ninja` (cwd = `VIXEN/`, where `CMakePresets.json` lives), then `--build --preset vixen-ninja`.

### Gate for the refactor
- Build green + **no NEW test failures** beyond the documented baseline (`scene_generators` 5 + `voxel_octree` 7). Re-check that set doesn't grow; node/registration tests must stay green.

### Where to start
- Plan: `VIXEN/docs/superpowers/plans/2026-06-19-rendergraph-node-build-decoupling.md` — **note its build/measure commands reference the old VS-generator path; substitute the `vixen-ninja` preset.** Start at Milestone 2. (Plan doc is currently untracked — `git add` it on the branch if continuing.)
- M2 touches only `VIXEN/libraries/RenderGraph/CMakeLists.txt` (the plan gives the exact two-library restructure).

### Watch out for
- **Never** `cmake.exe -B build` (VS-generator trap). Always the Ninja preset.
- M3 self-registration in a static lib **will be stripped** without whole-archive — the plan's anchor test catches it.
- Don't weaken `scene_generators`/`voxel_octree` to "go green" — they're real, deferred by decision.

---

*Generated: 2026-06-19 by Claude Code (session-summary). Branch has 4 commits ahead of `main`; not pushed.*
