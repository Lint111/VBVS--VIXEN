# VIXEN documentation audit and SPT import list

**Audit date:** 2026-09-23
**Repository / branch:** VIXEN / `lane-vixendocs`
**Base:** `178b838b3b3594b0e3d7102bb50fc8acccb5de29` (`HEAD == origin/main`)
**Owner request:** “Do a doc pass for vixen as well, it has many features and epochs mid work”.

## Scope and method

Ran the required CodeGraph query first. This worktree has no `.codegraph/`, so the audit used `rg`, direct file reads, current source paths, SPT read commands, and Git ancestry checks. The canonical vault has 341 Markdown files total, 289 outside archive directories, and 175 active-path/status/plan candidates by filename. This pass verified the current-state documents and feature families listed below; it did not claim a complete line-by-line audit of all 175 candidates.

Detailed current-code review covered the indexes and known-issues register; the AppFlow Inc1/Inc2/Inc2b plans; the domain-agnostic multi-channel recipe direction/plan; the Deep-Field mip, residency, stencil, wholesale-admission, and HDR documents; the September Main-Wave, SIMD materialization, and voxel-mutation reports; the June roadmap/backlog; and the Undertow/VIXEN federation note. Remaining candidate families are listed at the end.

SPT was read-only. `harvest all` succeeded for its registered sources, but a direct VIXEN path was rejected as an unknown source; no SPT write command was run. Only the requested CodegenTool workflow was built; no VIXEN product build or test suite was run.

## Existing SPT mapping

- `T-0525` is the open VIXEN documentation audit and already cites `D-0357`.
- `S-0007` is the existing review bucket for the 27 imported Known-Issues tasks. It was created before the original VIXEN issue document was in scope.
- Existing Known-Issues mapping, all still open in SPT:

| SPT task | VIXEN issue | SPT task | VIXEN issue | SPT task | VIXEN issue |
|---|---|---|---|---|---|
| T-0001 | KI-051 | T-0010 | KI-039 | T-0019 | KI-024 |
| T-0002 | KI-050 | T-0011 | KI-038 | T-0020 | KI-021 |
| T-0003 | KI-049 | T-0012 | KI-036 | T-0021 | KI-022 |
| T-0004 | KI-048 | T-0013 | KI-035 | T-0022 | KI-019 |
| T-0005 | KI-046 | T-0014 | KI-034 | T-0023 | KI-016 |
| T-0006 | KI-042 | T-0015 | KI-032 | T-0024 | KI-008 |
| T-0007 | KI-041 | T-0016 | KI-033 | T-0025 | KI-006 |
| T-0008 | KI-040 | T-0017 | KI-027 | T-0026 | KI-005 |
| T-0009 | KI-037 | T-0018 | KI-025 | T-0027 | KI-004 |

- `KI-052` is under **Resolved** in VIXEN's Known-Issues document and its E12 fix is in current VIXEN code, but SPT has no KI-052 item. The apply script adds it as a closed task with the verified fix commit.
- Existing governing SPT records: `D-0315` (R106.1 framework/application split), `D-0319` (R155 fetched dependency), `D-0329` (R155.1 `VIXEN/prod` tag), and `D-0351` (R162.4 managed side is dead). `T-0455` already records the split measurement; `T-0458` remains open for the unreachable VIXEN pin. The apply script adds direct report/ruling anchors to both existing tasks.
- No existing SPT items were found for the current E7–E12 stencil work, E21–E25 wholesale-admission slices, HDR1/HDR2, Main-Wave reconciliation, or the September SIMD and voxel-mutation deliveries.

## Document and feature findings

| Source document and lines | Stated status | Current VIXEN code / SPT check | Classification and import mapping |
|---|---|---|---|
| `VIXEN/DOCUMENTATION_INDEX.md:3-10`; `VIXEN/Vixen-Docs/00-Index/Home.md:12-14`; `VIXEN/Vixen-Docs/00-Index/Quick-Lookup.md:18-28` | Index says July game-renderer/SDF development; Home says Phase K/L complete in Dec 2025 and “Next: Multi-tester data collection”; Quick-Lookup last covers Aug 10 2026. | The code and current records include E12, E21–E25, HDR1/HDR2, and September materialization/reconciliation. Quick-Lookup's E11 result is still useful, but it omits later landed work. | **STALE INDEXES.** Home is obsolete; Quick-Lookup needs the post-August work and KI-052 closure. No SPT index-maintenance task added by this lane. |
| `VIXEN/README.md:60-63`; `VIXEN/Vixen-Docs/05-Progress/Maturation-Backlog-2026-06.md:6,35-39`; `VIXEN/Vixen-Docs/05-Progress/Production-Roadmap-2026.md:6,18-25` | README says the game-renderer boundary and SDF/Recipe remain active. Maturation Backlog is `active`; it says it supersedes the old roadmap. Production Roadmap is also marked `active`. | The user rulings now define a fetched VIXEN framework and a per-application render-graph application. The June backlog predates that split, and the January roadmap describes superseded timeline/physics workstreams. | README is broadly current; the roadmap's `active` label is **STALE**. The June backlog is **PARTIALLY SUPERSEDED** by R106.1/R155/R162.4 and September decisions. |
| `VIXEN/Vixen-Docs/04-Development/Known-Issues.md:16-154,1385-1430` | KI-051 through KI-049 are open; KI-052 is explicitly resolved by E12-T1. | SPT's T-0001…T-0027 map to the older open issues. Current E12 code adds the missing `BUFFER_WRITE_ARRAY` hazard edge; commit `868298e2e9cb2d9e6e827d53ddae69056b744b6b` is an ancestor of VIXEN `origin/main`. KI-027's old `VoxelInjectionQueue` is deleted, but `VoxelInjector` remains. | KI-052 doc/code status agrees; **SPT is missing the closed KI-052 item**. KI-027 remains **PARTIAL/OPEN**, so T-0017 must stay open. The apply script also adds the resolved Undertow report links to T-0001/T-0002/T-0003. |
| `VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc1-Plan-2026-07.md:28-31`; `AppFlow-Framework-Inc2-Plan-2026-07.md:37-53`; `AppFlow-Framework-Inc2b-Plan-2026-07.md:33-52` | Inc1, Inc2, and Inc2b milestones are marked DONE; Inc2b says the scripted editor toggle/undo/redo path is complete. | `VIXEN/libraries/AppFlow/` contains the runtime, state machine, binding store, action stack, editor integration, and tests. Inc2b code commit `d4ce37033cc5b821817689fc09b12613e0f8647a` is an ancestor of `origin/main`. R106.1 and R162.4 change the future consumer/managed-host framing. | **IMPLEMENTED** for native AppFlow Inc1/2/2b. Import as one closed task. Inc3 selector/editor-mode work and managed Undertow migration remain deferred or superseded by the current split. |
| `VIXEN/Vixen-Docs/01-Architecture/Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07.md:3`; `Domain-Agnostic-MultiChannel-Recipe-Output-Plan-2026-07.md:3,155-179` | Direction says pre-plan and awaits planning; separate plan says the full four-increment plan was greenlit and records Increment 1 DONE at Yeroket `6fa9cb2c`, with later Unity verification deferred. | The direction and plan disagree. The cited short SHA is not an object in the current Yeroket checkout, so its ancestor check cannot be established. VIXEN's `recipe-lowering.extensions` has 8 fields per entry; current Yeroket `RecipeLoweringModel.ParseExtensions` requires 11. The requested `recipe_simd_check` aborts before it compares the generated header. | **LIVE/PARTIAL; STATUS DISAGREES ACROSS DOCS.** Treat Inc1's external completion claim as unverified from this checkout. A live epoch/task is proposed for the VIXEN/kernel declaration contract and remaining plan evidence. Owner decision: update the VIXEN declarations to the 11-field schema or restore a documented compatible parser contract. |
| `VIXEN/Vixen-Docs/Deep-Field-Policy-Stencil-Grouping-2026-08.md:3,36-40,156-248` | E1/E2 shipped; stencil storage, tile reduction, and grouped dispatch are labeled DESIGN-FUTURE. | Current code has the shared classifier, E7 per-pixel stencil, E8 multirung fixture, E9/E10 orbital fixtures, E11 tile reduction/skip, and E12 hazard fix. Commits `e335130d`, `188f5cb9`, `4b7195e9`, `c12eaeba`, and `868298e2` are ancestors of VIXEN `origin/main`. E11 remains opt-in after a 63–67% dense-scene slowdown; sparse-scene payoff is not established. | **STALE STATUS / IMPLEMENTED WITH OPEN GATE.** Import E6–E12 delivered work. Keep a live E11 sparse-tile measurement task: the current rig lacks a sparse tile-uniform acceptance scene. |
| `VIXEN/Vixen-Docs/Deep-Field-Residency-Unification-2026-08.md:3,356-423` | `draft`; proposes CSV byte accounting, classifier extraction/CPU twin, residency wiring, then a long-session growth probe. | The C++ classifier is `VIXEN/libraries/SVO/include/CellFootprintRegime.h:39-63`; GLSL twin is `VIXEN/shaders/SceneBindings.glsl:582-599`; `ResidencyTrigger.h` calls the classifier. Perf CSV now carries wholesale/upload counters. The long-session repeated-boundary growth probe is not evidenced as complete. | **PARTIAL; DRAFT LABEL STALE.** Classifier and wiring shipped with the September merge; import the repeated-crossing/session measurement as open. Do not schedule an LRU before that falsification probe. |
| `VIXEN/Vixen-Docs/01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08.md:3,23-28,262-265,303-333` | `active`; regime-3 slice 1 is closed, while cross-instance composition and use of baked anisotropic mips are follow-ons. | Current code contains the cosmic accumulator and anisotropic mip storage. The document says the accumulator still commits an opaque per-instance hit and does not consume anisotropy. Its open `walkCov`/`walkSampledLevel` source audit and sky-sphere cache/hysteresis also lack completion evidence. | **ACTIVE / STATUS GENERALLY ACCURATE.** Import the open cosmic compositor, anisotropy-consumption, and residency probes as follow-on work. |
| `VIXEN/Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md:3,8-16,157-177` | `implementation-ready design`. | Current code implements E21-S1 through E25-S5: channel-pool reserve/populate; atomic channel-pool/brick-lookup admission and reuse ledger; zero fine-byte upload on mip-only legs; tier-ref/occupancy payload readiness; capacity arena with placeholder-backed 31.5% allocation reduction. All five commit SHAs are ancestors of `origin/main`. | **IMPLEMENTED; DOC UNDERSTATES CODE.** Import five closed tasks; no remaining slice from this document was identified. |
| `VIXEN/Vixen-Docs/Deep-Field-HDR-Exposure-2026-08.md:3,8-16,131-140` | `DESIGN-READY`; table labels the HDR seam an implementation-ready slice. | `ExposureTonemap`, `ExposureMeter`, `SceneRadianceNode`, and graph wiring are in current code. HDR1 `bd159652a734dae4c754f130c36717c6df6456ab` and HDR2 `f92761bb5cb02d6deeb013f27ff0113499dcaa87` are ancestors of `origin/main`; the source-list repair is `425b5c997b3dccf5a59791edebb233b644a9c702`. | **IMPLEMENTED; DOC STATUS STALE.** Import HDR1 and HDR2 as closed tasks. |
| `VIXEN/Vixen-Docs/01-Architecture/Main-Wave-Reconciliation-2026-09.md:3,58-80,131` | “Gated and delivered in the merge commit containing this report.” | Merge `e335130dcbceebf683f05d4008f35aead12d24d3` is an ancestor of `origin/main`; the report records the regenerated AppFlow and deleted retired view artifacts. | **IMPLEMENTED.** Import as a closed task. Its unrelated/nested-worktree AppFlow drift check remains controller-gated per the report. |
| `VIXEN/Vixen-Docs/01-Architecture/Simd-Materialization-2026-09.md:4-6,173-192` | Delivered with SIMD4 CPU default; full-tree AppFlow drift check remains controller-gated. | Engine commit `5f8dd399787dd86b336b0d29a8e9c1bc1075dc21` is an ancestor of VIXEN `origin/main`; Yeroket commits `939c2880` and `fdfed89f` are ancestors of Yeroket `origin/main`. The report's nested `.../undertow/vixen/engine/core/codegen/view-schemas` path is gone. | **IMPLEMENTED WITH A DOCUMENTED GATE.** Import the engine task as closed and anchor the Yeroket source. The dead view-schema reference is listed below. |
| `VIXEN/Vixen-Docs/01-Architecture/Voxel-Mutation-Replacement-2026-09.md:3,19-22,54-70` | CPU-first backend and multicore assembly implemented; GPU backend/paged-pool commit remains follow-on work. | Commit `fb6b1590dcccb859b0152fdbb00bf106cdb2e65b` is an ancestor of VIXEN `origin/main`. `VoxelInjectionQueue` files are removed; `VoxelInjector` remains, matching the partial/open KI-027 record. | **IMPLEMENTED / PARTIAL FOLLOW-ON.** Import the delivered CPU task as closed; keep T-0017/KI-027 open. |
| `VIXEN/Vixen-Docs/01-Architecture/Undertow-VIXEN-Federation-2026-08.md:21-98` | Describes VIXEN as an Undertow subdirectory with a CoreCLR/managed host and View subsystem. | R106.1/R155/R162.4 replace this model. Current Undertow has deleted `Undertow.View` and its wiring, and VIXEN is fetched as a pinned framework. | **OBSOLETE.** Do not import its managed-host or embedded-submodule plan as live VIXEN work. Its external path resolutions are listed below. |

### Verified current-code commit set

All VIXEN SHAs below passed `git merge-base --is-ancestor <sha> origin/main` in this worktree:

| Feature | Commit(s) |
|---|---|
| Main-Wave reconciliation / E6 shared classifier | `e335130dcbceebf683f05d4008f35aead12d24d3` |
| E7-T1 stencil + E8-T1 multirung | `188f5cb91a9c1bcfb486aa40cbdf30aff53e4adb` |
| E9/E10 level targeting + orbital fixture | `4b7195e9740a9237147a2be6b1e851f4bec25272` |
| E11-T1 tile reduction and evaluator skip | `c12eaeba82fc986731575149bbaf9e3cf92e8f2b` |
| E12-T1 / KI-052 hazard fix | `868298e2e9cb2d9e6e827d53ddae69056b744b6b` |
| E21-S1 | `231e272ec94c1d7eb8a1be466167b40e7655ad9f` |
| E22-S2 | `73f202440cc45f265a6088efe8e2c0d3393d5544` |
| E23-S3 | `27828493de9f3ac2ba630e2080c0e8d097d611f6` |
| E24-S4 | `c780e70584fbdb28dba48898a672ebfb132170bb` |
| E25-S5 | `3334cd4ed6a45b277ed62d0ab9b50430fa88d95a` |
| HDR1 | `bd159652a734dae4c754f130c36717c6df6456ab` |
| HDR2 | `f92761bb5cb02d6deeb013f27ff0113499dcaa87` |
| HDR source-list repair | `425b5c997b3dccf5a59791edebb233b644a9c702` |
| SIMD4 engine materialization | `5f8dd399787dd86b336b0d29a8e9c1bc1075dc21` |
| CPU-first voxel replacement | `fb6b1590dcccb859b0152fdbb00bf106cdb2e65b` |
| AppFlow Inc2b code/gate | `d4ce37033cc5b821817689fc09b12613e0f8647a` |

Yeroket kernel commits `939c2880` and `fdfed89f` independently passed the same ancestry check against Yeroket `origin/main`. The domain-agnostic plan's `6fa9cb2c` did **not**: it is not a valid object in the current checkout. No implementation claim is closed on that SHA.

## CodegenTool verification and baseline finding

**Required scope:** restore/build the kernel-framework `CodegenTool` and run VIXEN's `recipe_simd_check` inputs (`SDFInstruction.cs`, `SdfCoreKernels.cs`, `recipe-lowering.extensions`) against `RecipeSimd.g.hpp`.

| Step | Command/result |
|---|---|
| First build red, before document edits | A temporary output-path override caused MSB3540 (`MSBuildProjectExtensionsPath` changed after use), exit 1; queue log `1790173010-build-vixendocs-codegen-build.log`. A second isolated-path attempt included both the project `obj` and temporary generated sources and failed with duplicate assembly attributes; logs `1790173047-build-vixendocs-codegen-build-retry.log` and `1790173122-build-vixendocs-codegen-build-isolated.log`. These were invocation/configuration failures, not source baseline results. |
| Recovery A | Standard queued `dotnet restore ...CodegenTool.csproj -nodeReuse:false` succeeded (exit 0); standard queued `dotnet build ...CodegenTool.csproj -c Release --no-restore -nodeReuse:false` succeeded with 0 warnings and 0 errors. |
| Requested `--check` | Queued `dotnet run --project ...CodegenTool.csproj -c Release --no-build --no-restore --property nodeReuse=false -- --recipe-simd-cpp --recipe-opcodes .../SDFInstruction.cs --recipe-kernels .../SdfCoreKernels.cs --recipe-extensions VIXEN/codegen/recipe-lowering.extensions --out-header VIXEN/libraries/SVO/include/Recipe/generated/RecipeSimd.g.hpp --check` failed with exit 134. Queue log: `/home/liory/.local/state/undertow/undertow-box-logs/1790173253-light-vixendocs-codegen-check.log`. |

The failure occurs before generator output comparison. VIXEN `recipe-lowering.extensions:4-5` uses eight fields after `=`, while Yeroket `RecipeLoweringModel.cs:363-375` requires eleven, including derivative rule, hazard, and cost. CMake uses the same argument contract at `VIXEN/codegen/CMakeLists.txt:315-325`. No regeneration was attempted because the parser rejects the unchanged source before emission. **Generated-header freshness is unverified**; this report makes no stale-output or before/after claim.

The check failed on the recorded base itself (`HEAD == origin/main`) with unchanged tracked inputs. This is a cross-repository generator/schema contract finding, outside this docs-only change. No full product build is required for this deliverable; the report and apply script can be committed while the controller assigns the contract decision.

## Cross-repository reference resolution

`RESOLVED` means the path exists under the configured current repository root. `MOVED` means a current replacement exists. `DEAD` includes a removal commit. Where a Windows worktree artifact was not in the current tree or Git history, it is explicitly **UNCLASSIFIED** rather than assigned a fabricated removal SHA.

| Source doc and lines | Cross-repository reference | Status | Current target / evidence |
|---|---|---|---|
| `Quick-Lookup.md:18-21` | Undertow `perf/e7-t1-stencil-report.md`, `perf/e8-t1-multirung-report.md`, `perf/e9-t1-level-targeting-report.md`, `perf/e10-t1-orbital-fixture-report.md`, `perf/e10-t2-rung-calibration-report.md`, `perf/e11-t1-stencil-perf-report.md` | RESOLVED | All six files exist under `/home/liory/projects/undertow/perf/`; E12 report is also present at `undertow:perf/e12-t1-ki052-report.md`. |
| `Known-Issues.md:18,46,103,105,1419` | Undertow `perf/e7-t1-stencil-report.md`, `perf/e4-t1-virtual-census-report.md`, `perf/e3-t1-framesync-probe-report.md`, `perf/e5-t1-mip-regime-report.md`, `perf/e12-t1-ki052-report.md` | RESOLVED | All five reports exist under the current Undertow root. These targets are carried into the SPT links for T-0001/T-0002/T-0003 and the new KI-052 item. |
| `Known-Issues.md:103` | Windows `perf/e3-t1-framesync-probe/` directory (`winbuild`) | UNCLASSIFIED | No matching path in the current Undertow tree or tracked deletion/rename history. It appears to be a local Windows capture directory; provenance/replacement is not established. |
| `Known-Issues.md:153-154`; `Deep-Field-Mip-Accessor-Policy-2026-08.md:10-14,343-345` | Undertow `docs/plans/2026-08-04-wavefront-recipe-shading.md`; `docs/superpowers/specs/2026-08-08-deep-field-mip-policy-design.md` | RESOLVED | Both documents exist under the current Undertow root. |
| `Deep-Field-Policy-Stencil-Grouping-2026-08.md:11-12,31-32` | `C:\GitHub\undertow-winbuild\perf\e1-slice0\report.md`; `...\e2-slice1\report.md` | UNCLASSIFIED | Neither target exists in the current Undertow tree; name search and `git log --all --diff-filter=D` found no tracked replacement or removal commit. No file anchor should be added until the owner identifies the captured reports. |
| `AppFlow-Framework-Inc1-Plan-2026-07.md:28` | Yeroket kernel-framework `CodegenTool~` | RESOLVED | `yeroket:Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs` exists. |
| `Domain-Agnostic-MultiChannel-Recipe-Output-Plan-2026-07.md:20-21,164-179` | Yeroket `com.yeroket.utility.kernel-framework`, `com.utility.graph-framework`, `com.utility.sdf`, and commit `6fa9cb2c` | RESOLVED path / UNVERIFIED commit | Current package targets exist under `Packages/`; current parser is `yeroket:Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/RecipeLoweringModel.cs`. `6fa9cb2c` is absent from the local object database, so the plan's commit claim is not verified. |
| `Main-Wave-Reconciliation-2026-09.md:63-65,76`; `Simd-Materialization-2026-09.md:12,173-188` | Yeroket `CodegenTool~`, kernel SIMD emitter and commits `939c2880`/`fdfed89f`; old nested Undertow view-schema path | RESOLVED / DEAD | `yeroket:Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs` and current kernel files exist; both kernel SHAs are ancestors of `origin/main`. The nested `.../undertow/vixen/engine/core/codegen/view-schemas` path is gone; the tracked schema directory was removed by Undertow commit `1a688dd835cde40048d4cce2822711946d573b1a`. |
| `Undertow-VIXEN-Federation-2026-08.md:21-50,55-78,92-98` | Undertow `core/`, `vixen/CMakeLists.txt`, `vixen/app/CMakeLists.txt`, `vixen/codegen/CMakeLists.txt`, `vixen/render/Generated/ViewSectionEnum.g.h`, `vixen/host/Undertow.Vixen.Host/` | RESOLVED | All listed paths exist in the current Undertow tree. Their existence does not preserve the old embedded-submodule/managed-host architecture. |
| `Undertow-VIXEN-Federation-2026-08.md:50,92-95`; `Simd-Materialization-2026-09.md:188` | Undertow `core/codegen/view-schemas/UndertowHud.cs` and `core/codegen/view-schemas/` | DEAD | The hand schema moved into Undertow in `67dcb4819d3de4d270dd2ba3cbdb9cad8cbc933c`, then the schema ledger was deleted by `1a688dd835cde40048d4cce2822711946d573b1a`. |
| `Undertow-VIXEN-Federation-2026-08.md:93-95` | Undertow `core/src/Undertow.Authoring/Schema/SchemaJson.cs`; `core/src/Undertow.View/Generated/`; `tools/check-view-derive.sh` | RESOLVED / DEAD / MOVED | `SchemaJson.cs` remains. `Undertow.View/Generated/` and `check-view-derive.sh` were removed with `Undertow.View` in `1106474711647258e888b8925284f66c8d167c54`; the view derive gate now lives in `undertow:tools/check-content-codegen.sh`. |
| `Undertow-VIXEN-Federation-2026-08.md:98` | Yeroket `Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs` | RESOLVED | Path exists under the current Yeroket root. |

### Dead and unresolved references

- **DEAD:** `undertow:core/codegen/view-schemas/UndertowHud.cs` and its directory, removed by `1a688dd835cde40048d4cce2822711946d573b1a` after relocation (`67dcb4819d3de4d270dd2ba3cbdb9cad8cbc933c`).
- **DEAD:** `undertow:core/src/Undertow.View/Generated/`, removed with `Undertow.View` by `1106474711647258e888b8925284f66c8d167c54`.
- **MOVED:** `undertow:tools/check-view-derive.sh` functionality is in `undertow:tools/check-content-codegen.sh` after the same view retirement.
- **UNCLASSIFIED:** the two `undertow-winbuild` E1/E2 Windows reports and the E3 capture directory have no current file, name-search match, or tracked removal commit. The report leaves their provenance open rather than manufacturing a DEAD commit.

## Proposed SPT apply set

`reports/vixendocs-apply.sh` is a transfer script only; it has not been run. It adds **two live epochs** (Deep-Field follow-ons and domain-agnostic recipe output), their stories/open tasks, and **16 implemented tasks** closed with verified VIXEN commits. It links the existing T-0001/T-0002/T-0003 tasks to their resolved Undertow reports, links S-0007 to its VIXEN source, and anchors T-0525 to this report, the apply script, and the Undertow ruling/split references. Resolved cross-repo targets receive `--file` anchors; the unclassified Windows artifacts do not.

## Remaining candidate documents

The pass triaged candidate filenames/status lines but did not validate every feature plan against code. The remaining detailed-review surface includes the following plan/status families:

- Auto-Sync FrameGraph design and Inc1 plans P2–P5b; Config-Struct-Codegen P0/Phase A–C and standalone-tool designs.
- View-Contract Codegen Inc1–Inc5b, View-Model-Binding Inc A/A2/B/C/D/Override, and the renderer-agnostic provider-seam designs.
- SDF/Recipe authoring and kernel-codegen plans from Inc4 P0 through P2.4; Stored-SDF, voxel multi-channel, and voxel-authoring plans.
- Sampled-Lighting Inc0–Inc7 and its Cornell demo, DDGI, fail-scenario, and HWRT/multi-queue directions.
- Sparse-Mip ESVO, Tiered-ESVO Inc1–Inc3, recipe bucketing/unroll/cache, and Undertow Codegen Unification Inc1–Inc10.
- Other active-path infra/performance plans, audits, and legacy feature proposals under `01-Architecture/`, `04-Development/`, and `05-Progress/`; also the two `VIXEN/docs/superpowers/plans/` files.

Archive directories were not broadly traversed; only references from current documents to archived or removed targets were checked. These remaining items need a subsequent pass before calling the whole VIXEN vault reconciled.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- The VIXEN Known-Issues document had to be harvested and reconciled manually because SPT's source registry accepts only five hard-coded files and rejects the VIXEN path (`SPT/src/harvest.ts:366-378`) | VIXEN had no registered parser/source, so the existing harvest flow could not produce its candidates | add a manifest-backed VIXEN Known-Issues source or parameterized document parser.
- AppFlow Inc1 used a hand-authored `AppFlow.g.h` because the shared Yeroket `CodegenTool` emitted only `[GpuStruct]` structs, not the enum/table/reader needed by AppFlow (`AppFlow-Framework-Inc1-Plan-2026-07.md:28`; current header `VIXEN/libraries/AppFlow/include/generated/AppFlow.g.h`) | the facade lacks that declarative emitter surface | add schema-driven enum/table/reader emission and replace the hand-authored header.
- Recipe extension declarations drifted from the current shared parser (8 fields in `VIXEN/codegen/recipe-lowering.extensions:4-5`, 11 required by `RecipeLoweringModel.cs:368-375`) and stopped the requested `--check` before output comparison | the VIXEN declaration and kernel parser have no shared/versioned contract guard | choose one contract owner/schema and add a cross-repo compatibility gate that validates the declaration before codegen.
