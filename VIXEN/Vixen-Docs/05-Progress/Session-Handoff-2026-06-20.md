---
tags: [session-handoff, rendergraph, build-system, descriptors, picking]
date: 2026-06-20
branch: main
supersedes: Session-Handoff-2026-06-19.md
---

# Session Handoff — 2026-06-20

**Branch:** `main` (build-decoupling work merged in; feature branch deleted)
**Status:** All work merged + **pushed** to `origin/main`. Build GREEN · RenderGraph suite 50/51
(2 documented baseline fails) · GUI `VIXEN.exe` **visually validated, validation-clean**.

Continues `Session-Handoff-2026-06-19.md` (which left M1 done, M2–M5 pending). This session
finished the whole RenderGraph node build-decoupling plan, merged it, fixed a descriptor VUID,
and visually validated the app.

> **Build with the `vixen-ninja` preset** (`cmd.exe /c _ninja_preset_build.bat`), NOT
> `cmake -B build` (VS-generator trap — see friction.md / the 06-19 handoff).

---

## What this session produced

### A. RenderGraph build-decoupling: M2–M5 + M4 (all done, merged)
Plan: `docs/superpowers/plans/2026-06-19-rendergraph-node-build-decoupling.md` (status banner = all done).
Design documented in `Vixen-Docs/01-Architecture/RenderGraph-System.md` §9.

- **M2** — split the monolithic `RenderGraph` lib into `RenderGraphCore` (engine, zero node deps)
  + `RenderGraphNodes` (the ~40 nodes) + `RenderGraph` (INTERFACE facade). Editing a node never
  recompiles Core. (Prereq: removed a dead node-config leak from `TypedConnection.h`.)
- **M3** — every node `.cpp` self-registers via `VIXEN_REGISTER_NODE` into a manifest;
  `RegisterAllNodes()` replays it. `RenderGraphNodes` is linked **whole-archive** (else the
  registrars are stripped — `tests/test_node_self_registration.cpp` guards this). App + benchmark
  both dropped their hand-maintained registration lists.
- **M4** — split `VulkanGraphApplication.cpp` (1838→564 lines): graph construction moved to
  `application/main/source/graph/Build{RenderGraph,UIGraph,InstancingDemoGraph}.cpp`, each with
  only its subgraph's node includes. A node-config edit recompiles only the wiring subgraph TU.
- **M5** — measured + documented. Results: node `.cpp` edit → 1 TU; node-config edit → 5 TUs;
  Core + app-lifecycle TU never recompile on a node change.

### B. Descriptor VUID fix (shaderCounters / binding 8)
`VUID-vkCmdDispatch-None-08114` in the GUI app was **`shaderCounters` (binding 8)**, not
idOutputImage. Its wiring in `BuildRenderGraph.cpp` was mis-gated behind `#if USE_COMPRESSED_SHADER`,
though the non-compressed (default) shader also statically writes binding 8. Moved the binding-8
connection out of the `#if`. `VIXEN.exe` now runs with **zero validation errors**.
(Root-caused with temp instrumentation in `DescriptorSetNode`, since reverted.)

### C. Repo cleanup
Removed two accidentally-committed garbage files (botched `mkdir/cp` artifacts with WSL-mangled
names under `EventBus/` and `logger/`) and one-shot session scripts/logs.

### D. Visual validation (user-confirmed)
`VIXEN.exe` launched on the real AMD GPU under WSL: 41 node types self-registered, 30-node graph
built + compiled, device/window/swapchain created, **rendered the Cornell-box scene cleanly**
(128³, 1601 nodes / 1256 bricks / 299666 voxels — the intended default; hosts override via
`VIXEN_SCENE`). A click registered a pick HIT → **GPU picking (binding-9 idOutputImage readback)
works end-to-end**. Graceful shutdown on window close.

---

## Commits (all on `main`, pushed to `origin/main`)
`3ec2e6f3` TypedConnection node-config leak removed · `ec86171b` M2 split ·
`c1ee6889`+`f7635a39`+`b81064c0` M3 · `6b7cd3f9` M5 docs · `de4f9f1a` M4 ·
`a3f1500b` garbage-file cleanup · `b2f18808` **shaderCounters binding-8 fix** ·
`ac050704` benchmark-VUID docs · (+ session doc commits).

---

## Outstanding / known issues
- **Benchmark `idOutputImage` (binding 9) VUID** — `BenchmarkGraphFactory` wires no pick target,
  so the shader's binding-9 write hits an unbound descriptor. **Accepted / not fixed** (benchmark
  doesn't use picking). Documented in `04-Development/Benchmark-Troubleshooting.md`. To clear it,
  mirror the app: add a `PickIdTargetNode` + `gatherer binding 9` (Execute).
- **`StructSpreaderNode` is dead code** — `.cpp` fully commented out (`CreateInstance` undefined),
  intentionally left unregistered. Decide delete-or-implement.
- **Baseline test fails (unchanged, pre-existing):** `test_scene_generators` (5), `test_voxel_octree`
  (7) — see `04-Development/Known-Test-Failures.md`. Not regressions.

---

## Next steps (open)
1. (Optional) Fix the benchmark binding-9 VUID (above) for validation-clean benchmark runs.
2. Resolve `StructSpreaderNode` (delete vs implement).
3. Resolve the deferred generator/octree test gaps (separate from this work).

*Generated 2026-06-20 by Claude Code. `main` pushed to `origin/main`; tree clean.*
