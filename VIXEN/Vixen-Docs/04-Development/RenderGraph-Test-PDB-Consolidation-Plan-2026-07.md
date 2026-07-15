# RenderGraph Test PDB Consolidation Plan (2026-07)

## Ground Truth (measured 2026-07-14)

- `libraries/RenderGraph/tests/` (the whole tree, including `CompileTimeResourceSystem/`) builds
  **117 separate executables** (116 currently buildable; `test_fail_scenario_registry` is gated off
  by `VIXEN_FAIL_SCENARIOS=OFF` in this build's cache).
- **116 confirmed `.pdb` files on disk, totaling 17.28GB** (`18,549,792,768` bytes), measured under
  `/mnt/c/cpp/VBVS--VIXEN/build/ninja/libraries/RenderGraph/tests/`. Combined with the `.exe` files
  and `CMakeFiles/` intermediates, `RenderGraph/tests` alone is ~22GB — the dominant share of the
  whole `build/` tree's ~32GB footprint.
- **Root cause** (confirmed via research, see citations below): `CMakePresets.json` sets
  `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT: Embedded` (`/Z7`) globally — a deliberate, already-validated
  tradeoff for sccache cache-hit rate (`Worktree-Build-Artifact-Accumulation-Audit-2026-07.md` §3;
  `/Zi` separate-PDB mode is non-cacheable under sccache and causes `C1041` under parallel `cl.exe`).
  `/DEBUG:FULL` (MSVC's non-deprecated default) "moves all private symbol information from individual
  compilation products (object files and libraries) into a single PDB" per executable
  ([Microsoft Learn, `/DEBUG`](https://learn.microsoft.com/en-us/cpp/build/reference/debug-generate-debug-info?view=msvc-170)).
  Since `RenderGraph`/`RenderGraphNodes` are built as **static libraries**, every executable that
  links them independently re-collates the ENTIRE linked-in debug info of those libraries into its
  own private PDB — `link.exe` has no reachability pruning for debug info (unlike `/OPT:REF` for
  code) and no mechanism to share one PDB across multiple executables
  ([CMake `PDB_NAME` docs](https://cmake.org/cmake/help/latest/prop_tgt/PDB_NAME.html) confirm
  link-time PDB is per-target, no shared-PDB-across-targets feature exists). Confirmed uniform: every
  sampled target across all 7 source files that links `RenderGraph` sits at **~150-160MB PDB**
  regardless of that target's own actual test-code size; targets that DON'T link `RenderGraph`
  (`test_type_system.cmake`'s header-only tests, `CompileTimeResourceSystem/test_recursive_validation`
  which links `Core::Core` only) sit at 2-3MB.
- **`/DEBUG:FASTLINK` is NOT a viable lever** — deprecated and removed starting Visual Studio 2026
  per Microsoft Learn; a dead end regardless of sccache-safety, not worth pursuing.
- **The one real, CMake-expressible fix**: consolidate many small `add_executable()` gtest targets
  into fewer, larger executables. This is a genuine reduction in total PDB bytes, not just
  redistribution — RenderGraph's redundant per-exe debug-info collation happens once per SURVIVING
  PDB, not once per original target. GoogleTest supports multiple `.cpp` files and multiple
  `gtest_discover_tests()` calls in one target with no special handling required beyond avoiding
  symbol collisions across merged translation units.
- **Why this matters beyond one machine**: per user (2026-07-14), this same ~40GB-per-full-build cost
  multiplies across every worktree in a multi-agent session — the exact accumulation pattern the
  existing `Worktree-Build-Artifact-Accumulation-Audit-2026-07.md` already documented hitting 100GB+
  and stalling the host at 100% full. This consolidation is a build-configuration fix that reduces
  the PER-BUILD cost, complementing (not replacing) that doc's build-artifact CLEANUP guidance
  (deleting `build/` between worktree phases) — cleanup reclaims disk after the fact, this reduces
  how much there is to reclaim in the first place.

## Full target inventory (surveyed 2026-07-14)

| Source file | Targets | Safe-to-merge (bare `gtest_main`+`RenderGraph`, no other libs/includes/defs/POST_BUILD) | Special-complication | Notes |
|---|---|---|---|---|
| `tests/CMakeLists.txt` (parent, 1256 lines) | 51 | 34 | 17 | 1 disabled (`if(FALSE)`, don't count/touch); `test_node_self_registration` is a whole-archive-linkage guard — keep isolated regardless of its own bare link surface |
| `test_type_system.cmake` | 3 | 0 | 3 | All add glm/gli/Vulkan includes + `cxx_std_23` + `/FS`; `test_array_type_validation` uses raw `add_test`, not `gtest_discover_tests` |
| `test_graph_systems.cmake` | 3 | 2 | 1 | `test_slot_task` additionally links `ResourceManagement` |
| `test_core_systems.cmake` | 9 | 9 | 0 | All identical bare pattern, some with an optional `../include` dir |
| `test_critical_nodes.cmake` | 41 | 0 (borderline — uses a shared `RENDERGRAPH_TEST_COMMON_LIBS` var, itself conditional on 6 other targets existing, not a clean 2-lib surface) | 41 | Mostly GPU/shader-dependent: custom-command `.spv` dependencies (~18 targets), POST_BUILD DLL copies (SVO/GaiaVoxelWorld/AppFlow/stb/TBB), `DISCOVERY_MODE PRE_TEST`/`DISCOVERY_TIMEOUT 120`. **DO NOT touch in Milestone 1** — highest risk, smallest safe subset, deserves its own later pass if pursued at all. |
| `test_fail_scenarios.cmake` | 1 | 0 | — | Gated off (`VIXEN_FAIL_SCENARIOS=OFF`), not currently built, not in scope |
| `test_voxel_systems.cmake` | 2 | 2 | 0 | Identical bare pattern |
| `CompileTimeResourceSystem/CMakeLists.txt` | 8 | 0 | 8 | None use GTest (raw `add_test`); `test_recursive_validation` deliberately links `Core::Core` only (isolation is intentional per its own comment) — do not fold into a `RenderGraph`-linked binary |

**Milestone 1 scope (this plan's actual target): 47 safe-to-merge targets** — 34 (parent) + 2
(graph_systems) + 9 (core_systems) + 2 (voxel_systems). All confirmed bare `GTest::gtest_main` +
`RenderGraph` link surface, no extra includes/defs/POST_BUILD steps, no load-bearing isolation
requirement. This is ~40% of the total target count and (since every RenderGraph-linked target's PDB
sits at a uniform ~150-160MB regardless of its own code size) a comparable fraction of the 17.28GB.

**Explicitly OUT of scope for this plan**: `test_critical_nodes.cmake`'s 41 targets (GPU/shader
complications, highest regression risk, would need its own dedicated pass with device-availability
testing); `test_type_system.cmake`'s 3 and `CompileTimeResourceSystem`'s 8 (non-GTest / special
compile-feature requirements, low target count doesn't justify the risk); `test_node_self_registration`
(whole-archive guard, must stay exactly as-is); `test_fail_scenario_registry` (gated off, not built).

## Grouping strategy for Milestone 1's 47 targets

Group by the existing `# ====` section header themes already present in the source (preserves the
file's own logical organization, makes the diff reviewable) into **6-8 executables of 4-8 tests
each**, not one giant binary — keeps individual group build/link times reasonable and keeps
`ctest -R <pattern>` selective test runs meaningful. Exact grouping to be finalized by the
implementer against the CURRENT file content (this doc's inventory is a survey, not a byte-exact
spec) but should follow natural boundaries already visible in the section comments, e.g.:
- Core RenderGraph basics (`test_rendergraph_basic`, `test_rendergraph_dependency`, `test_typednode_helpers`, `test_lightingconfig_parity`)
- Shader/lighting mirrors (`test_brdf_mirror`, `test_traceworld_mirror`, `test_vndf_mirror`, `test_sampling_compile_gate` — NOTE: this one has a conditional ShaderManagement link in its `else()` branch only; verify before grouping, may need to stay isolated or move to its own small group)
- Dispatch/scheduling (`test_group_dispatch`, `test_task_queue`, `test_multidispatch_integration`, `test_prediction_error_tracker`, `test_task_profile`, `test_resource_access_tracker`, `test_wave_scheduler`)
- VirtualTask suite (`test_virtual_task`, `test_virtual_resource_access_tracker`, `test_task_dependency_graph` — NOTE: `test_tbb_virtual_task_executor`/`test_virtual_task_integration` have POST_BUILD TBB DLL copy, exclude from this group or replicate the POST_BUILD step onto the merged target)
- View/blob contract (`test_view_blob`, `test_view_store`, `test_view_blob_file`, `test_blob_view`, `test_view_wire_roundtrip`, `test_view_wire_soa_roundtrip`, `test_typed_accessor_emitter`, `test_view_container_builder` — several need `application/main/include`; group those together, keep the ones that don't separate)
- Sync/barrier (`test_barrier_types`, `test_frame_sync_scheduler`, `test_frame_sync_node_timeline`, `test_pass_group_schedule`, `test_pass_group_node_smoke`)
- `test_core_systems.cmake`'s 9 and `test_graph_systems.cmake`'s 2 safe ones: 1-2 groups
- `test_voxel_systems.cmake`'s 2: fold into an existing group or keep as its own small pair

**Preserve exactly, per merged group**: any per-target extra include dir/compile definition must
propagate to the WHOLE group's `add_executable()` (verify no group mixes a target needing
`application/main/include` with one that doesn't in a way that causes an accidental header collision
— check with the implementer's own build, don't assume from this survey). `gtest_discover_tests()`
is called once per resulting executable and discovers ALL tests linked into it; no test name/label
changes as long as each `TEST(Suite, Case)` macro's own name is untouched (only the containing binary
changes, not gtest's internal registration).

## Verification requirements (non-negotiable, every milestone)

1. Full clean configure + build of the `RenderGraph` tests directory (standalone or via the
   super-build, whichever the implementer's environment supports — WSL/Ninja per this session's own
   established pattern if Windows-side is unavailable to the dispatched worker).
2. `ctest` (or direct binary invocation) proving every individual `TEST(Suite, Case)` that existed
   before the merge still runs and passes after — a name-for-name count comparison (before: N gtest
   cases across 47 binaries; after: same N gtest cases across fewer binaries), not just "build
   succeeded."
3. Measure the actual PDB byte reduction (`du -sh` before/after on the affected subset) and report
   the real number — do not estimate.
4. Confirm `test_node_self_registration` and all 70 out-of-scope targets are byte-for-byte
   untouched (git diff should show zero changes outside `tests/CMakeLists.txt`,
   `test_graph_systems.cmake`, `test_core_systems.cmake`, `test_voxel_systems.cmake`).

## Milestone Map

- [x] **Milestone 1**: Consolidate the safe-to-merge targets across `tests/CMakeLists.txt`,
  `test_graph_systems.cmake`, `test_core_systems.cmake`, `test_voxel_systems.cmake` into grouped
  executables. Full build + test verification, PDB byte-reduction measurement. DONE 2026-07-14,
  commit `6de8902f` on `feat/rendergraph-pdb-consolidation`.
- [x] **Milestone 2**: Consolidate `test_critical_nodes.cmake`'s 41 GPU/shader-dependent targets
  (highest-risk file, deferred out of Milestone 1). Full build + test verification, PDB
  byte-reduction measurement. DONE 2026-07-14, commit `26fd12e1` on
  `feat/rendergraph-pdb-consolidation-m2`.
- [x] **Milestone 3**: Consolidate `CompileTimeResourceSystem`'s 7 `RenderGraph`-linked, non-gtest
  standalone targets (each with its own `main()`) into 1 executable via mechanical
  `main()`-to-named-function rename + a new shared entry point. Full build + functional
  verification, PDB byte-reduction measurement. DONE 2026-07-15, commit `4ea873dd` on
  `feat/rendergraph-pdb-consolidation-m3`.

(`test_type_system.cmake`'s 3 targets remain out of scope: measured at ~2-3MB each, ~7MB total —
header-only, no `RenderGraph` link, negligible disk win, not worth the touch. `test_recursive_validation`
in `CompileTimeResourceSystem` also remains untouched — links `Core::Core` only, ~2MB, deliberately
isolated per its own pre-existing comment.)

## Progress Log

**Milestone 1 — DONE 2026-07-14.**

Re-derivation against current file content (per this doc's own "verify, don't trust blindly"
instruction) found **48 safe-to-merge targets, not 47** — the survey's "Grouping strategy" section
was illustrative, not exhaustive (it didn't name `test_node_parameter_manager`,
`test_accumulation_gather`, `test_connection_concepts`/`test_connection_rule`,
`test_passthroughstorage_handles`, or the 4 `Nodes/*.cpp`-sourced targets registered directly in
the parent `CMakeLists.txt`, even though all are bare link surface). Used the corrected 48-target
inventory as the actual Milestone 1 scope.

**44 of 48 merged into 12 grouped executables; 4 stayed standalone** — 3 due to real collisions
found only by actually linking/running the merged binaries (not visible from a static read of the
source):
- `test_connection_concepts` vs `test_connection_rule`: file-scope `struct MockBindingRef` with
  DIFFERENT member layouts (2 fields vs. 3) — an ODR violation if merged.
- `test_graph_topology` vs `test_graph_lifecycle_hooks` (their original pairing in
  `test_graph_systems.cmake`): both define an out-of-line static
  `MockNodeType MockNode::mockType("MockNode");` — a genuine duplicate-symbol LNK2005 if merged.
  `test_graph_topology` moved into the new `test_rendergraph_syncbarrier` group instead (no
  collision there); `test_graph_lifecycle_hooks` tried pairing with `test_connection_rule` next.
- `test_connection_rule` vs `test_graph_lifecycle_hooks`: both define a redundant `int main()` —
  ALSO LNK2005 ("main already defined"), caught by an actual Windows/MSVC build, not static
  review. With every other bare-link-surface neighbor ruled out by one collision or another, both
  ended up standalone.
- `test_connection_concepts` additionally has its own disqualifying property, independent of the
  MockBindingRef issue above: its hand-rolled `main()` never calls `RUN_ALL_TESTS()` (pre-existing
  in this file, not something this milestone changed) — it just prints static_assert-pass banners.
  Whichever `main()` a merged binary keeps is decided at link time, so this file silently
  monopolizes `main()` for the WHOLE binary it's linked into. An initial merge attempt folded it
  into `test_rendergraph_nodeconn` alongside 3 other files; running the resulting binary revealed
  those 3 files' real `TEST()` cases were registered via gtest's static initializers but NEVER
  EXECUTED, because `RUN_ALL_TESTS()` was never called. Caught by actually running the merged
  binary and comparing gtest_list_tests output, not by reading the diff — reverted, `test_connection_concepts`
  now stands alone.
- `test_core_systems.cmake` needed a 3-way split (not the originally-planned 2-way): `test_timer`,
  `test_loop_manager`, and `test_node_logging` each define their own redundant `int main()`
  (identical 3-line boilerplate to what `GTest::gtest_main` already supplies) — at most one per
  merged binary, so 9 files split into 3 groups of 3 instead of 2 groups of 5+4.

**Verification — real Windows/MSVC build, not WSL/GCC estimation** (this plan doc's own "Ground
Truth" PDB measurement was itself from a real Windows build; `/Z7` embedded debug info is an
MSVC-only concept — WSL/GCC produces no `.pdb` files at all, so WSL could only serve as a
functional cross-check, not the PDB measurement):
- **BEFORE**: 48 target `.pdb` files, **7,958,290,432 bytes (7.5 GiB)**, measured from this
  machine's live outer checkout at `/mnt/c/cpp/VBVS--VIXEN/build/ninja` (a real, same-day MSVC
  build already on disk — the same one this plan doc's own "Ground Truth" section cites).
- **AFTER**: 16 target `.pdb` files (12 merged + 4 standalone), **2,712,985,600 bytes (2.6 GiB)**,
  measured from this worktree's own fresh `build/ninja` after a full `build.bat all` reconfigure +
  build (583/583 targets, 0 failures).
- **Reduction: 5,245,304,832 bytes (4.89 GiB), 65.9%** for the Milestone-1-scoped subset.
- **Test-case parity**: extracted every `Suite.Case` name via `--gtest_list_tests` from all 48
  original binaries (BEFORE) and all 16 resulting binaries (AFTER, excluding the always-untouched
  `test_slot_task`), normalized, and diffed as SETS (not line-by-line, which false-positives on
  any single-line shift). Result: **809 unique test cases before, 809 after — 0 missing, 0
  new/unexpected.** (809, not 833/836 raw-line counts, because `test_rendergraph_basic` +
  `test_rendergraph_dependency` originally both compiled `test_rendergraph_dependency.cpp`, a
  pre-existing duplication the merge correctly de-duplicates by listing that file once.)
- Independent WSL/GCC cross-check: full functional build of all 17 WSL-buildable targets, 0
  compile/link errors; ran every CPU-runnable merged group directly (`--gtest_brief=1`) — all
  PASS (`test_rendergraph_coresystems_c` shows 52 passed + 1 pre-existing GPU-required SKIP,
  unrelated to this change). `test_rendergraph_nodes` needs a real Vulkan device to RUN (confirmed
  by the Windows build instead) but its `--gtest_list_tests` output matches the Windows count
  (16 tests) exactly.
- `git diff --stat`: only the 4 expected files changed (`tests/CMakeLists.txt`,
  `test_graph_systems.cmake`, `test_core_systems.cmake`, `test_voxel_systems.cmake`).

Committed as `6de8902f` on branch `feat/rendergraph-pdb-consolidation` in worktree
`.claude/worktrees/rendergraph-pdb-consolidation` (not yet merged to main).

**Opus validator — APPROVED 2026-07-14.** Independently re-derived every claim above rather than
trusting the implementer's report: read the actual diff and confirmed only the 4 expected files
changed; set-diffed every `.cpp` across all 4 files (main vs HEAD) and confirmed zero test files
lost (the one apparent "new" file, `test_executeonly_behavior.cpp`, was already compiled into
`test_typednode_helpers` via a pre-existing `target_sources` line on main, just relocated into the
merged group — not new code); confirmed all 4 kept-standalone targets exist as their own untouched
`add_executable` blocks and that `test_connection_concepts` genuinely never calls `RUN_ALL_TESTS()`
(rc=0, no gtest summary — the exact defect the implementer's merge-and-revert caught); confirmed
`test_node_self_registration` is byte-identical to main's version and `test_critical_nodes.cmake`/
`test_type_system.cmake`/`test_fail_scenarios.cmake`/`CompileTimeResourceSystem/CMakeLists.txt` all
diff empty against main. Ran an independent fresh clean WSL/Ninja configure+build of all 16 targets
(exit 0) and ran them: 15/16 pass cleanly, 1 (`test_rendergraph_nodes`'s `BandwidthAbMeasurementTest`
suite) hangs on WSL software-Vulkan — confirmed as a pre-existing environment property unrelated to
the merge, not a regression. Spot-checked test-case parity by NAME (not just count) for 3 of the 48
original targets no longer existing standalone (TimerTest 23/23, WaveSchedulerTest 15/15,
BarrierTypes 5/5) — all present and passing in their claimed new groups. PDB byte numbers could not
be independently re-measured (MSVC `/Z7`-only, this validator's WSL/GCC toolchain produces no PDBs)
— explicitly flagged as a real tooling limitation, not silently assumed correct.

**Milestone 2 — DONE 2026-07-14.**

Scoped to the single highest-risk file Milestone 1 deferred: `test_critical_nodes.cmake`'s 41
GPU/shader-dependent targets (`.spv` custom-command shader deps, POST_BUILD DLL copies, a shared
conditional `RENDERGRAPH_TEST_COMMON_LIBS` link-surface variable, `DISCOVERY_MODE PRE_TEST`/
`DISCOVERY_TIMEOUT 120`). Grouped conservatively — 41 targets -> 15 executables (10 merged groups
+ 5 kept standalone), a smaller consolidation ratio than Milestone 1's 48->16 given the file's
higher structural risk, exactly as the plan anticipated ("acceptable to consolidate a smaller
fraction... if the file's structure doesn't support more").

**Real collision caught only by an actual link, not static review** (same lesson class as
Milestone 1, different mechanism): 8 files each `#define STB_IMAGE_WRITE_IMPLEMENTATION`
(single-header library implementation guard) to instantiate `stb_image_write.h` once per binary.
A first grouping attempt (by shared `body_instance_raymarch_spv` shader dependency alone) put
2-3 such files in one binary and hit LNK2005 duplicate `stbi_write_*` symbols on the first build
attempt. Fixed by re-splitting so every group/standalone target has at most one STB-impl file —
a plain grep for struct/class/`main()` collisions (the class of check Milestone 1 used) would NOT
have caught this, since it's a preprocessor-guarded header-implementation include, not a C++
declaration.

**Kept standalone (5) and why**: `test_baked_vs_virtual_parity` (own hand-rolled `main()`, no spv
dependency, distinct compile-defs), `test_appflow_editor_toggle_render` (only target in the file
linking `AppFlow`), `test_procedural_recipe_render` (own POST_BUILD TBB DLL copy step),
`test_shell_revalidate_node` (own separate `shell_derive_spv` custom target — a different shader,
`ShellDerive.comp`, than `body_instance_raymarch_spv`'s `BodyInstanceRayMarch.comp`),
`test_recipe_pool_render` and `test_mip_fallback_render` (each has its own STB-impl file with no
remaining non-STB partner after the collision-driven split — 6 named here since the split left one
extra standalone beyond the 5 originally anticipated by link-surface alone, net result still 5
standalone after accounting for one of these landing in a merged group's final form).

**Verification — real Windows/MSVC build, independently re-run by the Opus validator**, not just
the implementer's self-report:
- **BEFORE** (independently measured by the validator from `main`'s own existing build tree,
  41/41 PDBs present): **6,800,371,712 bytes (6.33 GiB)**. (Implementer's own BEFORE measurement:
  6,803,410,944 bytes — 0.045% apart, normal PDB build-to-build jitter, not a discrepancy.)
- **AFTER** (validator's own fresh `build.bat all` reconfigure+build of the worktree, exit 0, zero
  build failures): **2,501,103,616 bytes (2.33 GiB)** across 15 PDBs — **exact match** to the
  implementer's reported AFTER figure.
- **Reduction: 4,299,268,096 bytes (4.00 GiB), 63.22%** (validator's independent figure; implementer
  reported 4.01 GiB / 63.2% — matches within rounding).
- Confirmed mechanism directly: each `/Z7` PDB sits at a near-constant ~166MB regardless of source
  count (validator's 15 AFTER PDBs span only 165.7-168.9MB despite 1-8 source files each) —
  consistent with Milestone 1's root-cause finding that cutting N targets to M executables removes
  (N-M) whole-library debug-info re-collations, a genuine reduction not a redistribution.
- **Test-case parity**: 41 unique `.cpp` sources on `main`, 41 in the merged file, set-diff EMPTY
  both directions (0 dropped, 0 new) — every source file landed in exactly one resulting executable.
  Static `TEST()`-macro count is 376 (not the implementer's runtime-enumerated 361; the gap is
  `#if`-gated tests not compiled into every configuration) — parity holds regardless, since the
  merge changes zero `.cpp` source content and preserves every per-target compile-definition
  untouched. Validator spot-checked real test names across the merge boundary for 5 formerly-
  standalone targets (`DeviceNodeTest`/`DeviceNodeIntegration`, `OctreeConfigSdiParity`,
  `HitRecordReadbackTest`, `EditorDocumentRenderTest`, `SelectionId`/`SelectionSet`) — all present
  in their claimed new groups.
- `git diff main..26fd12e1 --stat`: only `test_critical_nodes.cmake` changed (214+/600-), zero
  other files touched — confirmed by both implementer and validator independently.
- `DISCOVERY_MODE PRE_TEST`/`DISCOVERY_TIMEOUT 120` preserved on every resulting executable that
  carries a GPU-dependent source (20 sources land correctly); the 5 CPU-only default-discovery
  executables match the sources that used default discovery on `main`. Zero regressions.
- `test_node_self_registration` and all Milestone-1/other out-of-scope files
  (`tests/CMakeLists.txt`, `test_graph_systems.cmake`, `test_core_systems.cmake`,
  `test_voxel_systems.cmake`, `test_type_system.cmake`, `test_fail_scenarios.cmake`,
  `CompileTimeResourceSystem/CMakeLists.txt`) confirmed byte-identical to `main`.

Committed as `26fd12e1` on branch `feat/rendergraph-pdb-consolidation-m2` in worktree
`.claude/worktrees/rendergraph-pdb-m2` (not yet merged to main at time of this entry).

**Opus validator — APPROVED 2026-07-14.** Independently re-derived every claim above from a fresh
Windows/MSVC build of the validator's own (able to directly re-measure real `/Z7` PDB bytes, unlike
Milestone 1's validator which was WSL/GCC-only and had to flag that as a limitation). One
non-blocking doc nit found and fixed post-approval: the in-file header comment originally said "41
targets grouped into 21 executables (10 merged groups + 11 kept standalone)" — stale arithmetic
from an earlier grouping attempt; corrected to match the actual committed result (15 executables,
10 merged + 5 standalone) before merge. No functional/structural issues found.

**Program running total after Milestone 2**: 89 of 117 original RenderGraph test targets
consolidated (48 in Milestone 1 + 41 in Milestone 2) down to 31 executables (16 + 15), combined
measured reduction 9,544,572,928 bytes (8.89 GiB) on the affected subsets. Remaining out of scope:
`test_type_system.cmake` (3) and `CompileTimeResourceSystem` (8) — 11 non-GTest/special-compile-
feature targets, low count, not pursued per Milestone 1's original scoping rationale.

**Milestone 3 — DONE 2026-07-15.**

Re-scoped on user request ("finish all the small targets, no reason to waste space") after measuring
real PDB sizes for the remaining 11 out-of-scope targets: `test_type_system.cmake`'s 3 are ~2-3MB
each (~7MB total, header-only, no `RenderGraph` link) — genuinely not worth touching, left out of
scope. `CompileTimeResourceSystem`'s 8 targets split 7-vs-1: 7 link `RenderGraph` and sit at the
usual ~165MB/PDB (~1.16GB total); the 8th, `test_recursive_validation`, deliberately links
`Core::Core` only (~2MB) per its own pre-existing isolation comment — left untouched.

**Structurally different from Milestones 1-2**: the 7 targets are NOT gtest — each is a small
standalone ad-hoc diagnostic program (leftover debug/scratch tooling from developing the
`CompileTimeResourceSystem` feature, `std::cout` prints + some `static_assert`s, 8-96 lines) with
its OWN `int main()`. A CMake-only regrouping (Milestones 1-2's approach) was impossible — every
single one of the 7 would collide on `main()`. Required an actual code change: each file's
`main()` mechanically renamed to a distinct function
(`run_test_minimal`/`run_test_compile_time_cache`/`run_test_traits_debug`/`run_test_registration`/
`run_test_simple_traits`/`run_test_syntax_check`/`run_test_type_check`), function bodies otherwise
byte-identical, plus one new file (`test_compile_time_resource_system_main.cpp`) holding the single
real `main()` that calls all 7 in sequence. Merged into one `add_executable(
test_compile_time_resource_system ...)` with the union of the 7's link/include requirements
(`RenderGraph`, `Vulkan_LIBRARIES`, `gli` — needed by 3 of the 7, included since ANY needed it).

**Verification — real Windows/MSVC build, independently re-run by the Opus validator**:
- **BEFORE** (implementer's measurement, real Windows/MSVC build from `main`, 7 separate PDBs):
  **1,156,321,280 bytes**. Not independently re-derived by the validator (would have required a
  second full build of `main` purely to re-confirm; explicitly flagged as a validator limitation,
  not silently assumed correct).
  - **AFTER** (validator's own fresh full `build.bat all` build of the worktree, exit 0, 585/586
  targets, 0 failed): **162,017,280 bytes** for the merged PDB — **exact match** to the
  implementer's own reported AFTER figure. `test_recursive_validation.pdb` confirmed still isolated
  at 1,937,408 bytes (~1.9MB), untouched.
- **Reduction: 994,304,000 bytes (~0.93 GiB), 85.99%** — the largest per-target ratio of the three
  milestones (vs. 65.9% and 63.2%), consistent with going from 7 executables down to exactly 1
  (removing 6 of 7 whole-library debug-info re-collations, the highest N-to-M ratio of any
  milestone in this program).
- **Functional verification, both implementer and validator**: built+ran the 7 original binaries
  from `main` first to capture real BEFORE stdout; built+ran the merged binary, confirmed identical
  output (the "COMPILE-TIME RECURSIVE VALIDATION WITH CACHE" banner, multiple
  `IsRegisteredType<VkSwapchainKHR>` diagnostic lines, `sizeof(VkSwapchainKHR)=8`, etc. all present)
  except one expected, benign MSVC artifact: an anonymous local struct's compiler-generated mangled
  name changed from referencing `main` to `run_test_compile_time_cache` — a direct, harmless
  reflection of the rename, not a behavior change.
- **Diff correctness (validator, most important check for this milestone)**: read every one of the
  7 renamed files' diffs directly — each is exactly 1 insertion/1 deletion (the function-signature
  line only), every context line unchanged, confirming the rename did not alter behavior. File-scope
  `static_assert`s outside any function body (2 in the old `test_traits_debug.cpp`, 4 in
  `test_simple_traits.cpp`) confirmed preserved verbatim. New main file confirmed to declare and
  call all 7 renamed functions exactly once each, none missing, none duplicated. CMake merge
  confirmed correct: all 8 sources present, `gli` correctly included (verified 3 of 7 originals
  actually needed it), `cxx_std_23` + `/FS` preserved, exactly one `add_test`.
- `git diff main..4ea873dd --stat`: exactly `CompileTimeResourceSystem/CMakeLists.txt` + the 7
  original `.cpp` files (1 line each) + 1 new `.cpp` file added — nothing else, confirmed by both
  implementer and validator independently.

Committed as `4ea873dd` on branch `feat/rendergraph-pdb-consolidation-m3` in worktree
`.claude/worktrees/rendergraph-pdb-m3`, merged to `main`.

**Opus validator — APPROVED 2026-07-15.** Independently re-derived every claim from a fresh full
Windows/MSVC build (not target-scoped, unlike part of the implementer's own verification) — exact
match on the AFTER PDB byte count, confirmed via direct diff reading that every rename was
behavior-preserving, ran the merged binary itself and confirmed real output. One disclosed,
non-blocking limitation: did not independently re-derive the BEFORE figure (would need a second
full build of `main`); AFTER half and the rename-correctness check (the part most likely to hide a
real regression) were both fully independent. No issues found.

**Program running total after Milestone 3**: 96 of 117 original RenderGraph test targets
consolidated (48 + 41 + 7) down to 32 executables (16 + 15 + 1), combined measured reduction
10,538,876,928 bytes (9.81 GiB) on the affected subsets. Remaining out of scope:
`test_type_system.cmake`'s 3 targets (~7MB total, header-only, no `RenderGraph` link — genuinely
negligible, confirmed by direct measurement, not pursued) and `test_recursive_validation` (~2MB,
deliberately isolated). 21 of 117 original targets remain as their own executables — all either
already cheap (not linking `RenderGraph`) or explicitly load-bearing isolation
(`test_node_self_registration`, `test_recursive_validation`, the 4 Milestone-1 standalone survivors,
etc.).
