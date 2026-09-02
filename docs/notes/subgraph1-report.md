# subgraph1 — I1/I2 worker report

Date: 2026-09-02
Worker: subgraph1
Branch: `lane-subgraph`
Base: `2e6d1ba9509027f2822675c982b786087ca674dc`

## Status

Blocked before implementation by a missing landed dependency. The requested base commit is
correct, but it does not contain the graph-lowering surface required by the authority plan.

## Evidence

- `docs/refs/2026-09-02-subgraph-nodes.md` requires compile-time flattening to remain invisible to
  `GraphTaskLowering::Build` and names `GraphTaskPlan` as the I2 acceptance surface.
- `docs/refs/2026-09-02-subgraph-nodes-plan.md` states that `graphlower` is landed on main and
  requires the I2 plan-equality gate against its task count, edge count, and wave partition.
- CodeGraph returned no indexed context for the requested symbols.
- Repository search found no `GraphTaskLowering`, `GraphTaskPlan`, `GraphTaskLowering.cpp`, or
  corresponding header under `VIXEN/`.
- `git ls-tree -r --name-only HEAD` found no graph-lowering or task-plan path at the requested
  base commit.

## Acceptance

- I1: not implemented; no tests run.
- I2: not implemented; no tests run.
- Full build/test: not run because the required I2 dependency and acceptance surface are absent.
- App files: unchanged.
- Generated files: unchanged.
- Lowering files: unchanged.

## Not delivered vs brief

All requested I1/I2 implementation, tests, build verification, and commit are not delivered. The
worker stopped as instructed rather than introducing a substitute lowering or inventing a parallel
task-plan API.

## Controller action required

Land or otherwise make the planned `graphlower` dependency available on this exact base, then
restart this worker. The worker should re-check the dependency before implementing I1/I2.

## Run 2 — implementation and verification gate

The controller-provided graphlower dependency is present on this run's base. I1 and I2 were
implemented in the RenderGraph library and tests were added. The first queued target build
compiled the RenderGraph library and reached `test_subgraph_nodes.cpp`; it exposed an overload
resolution error in the new test's integer slot-index call. The typed `GraphScope::Connect`
overload was then constrained to constexpr slot types so the existing raw-index overload is
selected. No subsequent compiler verdict was obtained: the required retry remained queued for
over thirty minutes behind active, CPU-consuming build/test holders and was interrupted after
interval polling. No raw build or test command was used.

### Delivered implementation

- I1: `GraphScope` scoped names, member tracking, scoped node creation, internal connections,
  port binding, duplicate-name rejection, and rollback.
- I2: `SubGraphType`, `SubGraphHandle`, constexpr port binding, nested instantiation with cycle and
  depth diagnostics, and immediate flattening of node/group and group/group connections.
- `ConnectionBatch` group connection overloads without introducing new schema vocabulary or a
  runtime grouping node.
- RenderGraph tests covering scoped naming/membership, hand-wired plan equivalence, rollback,
  duplicate outputs, recursive/depth rejection, and the selected graphlower acceptance surface.

### Acceptance state

- I1: implementation and tests present; build/test verdict pending queue access.
- I2: implementation and tests present; build/test verdict pending queue access.
- First target build: failed in the new test compilation on the overload issue described above;
  the source correction was applied.
- Corrected target build retry: externally queue-blocked; no verdict.
- RenderGraph test binaries, including `test_graph_task_lowering`: not run because the corrected
  target build could not acquire the required queue slot.
- Full engine build and codegen check: not run for the same queue gate.
- Generated files: not hand-edited. Application files and lowering files are unchanged.

## Not delivered vs brief — Run 2

The I1/I2 implementation, tests, and required report update are delivered in the worktree. The
required post-correction build, per-binary tests, and full build/codegen check remain blocked
by the shared build/test queue. The implementation was committed as `555272b2`; no push, merge,
branch switch, or other worktree access was performed.

## Run 3 — flatten parity correction

Controller verification of `555272b2` reported `test_rendergraph_subgraph` at 4/5, with the
flattening acceptance failing only because the grouped fixture named its member `Transform` while
the hand-wired reference named it `transform`. Task count and dependency-edge count already
matched. The fixture now uses the same local name in both constructions, and the explicit scoped
name assertion was updated accordingly. No RenderGraph implementation, lowering, application, or
generated file was changed in this correction.

### Acceptance

- I1/I2 focused target build: passed through the queue (`subgraph:build-parity-final`).
- I2 acceptance: `test_rendergraph_subgraph` **5/5 passed** through the queue
  (`subgraph:test-subgraph-final`), including `FlattenedSubGraphProducesTheHandWiredTaskPlan`.
- Lowering dependency regression: `test_rendergraph_tracking` **46/46 passed** through the queue
  (`subgraph:test-tracking-final`), including all 8 `GraphTaskLoweringTest` cases.
- Full configured CMake build: **controller-gated failure**, not caused by this change. The build
  linked the subgraph and tracking targets and reached step 538/556, then `recipe_simd_check`
  rejected line 4 of `recipe-lowering.extensions` as an invalid extension declaration. The queued
  retry ended with `UT_JOB_FAILED` / exit 134; no compiler diagnostic referenced the changed file.
- Standalone light content-codegen gate: **controller-gated failure**, 97/99 artifacts OK, with
  errors for `Undertow.Native/Generated/GaiaShims.g.cs` and the missing `perf` golden
  `stack-8edc62062305a223ba9f6ca564532e3fdeeff44baac1b0cf7db8921feebe6a9a/stack-8edc62062305a223ba9f6ca564532e3fdeeff44baac1b0cf7db8921feebe6a9a.json`.
  The gate also reported pre-existing schema/scheduler warnings.

### Not delivered vs brief — Run 3

No additional I1/I2 behavior was required after the parity diagnosis. The full repository build
and broad content-codegen gate remain externally blocked as documented above; focused acceptance
and the graphlower regression are green. No push or merge was performed.
