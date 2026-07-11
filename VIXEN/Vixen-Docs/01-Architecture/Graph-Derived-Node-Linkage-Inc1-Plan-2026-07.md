# Graph-Derived Node Linkage — Implementation Plan (Inc-1: Prove the mechanism on VixenApp)

> **For agentic workers:** use the project's standard build/test gate
> (`vixen-build-policy` skill) between every task — this touches CMakeLists.txt and link
> behavior directly, so a green full build + green RenderGraph test suite is the correctness
> bar for every step, not just the last one.

**Goal:** Prove the whole graph-derived-linkage mechanism end-to-end on the one real consumer
(`VixenApp`) — per-node OBJECT libraries + a generated manifest + scoped linkage — with **zero
behavior change** to what actually renders, and a measurable (even if modest today) reduction in
linked node object code. Do not delete any node. Do not touch `RegisterAllNodes()` semantics.

**Architecture:** Pure build-system + one small generation script. No node source code changes,
no RenderGraph runtime changes. `RenderGraphNodes` stops being a single STATIC target; node
`.cpp` files become individual OBJECT libraries; a generated CMake variable list (derived from
`AddNode<T>` call sites in `application/main/source/graph/Build*.cpp`) drives which OBJECT
libraries `VixenApp` actually links.

**Tech Stack:** CMake 3.24+ (OBJECT libraries + generator expressions), a small CMake-script or
Python manifest-extraction step (decision point, see Task 2), C++23/MSVC/Ninja preset
(`vixen-ninja`).

**Spec:** [[Graph-Derived-Node-Linkage-Spec-2026-07]]. **Direction:** [[Graph-Derived-Node-Linkage-Direction-2026-07]].
**Branch:** `feat/graph-derived-node-linkage` (create from `main`).

---

## Plan series

This is **Inc-1 of an open-ended epic** — later increments (editor/headless binaries, per-node
granularity refinement, verification tooling) are NOT planned yet; they depend on Inc-1's
measured results and on `vixen_editor` (or another consumer) actually gaining its own graph
builder. Do not pre-plan them; revisit after Inc-1 lands per the spec's §5 scope note.

| Milestone | Scope | Gate |
|---|---|---|
| **M1** | Baseline measurement: capture pre-change `VixenApp.exe`/`.pdb` size + full-relink time, 3x each for noise floor | numbers recorded in this doc's Progress Log before any code changes |
| **M2** | Convert `RENDERGRAPH_NODE_SOURCES` (53 nodes) into 53 OBJECT library targets via a `foreach` loop; `RenderGraphNodes` facade links ALL of them (no scoping yet) | build green, `test_node_self_registration` still ≥53, byte-identical `VixenApp.exe` (mechanically same object code, just repackaged) |
| **M3** | Manifest extraction: script that scans `application/main/source/graph/Build*.cpp` for `AddNode<XNodeType>(` and emits the used-node-type list at configure time | manifest correctness: manual diff against the 45-node union computed in the spec (§1) matches exactly |
| **M4** | Scoped linkage: `VixenApp` links only the manifest's OBJECT libraries (+ transitively required non-node deps unchanged); `RenderGraphNodes` (unscoped, all-53) stays available for tests | build green, `VixenApp` live-render gate passes (visual/behavior unchanged), `.exe`/`.pdb` size measurably smaller than M1 baseline, tests still link all 53 and stay green |
| **M5** | Docs + progress log + measured before/after numbers written up in the spec's §1 table (or a follow-up note) | vault updated, epic status set to reflect Inc-1 DONE |

Opus (or equivalent) validates M2 and M4 specifically — those are the two milestones with real
risk of silently breaking linkage (M2: a node accidentally dropped from the OBJECT-library
loop; M4: the manifest scoping accidentally excluding a node `VixenApp` actually needs, which
would surface as a `throw std::runtime_error("Node type not registered: ...")` at graph-build
time — a loud runtime failure per the spec §3, so the live-render gate is the real safety net
here, not just eyeballing).

---

## Task breakdown

### M1 — Baseline measurement
- [ ] Full clean build of `vixen-ninja` preset (through `build.bat all`), record wall-clock and
      confirm exit green.
- [ ] Record `VixenApp.exe` and its `.pdb` size (bytes) — 3 consecutive from-scratch relinks
      (touch a trivial app source file, rebuild, no clean) to get a noise floor on link time.
- [ ] Record current `RegisterAllNodes()`-derived count via `test_node_self_registration`
      (expect ≥53, confirms nothing already regressed).
- [ ] Write all of the above into this doc's Progress Log before touching CMakeLists.txt.

### M2 — Per-node OBJECT libraries (no scoping yet — mechanical repackaging only)
- [ ] In `libraries/RenderGraph/CMakeLists.txt`, replace the single `RENDERGRAPH_NODE_SOURCES`
      list's consumption (currently folded into the `RenderGraphNodes` STATIC target) with a
      `foreach(node_src IN LISTS RENDERGRAPH_NODE_SOURCES)` loop that derives a target name per
      node (e.g. strip path/extension → `RenderGraphNode_CameraNode`) and calls
      `add_library(RenderGraphNode_<Name> OBJECT <that .cpp>)`, propagating the same
      `target_include_directories`/`target_link_libraries` (PUBLIC deps: `RenderGraphCore`,
      glfw, rmlui_core, freetype, `RMLUI_STATIC_LIB`) that `RenderGraphNodes` currently sets —
      each OBJECT library needs its own include/compile-definitions since they're now
      independent targets, not TUs folded into one static lib.
- [ ] Change `RenderGraphNodes` from `add_library(RenderGraphNodes STATIC ${RENDERGRAPH_NODE_SOURCES})`
      to an `INTERFACE` (or empty `STATIC`, whichever CMake generator-expression story is
      cleaner — decide during implementation) target that PUBLIC-links every
      `RenderGraphNode_<Name>` OBJECT library — i.e., "all nodes" becomes the *default*
      composition, so nothing downstream of the facade changes yet.
- [ ] Remove the now-unnecessary whole-archive linking
      (`$<LINK_LIBRARY:WHOLE_ARCHIVE,RenderGraphNodes>` / `/WHOLEARCHIVE` / `--whole-archive`)
      from wherever `RenderGraph/CMakeLists.txt` currently applies it **only if** OBJECT
      libraries are confirmed to not need it (OBJECT library members are unconditionally
      included when the OBJECT library is linked — no stripping to defeat, per spec §2.3 — but
      verify this empirically with `test_node_self_registration` before deleting the workaround,
      don't assume from documentation).
- [ ] Full rebuild green. `test_node_self_registration` still reports ≥53. `VixenApp` still
      builds and boots (functional check, not yet a size-diff check — M2's point is "same
      behavior, different packaging").

### M3 — Manifest extraction
- [ ] Decide extraction mechanism per spec §2.4: try the simplest thing first — a CMake
      `file(STRINGS ... REGEX "AddNode<([A-Za-z]+)>")` (or equivalent) over the fixed list
      `application/main/source/graph/Build*.cpp`, run at configure time, writing
      `${CMAKE_BINARY_DIR}/generated/node_manifest.cmake` with a `set(VIXEN_APP_USED_NODE_TYPES ...)`
      list. Escalate to a small standalone script (Python or reusing kernel-codegen tooling)
      ONLY if the regex proves fragile against real call-site formatting (multi-line template
      args, etc.) — check the actual files first, don't pre-guess.
- [ ] The extracted node-TYPE names (`CameraNodeType`) need mapping to their OBJECT-library
      target names (`RenderGraphNode_CameraNode`) — decide the naming convention now (type name
      minus `Type` suffix matches file basename in this codebase's convention; verify this holds
      for all 53 before relying on it as a blanket rule).
- [ ] Manual verification: diff the extracted list against the 45-node union already computed
      in spec §1 (recomputed via the file-header-include grep) — must match exactly. If it
      doesn't, the discrepancy is data (which mechanism is more accurate — `#include` vs
      `AddNode<T>` call sites), not necessarily a bug; investigate and record which is right in
      the Progress Log.

### M4 — Scoped linkage for VixenApp
- [ ] In `application/main/CMakeLists.txt`, replace `VixenApp`'s link against the `RenderGraph`
      facade's node linkage with a targeted link against only the manifest-listed
      `RenderGraphNode_<X>` OBJECT libraries (still via `RenderGraphCore` + the facade for
      everything non-node, per spec §2.3's "facade becomes a thin wrapper" — decide the exact
      CMake mechanics: likely a new `vixen_link_used_nodes(VixenApp)` function in a
      `cmake/VixenNodeLinkage.cmake` helper, reading the generated manifest and looping
      `target_link_libraries`).
- [ ] Full clean build green.
- [ ] **Live-render gate**: run `VixenApp.exe`, confirm the default 3-body scene renders
      identically to before this increment (same body count, same visual output) — this is the
      actual safety net for "did the manifest under-scope something," per spec §3's discussion
      of the loud-runtime-failure fallback. If it throws `"Node type not registered"`, that
      pinpoints exactly which node the manifest missed — fix the extraction, don't hand-patch
      the manifest.
- [ ] Confirm `libraries/RenderGraph/tests/*` still link the full unscoped `RenderGraphNodes`
      facade (all 53) and stay green — the test suite is explicitly NOT going through the
      manifest-scoping path (spec §2.3).
- [ ] Measure `VixenApp.exe`/`.pdb` size and relink time (same methodology as M1, 3x), compare
      against the M1 baseline. Record the actual delta — expect modest (~15%, per spec §1's
      45/53 usage) but non-zero.

### M5 — Docs + writeup
- [ ] Update this plan doc's Progress Log with final M1 vs M4 numbers.
- [ ] Update [[Graph-Derived-Node-Linkage-Spec-2026-07]] status banner
      (📐 SPEC → mark Inc-1 implemented, if the vault's convention for that exists — check
      Known-Issues.md/other epics' pattern for how "spec implemented, epic continues" is
      denoted).
- [ ] Update [[RenderGraph-System.md]] §9 (Library Structure & Node Registration) with a new
      subsection documenting the OBJECT-library + manifest scoping, following §9.5's existing
      "Build-granularity results" table format — add a **link-granularity** results row/table
      alongside the existing compile-granularity one.
- [ ] Note explicitly in the writeup that RT-core-cluster nodes (`AccelerationStructureNode`,
      `RayTracingPipelineNode`, `TraceRaysNode`) are now unlinked-by-default from `VixenApp` and
      will re-enter automatically (no manifest edit needed) once
      [[RT-Core-Optional-Acceleration-Spec-2026-07]] wires them into `BuildRenderGraph.cpp`.

---

## File structure (Inc-1)

- **Modify** `libraries/RenderGraph/CMakeLists.txt` — OBJECT library loop, facade restructure,
  whole-archive removal (if verified safe).
- **Create** `cmake/VixenNodeLinkage.cmake` — manifest-extraction step + `vixen_link_used_nodes()`
  helper function.
- **Modify** `application/main/CMakeLists.txt` — call `vixen_link_used_nodes(VixenApp)` instead
  of (or in addition to, during transition) the current facade link.
- **No changes** to any `libraries/RenderGraph/src/Nodes/*.cpp`, `NodeRegistration.h`,
  `NodeRegistration.cpp`, or `RegisterAllNodes()` — this increment is pure build-system.

**Build:** `cd /mnt/c/cpp/VBVS--VIXEN && cmd.exe /c build.bat all vixen-ninja` (see
[[vixen-build-policy]] for lock/queue/target-filter usage — this plan's iterative CMake changes
are a good candidate for the `build.bat build vixen-ninja VixenApp`-style scoped target filter
added this cycle, to avoid relinking the whole graph every M2/M4 iteration).
**Run RenderGraph tests:** the `test_node_self_registration` and `test_pass_group_node_smoke`
binaries under `build/libraries/RenderGraph/tests/`.
**Live gate:** run `VixenApp.exe` directly, visually confirm the default 3-body scene, per the
standing [[live-verification-authoritative-for-gpu-work]] rule — static CMake review has
repeatedly missed real linkage/runtime bugs in past epics; this one is no exception.

---

### Progress Log

*(empty — Inc-1 not yet started)*
