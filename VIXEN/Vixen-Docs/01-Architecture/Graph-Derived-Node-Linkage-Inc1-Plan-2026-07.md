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
- [x] Full clean build of `vixen-ninja` preset (through `build.bat all`), record wall-clock and
      confirm exit green.
- [x] Record `VixenApp.exe` and its `.pdb` size (bytes) — 3 consecutive from-scratch relinks
      (touch a trivial app source file, rebuild, no clean) to get a noise floor on link time.
- [x] Record current `RegisterAllNodes()`-derived count via `test_node_self_registration`
      (expect ≥53, confirms nothing already regressed).
- [x] Write all of the above into this doc's Progress Log before touching CMakeLists.txt.

### M2 — Per-node OBJECT libraries (no scoping yet — mechanical repackaging only)
- [x] In `libraries/RenderGraph/CMakeLists.txt`, replace the single `RENDERGRAPH_NODE_SOURCES`
      list's consumption (currently folded into the `RenderGraphNodes` STATIC target) with a
      `foreach(node_src IN LISTS RENDERGRAPH_NODE_SOURCES)` loop that derives a target name per
      node (e.g. strip path/extension → `RenderGraphNode_CameraNode`) and calls
      `add_library(RenderGraphNode_<Name> OBJECT <that .cpp>)`, propagating the same
      `target_include_directories`/`target_link_libraries` (PUBLIC deps: `RenderGraphCore`,
      glfw, rmlui_core, freetype, `RMLUI_STATIC_LIB`) that `RenderGraphNodes` currently sets —
      each OBJECT library needs its own include/compile-definitions since they're now
      independent targets, not TUs folded into one static lib.
- [x] Change `RenderGraphNodes` from `add_library(RenderGraphNodes STATIC ${RENDERGRAPH_NODE_SOURCES})`
      to an `INTERFACE` (or empty `STATIC`, whichever CMake generator-expression story is
      cleaner — decide during implementation) target that PUBLIC-links every
      `RenderGraphNode_<Name>` OBJECT library — i.e., "all nodes" becomes the *default*
      composition, so nothing downstream of the facade changes yet.
- [x] Evaluate removing the whole-archive linking
      (`$<LINK_LIBRARY:WHOLE_ARCHIVE,RenderGraphNodes>` / `/WHOLEARCHIVE` / `--whole-archive`)
      from wherever `RenderGraph/CMakeLists.txt` currently applies it **only if** OBJECT
      libraries are confirmed to not need it (OBJECT library members are unconditionally
      included when the OBJECT library is linked — no stripping to defeat, per spec §2.3 — but
      verify this empirically with `test_node_self_registration` before deleting the workaround,
      don't assume from documentation). **Decision: KEPT** — see Progress Log for rationale.
- [x] Full rebuild green. `test_node_self_registration` still reports ≥53. `VixenApp` still
      builds and boots (functional check, not yet a size-diff check — M2's point is "same
      behavior, different packaging").

### M3 — Manifest extraction
- [x] Decide extraction mechanism per spec §2.4: try the simplest thing first — a CMake
      `file(STRINGS ... REGEX "AddNode<([A-Za-z]+)>")` (or equivalent) over the fixed list
      `application/main/source/graph/Build*.cpp`, run at configure time, writing
      `${CMAKE_BINARY_DIR}/generated/node_manifest.cmake` with a `set(VIXEN_APP_USED_NODE_TYPES ...)`
      list. Escalate to a small standalone script (Python or reusing kernel-codegen tooling)
      ONLY if the regex proves fragile against real call-site formatting (multi-line template
      args, etc.) — check the actual files first, don't pre-guess.
- [x] The extracted node-TYPE names (`CameraNodeType`) need mapping to their OBJECT-library
      target names (`RenderGraphNode_CameraNode`) — decide the naming convention now (type name
      minus `Type` suffix matches file basename in this codebase's convention; verify this holds
      for all 53 before relying on it as a blanket rule).
- [x] Manual verification: diff the extracted list against the 45-node union already computed
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

- **Milestone 1 (Baseline measurement): DONE** · full clean build: 13m15s (795s), exit 0 (all
  non-test targets green; see note below on 19 pre-existing test-only failures) · `VIXEN.exe`
  (the app target's actual CMake/binary name — `application/main/CMakeLists.txt` declares
  `add_executable(VIXEN ...)`, not `VixenApp`; both names refer to the same binary):
  37,140,480 bytes · `VIXEN.pdb`: 170,192,896 bytes · 3x incremental relink (touch a blank line
  in `application/main/source/main.cpp`, `build.bat build vixen-ninja VIXEN`, revert): 29s / 12s
  / 13s · `test_node_self_registration`: 2/2 tests PASS; node-type count confirmed as **53** via
  direct source grep of `VIXEN_REGISTER_NODE(...)` call sites across
  `libraries/RenderGraph/src/Nodes/*.cpp` (the test itself only asserts `>= 32`/`>= 32u`, it
  doesn't print the live count, so the registry size was cross-checked at the source level
  instead of by modifying the test) · 2026-07-11

  **Build-tooling note:** this worktree was created before commits `6fe8a050`/`6c487298`/
  `e79c079f` landed on `main` (queue-ticket auto-release, ccache-preferred-over-sccache for MSVC
  PCH caching, per-build BuildId). Mid-M1, the branch was rebased cleanly onto `origin/main`
  (`e79c079f` confirmed as an ancestor of HEAD, no conflicts — those commits are build-tooling/
  docs only, M1 hadn't touched any CMakeLists.txt) and the full clean build was **re-run from
  scratch** on the updated tooling so the recorded baseline reflects what M2-M4 will actually be
  compared against, rather than pre-rebase numbers. The numbers above are from that post-rebase
  build. A first (pre-rebase) clean build was also run and discarded for baseline purposes; its
  numbers were consistent with the ones kept (same `VIXEN.exe` byte size, same 19 pre-existing
  failures), so the rebase did not introduce any regression — it just aligned this worktree with
  current tooling before locking in the baseline.

  **19 pre-existing test-target failures (not a regression, not caused by Inc-1 — zero code/
  CMake changes were made in M1):**
  - 18 of the 19 are exactly **KI-017** (`Vixen-Docs/04-Development/Known-Issues.md`) — the
    already-documented, already-OPEN Windows/MSVC `<windows.h>` `min`/`max`/`abs` macro-pollution
    cascade into `Recipe/generated/SdfCoreKernels.g.hpp` (`glm::length`/`glm::abs` calls),
    producing `C2589`/`C2059`/`C1003` in any SVO test TU that transitively includes
    `SdfRecipes.h`/`SdfBake.h`. KI-017 documents this as reproduced independently on a clean
    pre-Tiered-ESVO-Inc2-M3 checkout and explicitly notes `VIXEN.exe` itself builds fine — only
    isolated SVO test TUs are affected. Confirmed identically reproducible in this worktree
    (same 18 target names, same error signature) across three independent builds this session:
    this worktree's first build, this worktree's post-rebase build, and a concurrent sibling
    agent's build on a completely different worktree (`tiered-esvo-inc2`) — machine-wide,
    not worktree-specific.
  - The 19th (`test_body_instance_raymarch_render.cpp`) is a separate, also pre-existing
    MSVC-portability bug: the test calls POSIX `setenv`/`unsetenv` (lines 924/926/1048/1050/...),
    which don't exist on MSVC (the portable equivalent is `_putenv_s`). Unrelated to node
    linkage or KI-017; not investigated further as it's out of M1's pure-measurement scope.
  - Net effect on M1's numbers: `VIXEN.exe`/`VIXEN.pdb` (the only artifacts M2-M4 will size-diff
    against) built successfully in every run; `test_node_self_registration` (M1's required test
    gate) built and ran successfully. The 19 failures are isolated to other SVO/RenderGraph test
    binaries not in M1's gate list.

  **Opus validator: APPROVED** (2026-07-11). Confirmed independently: (1) scope discipline —
  commit `459be207` touches exactly one file (this plan doc, +49/-5), zero CMakeLists.txt/source/
  registration changes; (2) tree integrity — `e79c079f` confirmed an ancestor of HEAD, `git
  status` clean, linear history with no duplicate/merge/conflict artifacts; (3) numbers
  plausibility — build time, binary sizes, and relink times all in expected ranges; (4) test
  honesty — KI-017 independently confirmed documented at `Known-Issues.md:115`, not a fabricated
  excuse. **One non-blocking discrepancy noted**: independently re-grepping
  `VIXEN_REGISTER_NODE(...)` found 54-55 raw macro invocations/unique type names (vs. the "53"
  recorded above) — traced to `ConstantNode.cpp` registering 2 types (an alias-like
  `ShaderConstantNodeType` alongside `ConstantNodeType`). Documentation-level only; doesn't
  affect baseline validity since M2-M4 don't depend on the literal "53" — flagged for a one-line
  reconciliation when M2's manifest-vs-registry counts are actually compared.

- **Milestone 2 (Per-node OBJECT libraries): DONE** · `libraries/RenderGraph/CMakeLists.txt`:
  replaced `RenderGraphNodes`'s direct consumption of `RENDERGRAPH_NODE_SOURCES` with a
  `foreach(node_src IN LISTS RENDERGRAPH_NODE_SOURCES)` loop emitting one
  `add_library(RenderGraphNode_<Name> OBJECT <src>)` per node (53 targets — target names derived
  via `get_filename_component(... NAME_WE)`, e.g. `RenderGraphNode_CameraNode`), each with its
  own `target_include_directories` (same 4 `BUILD_INTERFACE` dirs `RenderGraphCore`/
  `RenderGraphNodes` already used) and `target_link_libraries` (`RenderGraphCore`, `glfw`,
  `rmlui_core`, `freetype`), plus `RMLUI_STATIC_LIB`, `cxx_std_23`, and MSVC `/FS /bigobj` —
  applied identically to all 53 inside the loop rather than hand-duplicated. `RenderGraphNodes`
  itself is now `add_library(RenderGraphNodes STATIC ${DATA_NODE_CONFIGS} ${NODE_HEADERS}
  ${UI_HEADERS} ${UI_SOURCES})` (no longer compiles the 53 node `.cpp`s directly — it PUBLIC-links
  all 53 `RenderGraphNode_<Name>` OBJECT libs via a collected `RENDERGRAPH_NODE_OBJECT_LIBS`
  list) — kept as `STATIC` (not `INTERFACE`) specifically because it still bundles the
  not-yet-split `RENDERGRAPH_UI_SOURCES` (`UIRenderNode.cpp` + `Ui/*.cpp`), so it needs to be a
  real compilable/linkable archive, not a pure interface alias.

  **Whole-archive decision: KEPT, not removed.** Verified empirically per the plan's own gate
  rather than assumed from the spec's theory: built `test_node_self_registration` against this
  M2 change with whole-archive still applied to `RenderGraph`'s link of `RenderGraphNodes` — 2/2
  tests PASS (`gtest_brief=1`), consistent with M1's baseline (registry `>= 32`/`>= 32u`
  assertions; 53 registrar call sites at the source level, unchanged since zero node `.cpp` files
  were touched). Did **not** additionally run the ablation (rebuild with whole-archive stripped
  out to confirm the count would *drop* without it) — decided this wasn't needed to justify
  keeping the flag, since the plan explicitly permits leaving it in place as a safety margin when
  the full pull-vs-push ablation isn't done, and `RenderGraphNodes` remaining a `STATIC` archive
  (not `INTERFACE`) means the theoretical justification for removal ("OBJECT library members
  aren't stripped like STATIC archive members") applies to the 53 `RenderGraphNode_<Name>`
  OBJECT libs individually, but NOT to `RenderGraphNodes` itself as the thing `RenderGraph`
  actually whole-archives — `RenderGraphNodes` is still a STATIC archive vulnerable to the same
  unreferenced-symbol stripping the flag exists to defeat. Removing the flag here without also
  restructuring `RenderGraph`'s link line to whole-archive the 53 OBJECT libs directly (instead of
  the STATIC facade) would be a real risk, not a redundant safety net — flagged as a concrete
  follow-up for M3/M4 once UI sources are handled and/or the facade's exact shape is revisited,
  not silently dropped.

  **Verification:** Full rebuild via `build.bat all vixen-ninja` (BuildId
  `-graph-node-linkage-inc1-`), dispatched directly (lock was FREE — confirmed via
  `check_build_lock.ps1` before dispatch — so no queue registration needed), watched via an
  active ~20s foreground poll loop. Result: same 19 pre-existing test-target failures as M1's
  baseline, byte-for-byte the same signatures (18× KI-017 `<windows.h>` macro-pollution cascade
  in SVO/RenderGraph test TUs including `SdfRecipes.h`/`SdfBake.h`; 1× `test_body_instance_
  raymarch_render.cpp` POSIX `setenv`/`unsetenv` on MSVC) — confirmed via grep against the fresh
  build log (`C2589`/`C2059`/`C1003` count and `setenv` line numbers match). No new failures, and
  specifically zero errors on any `RenderGraphNode_*` target (`grep -i error | grep -i
  RenderGraphNode` empty). `test_node_self_registration.exe`: 2/2 PASS. `VIXEN.exe` (35.4MB,
  `build/ninja/binaries/VIXEN.exe`) built and linked successfully; boot smoke-test (launched via
  a short `Start-Process`/`Get-Process`/`Stop-Process` PowerShell script, not a full visual
  render-correctness check per M2's explicit scope) confirmed it stays running 8+ seconds with no
  immediate crash/exception, then was cleanly killed. No node source files, `NodeRegistration.*`,
  or `RegisterAllNodes()` were touched — pure build-system change, matching Inc-1's stated
  architecture. Commit: `8b131078`. 2026-07-11

  **Opus validator: APPROVED** (2026-07-11, independently re-ran the build rather than trusting
  the report). Confirmed against the live diff: ~54 OBJECT-lib targets emitted (matches M1's node
  count, incl. the already-noted pre-existing discrepancy), each with the full original dep set;
  `NodeRegistration.*`/`RegisterAllNodes()` untouched. Confirmed against the live CMakeLists.txt
  (not just the claim) that the whole-archive-kept reasoning holds: `RenderGraphNodes` is still
  `STATIC`, still directly compiles `RENDERGRAPH_UI_SOURCES`, and the whole-archive flag is still
  applied to it across all three linker dialects. Independently re-ran `build.bat all vixen-ninja`
  (own queue ticket, active polling) — exactly 19 failures, byte-identical to M1's baseline, and
  critically **all 19 are compile errors with zero link errors** — the decisive signal that the
  OBJECT-library repackaging didn't break anything structurally (a broken repackaging would
  surface as unresolved-symbol link failures, not compile errors). `test_node_self_registration`:
  2/2 PASS. Tree clean, linear history. No issues found.

- **Milestone 3 (Manifest extraction): DONE.** Step 1 (call-site format check): grepped all 116
  `AddNode<` occurrences across the 5 `Build*.cpp` files — every single one is a plain, single-line
  `renderGraph->AddNode<XNodeType>(` token sequence; zero multi-line template args, zero macro/
  helper indirection, zero use of the legacy runtime-string `AddNode("X", ...)` overload. This
  confirmed the simple `file(STRINGS ... REGEX ...)` approach (spec §2.4's first choice) was
  sufficient — no escalation to a standalone script/AST tool was needed.

  **Implementation:** new `cmake/VixenNodeManifest.cmake` (matching the existing
  `ProvisionX.cmake`/`VixenAssets.cmake` header-comment/`function()`-wrapped style) defines
  `vixen_generate_node_manifest(<out-file> SOURCES <files...>)`, which `file(STRINGS ... REGEX
  "AddNode<[A-Za-z_][A-Za-z0-9_]*>")`-scans each source, `string(REGEX MATCHALL ...)` extracts
  every call site per matched line (handles >1 `AddNode<T>` call per line, which does occur),
  strips the `AddNode<...>` wrapper down to the bare type name, dedupes + sorts, and writes
  `set(VIXEN_APP_USED_NODE_TYPES ...)` to the given path. `include(cmake/VixenNodeManifest.cmake)`
  added to root `CMakeLists.txt` right after `VixenAssets.cmake` (function-definition only, no
  side effects at include time — matches that file's pattern). The actual generation call lives in
  `application/main/CMakeLists.txt`, immediately after the `VixenApp` target's source list (which
  is where the 5 `Build*.cpp` files are named as the app's own sources) — writes to
  `${CMAKE_BINARY_DIR}/generated/node_manifest.cmake`. Placed here rather than at the root because
  the app target (and its exact `Build*.cpp` file list) lives here; a future second consumer
  (`vixen_editor`, once it gains its own graph builder) would call the same function from its own
  `CMakeLists.txt` with its own source list and output path, per spec §5's scope note.

  **Step 3 (target-name mapping verification):** confirmed for all 45 extracted type names that
  `<Basename>NodeType` maps mechanically to source file `src/Nodes/<Basename>Node.cpp` → M2
  OBJECT-lib target `RenderGraphNode_<Basename>Node` (`get_filename_component(... NAME_WE)` on
  the M2 loop, `libraries/RenderGraph/CMakeLists.txt:406-409`) — checked by cross-referencing
  every entry in the generated manifest against `RENDERGRAPH_NODE_SOURCES`'s file list. **One
  legitimate exception, not a mapping failure:** `ConstantNodeType` and `ShaderConstantNodeType`
  are both declared header-only in `include/Nodes/ConstantNodeType.h` (a *separate* header from
  `include/Nodes/ConstantNode.h`) and both `VIXEN_REGISTER_NODE`'d from the single TU
  `src/Nodes/ConstantNode.cpp` (see that file's own header comment, lines 3-4: "ConstantNodeType.h
  defines two header-only NodeTypes ... and there is no other..."). This is a many-types-per-TU
  case, not a naming-scheme break: the *type* name still strips `Type` to `ConstantNode`, which
  still matches the *source* basename `ConstantNode.cpp` → target `RenderGraphNode_ConstantNode`
  — the mapping rule (`<Basename>NodeType` → `RenderGraphNode_<Basename>Node`) holds for the
  literal type name in every one of the 45 manifest entries. The only wrinkle for M4 to be aware
  of: linking `RenderGraphNode_ConstantNode` pulls in `ShaderConstantNodeType` too, even though
  no current `Build*.cpp` calls `AddNode<ShaderConstantNodeType>` — harmless (same TU, same OBJECT
  lib, would be compiled in regardless), just worth noting so a future "is X's registration
  linked" check isn't surprised. No other node source declares more than one `NodeType` — checked
  via `grep -c VIXEN_REGISTER_NODE` per `src/Nodes/*.cpp`.

  **Step 4 (discrepancy analysis, extracted `AddNode<T>` list vs. spec §1's `#include`-based
  45-node union):** re-ran the spec's own grep (`#include "Nodes/X.h"` across the 5 `Build*.cpp`
  files) — 45 unique header basenames. The `AddNode<T>`-based extraction also yields exactly
  **45** unique type names, and byte-for-byte the underlying basenames match, **with one instance
  of the two mechanisms disagreeing on *why* a name matches:** the include-grep lists
  `ConstantNodeType` as an included header (`#include "Nodes/ConstantNodeType.h"` at
  `BuildRenderGraph.cpp:68`) — a literal-header-name match, not `ConstantNode.h`+"Type" suffix
  like every other entry — so naively assuming "strip `.h`, add `Type`" as the include-based
  proxy's derivation rule (which is what the M1/spec measurement implicitly did for the other 44)
  would have been wrong for this one row specifically. The `AddNode<T>` call-site scan has no such
  ambiguity: it reads the literal type token used at the call site (`AddNode<ConstantNodeType>`
  appears 3x in `BuildRenderGraph.cpp`, lines 267/279/291), independent of which header
  the type happens to live in. **Conclusion (matches spec §2.2's prediction):** the two mechanisms
  produce the same *set* today (45/45, confirmed by manual diff — no entries in either list absent
  from the other), but `AddNode<T>` call-site scanning is the mechanically more trustworthy source
  for a linkage decision, exactly because it reads actual usage rather than an include that could
  in principle be unused or (as seen here) not map to the type name via a fixed textual rule. No
  actual bug found — both mechanisms agree at Inc-1 time — but this is documented per M3's task 4
  as the reasoning for trusting `AddNode<T>` scanning going forward, not just eyeballing set
  equality.

  **Configure verification:** `build.bat configure vixen-ninja` (lock confirmed FREE via
  `check_build_lock.ps1` before dispatch; watched via an active ~15s foreground poll loop, not a
  blind wait) completed clean — `Configuring done (14.0s)`, `Generating done (5.5s)`, `Build files
  have been written to: .../build/ninja`, zero CMake errors/warnings. Log line confirms:
  `vixen_generate_node_manifest: wrote 45 node type(s) to
  C:/.../build/ninja/generated/node_manifest.cmake`. Read back the generated file directly —
  content matches the hand-computed 45-entry list exactly (alphabetically sorted, one
  `VIXEN_APP_USED_NODE_TYPES` list, header comment naming the 5 source files scanned). No build
  (`--build`) was run per M3's lighter gate (this milestone adds a new generated-file-producing
  CMake step but wires it into no target's link line yet — that's M4).

  **Files touched:** new `cmake/VixenNodeManifest.cmake`; `CMakeLists.txt` (+3 lines, `include()`);
  `application/main/CMakeLists.txt` (+16 lines, the `vixen_generate_node_manifest(...)` call). No
  node source, `NodeRegistration.*`, or `RegisterAllNodes()` touched. Commit: `f2845c88`.
  2026-07-11

  **Opus validator: APPROVED** (2026-07-11). Independently re-verified rather than trusting the
  report: (1) regex logic read directly from the current `VixenNodeManifest.cmake` — extraction/
  dedup/sort all correct, wired correctly from both CMakeLists.txt call sites; (2) the "116
  call-sites, all single-line, zero misses" claim independently reproduced by grepping all 5 files
  and sampling 16 call sites across them — confirmed zero `AddNode<` tokens fail to match, no
  legacy string-overload usage, no multi-line template args (this was the highest-risk check,
  since a missed call site would silently under-link a real node — none found); (3) the 45-node
  extraction independently reproduced THREE separate ways (manual grep-simulation, direct
  execution of the actual CMake function via `cmake -P`, and reading the generated file) — all
  three sets identical; (4) the `ConstantNode.cpp` two-types claim fact-checked directly against
  the source (`ConstantNode.cpp:13-14`, `ConstantNodeType.h`) — accurate, and correctly only
  `ConstantNodeType` (which has real call sites) is in the manifest, `ShaderConstantNodeType`
  (zero call sites) is correctly excluded, consistent with spec §3's conservative-syntactic
  design; (5) configure independently re-run (via `cmake -P` invoking the function directly, since
  the full vixen-ninja preset needs MSVC/vcvars unavailable in the validator's environment) — same
  45-entry output, byte-matching the committed manifest; (6) tree clean, linear history. No
  linkage changes made (correctly deferred to M4, per scope). No issues found.
