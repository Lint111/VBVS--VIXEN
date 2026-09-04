# Voxel Mutation Replacement — 2026-09

Status: implemented CPU-first backend and shared-dispatch batch contract; GPU backend and paged-pool commit remain follow-on work.

## Decision and measured layout boundary

The two proposed materializers remain interchangeable behind one action: an owned recipe snapshot goes in, and an owned, pre-ordered `SerializedOctree` page comes out. CPU `ChildDescriptor` is eight bytes and is uploaded verbatim as the shader's two-word ESVO descriptor. Bricks, material/channel pools, brick lookup, mips, tier references, and `OctreeConfig` likewise already have upload-ready GPU layouts. There is no conversion stage at this boundary. Gaia ECS entities are not layout-identical and are therefore not the bulk contract.

CPU recipe materialization is the first backend. It works in the CPU-only WSL gate, reuses the engine's recipe evaluator, and dispatches through the shared typed dispatcher rather than creating another hardware-sized worker pool. A future GPU backend should consume the same request snapshot and return the same `SerializedOctree` streams in canonical order. It should win on sufficiently large jobs, but requires a compute/readback or device-local placement proof and WSL GPU validation that this lane cannot make portable. Callers do not change when that backend arrives.

The doctrine remains the trigger: virtual/unevolved state is not materialized. A delta-log consumer submits the affected region's owned recipe snapshot only when a delta must become resident.

## Replacement design

`DispatchMaterializationBatch` owns its request vector for the duration of one synchronous call and lowers the batch to one typed `KernelDispatch::Stage`. `KernelDispatch::RunPerElementStage` owns worker admission, wave barriers, and stop-token handling; each indexed result is published in input order after reaching `Completed`, `Cancelled`, or `Failed`. There is no materialization-specific mutex, queue, or TBB arena.

`CpuRecipeMaterializer` evaluates the owned recipe snapshot and produces a complete `SerializedOctree`, including channels, lookup, configuration, and mip streams. Its output is placement-ready. Cancellation is checked during recipe evaluation and at each bake/build/serialize phase boundary; an already-entered Gaia rebuild or serialization pass completes before returning a terminal cancellation result. The current first backend still uses the existing Gaia-backed SDF bake internally before serialization; that staging is a compatibility cost, not part of the public bulk contract.

`GaiaVoxelWorld::createVoxelsBatch` remains the editor/compatibility path. Its grouped strategy partitions requests by component signature, creates one archetype seed, uses pinned Gaia `copy_n`, then overwrites values in original request order. The serial strategy remains selectable for measurement. Direct structural creation now fails while Gaia is query-locked; `deferVoxelsBatch` copies owned values into Gaia's ST/MT command buffer and `finalizeDeferredVoxelsBatch` refreshes the Morton index after the lock is released.

The retired `VoxelInjectionQueue` header, source, CMake membership, and old Gaia test target were deleted. No dormant unsafe ring remains.

## Invariants proven

| Invariant / retired defect | Proof |
|---|---|
| Owned payload produces ordered terminal results | `BulkMaterializationDispatchTest.OwnedPayloadProducesOrderedTerminalResults` |
| Assembly workers do not mutate a shared Gaia world | `BulkMaterializationDispatchTest.AssemblyWorkersNeverMutateGaiaWorld` |
| Canonical 1-vs-N dispatch parity | `BulkMaterializationDispatchTest.OneVsNWorkerCanonicalHashParity` |
| Empty input is a successful no-op | `BulkMaterializationDispatchTest.EmptyBatchCompletesWithoutDispatch` |
| Canonical hash includes typed-stream lengths | `BulkMaterializationDispatchTest.CanonicalHashIncludesTypedStreamLengths` |
| Backend failure is retained at the input index | `BulkMaterializationDispatchTest.FailureProducesIndexedTerminalResult` |
| Canonical 1-vs-N real CPU recipe/page parity | `BulkMaterializationIntegrationTest.CpuRecipeOneVsNWorkerCanonicalHashParity` |
| Cancellation produces one terminal result per request | `BulkMaterializationDispatchTest.CancellationProducesOneTerminalResultPerRequest` |
| Grouping preserves values, archetypes, and input order | `GaiaVoxelWorldCoverageTest.GroupedCopyNPreservesInputOrderAndComponentValues` |
| Query-lock structural work uses a Gaia command buffer | `GaiaVoxelWorldCoverageTest.StructuralCommitDuringQueryUsesGaiaCommandBuffer` |
| CPU backend emits complete upload-ready page streams | `BulkMaterializationIntegrationTest.CpuRecipeProducesUploadReadyEsvoPage` |

The pre-change baseline reproduced `double free or corruption` in `VoxelInjectionQueueTest.ProcessBatchCreation`, directly confirming concurrent mutation of one Gaia world. The old core `GetPosition` test also expected non-integral coordinates from a Morton cell address; it was corrected to the documented floor-to-cell contract (`10.5,20.3,-5.7` maps to `10,20,-6`).

## Grouped batch benchmark

Release build, WSL CPU, queue-pinned cores 0–7, 100,000 items, five rounds; table reports the median. `pinned_gaia_copy_n` clones one homogeneous archetype without per-item value replacement, and `direct_page_metadata` appends placement records only. They are deliberate lower bounds, not behavior-equivalent alternatives to the first two rows.

| Path | Median ms | Items/s | Relative to serial |
|---|---:|---:|---:|
| Serial wrapper | 74.6466 | 1,339,650 | 1.00× |
| Grouped `copy_n` + value overwrite | 47.8652 | 2,089,200 | 1.56× |
| Pinned Gaia raw `copy_n` lower bound | 6.62053 | 15,104,500 | 11.28× |
| Direct page metadata lower bound | 0.097804 | 1,022,450,000 | 763.23× |

## Item 3 — shared-dispatch page assembly delivered

Delivered: owned inputs/outputs; one typed `KernelDispatch::Stage` with explicit worker count; no private queue or background pool; cancellation; stable result ordering; canonical hash over every ESVO stream/configuration; fake-backend and real CPU-backend 1-vs-N parity. The CPU producer is functional but currently pays an internal Gaia ECS staging pass. A direct recipe-to-page writer is follow-on optimization and does not alter the contract.

## Item 4 — paged pool and generation swap design only

Reserve a fixed-capacity pool slice against `(region, generation)` and produce into non-visible storage. Before upload and again at timeline completion, reject the result unless its generation is still the region's newest requested generation. Placement stamps byte/element offsets into a private descriptor. Publish that descriptor with one release operation; readers acquire one descriptor and therefore observe either the complete old page or the complete new page, never a mixture.

On exhaustion, return backpressure rather than partially publishing. Eviction selects only unpinned pages with no CPU readers and no in-flight upload/trace use. The old descriptor enters a retirement list after the swap and its slices become reusable only after both the last CPU lease and the relevant GPU timeline value have retired. A stale result releases its private reservation without becoming visible. Budgets apply to the coalesced stream ranges before upload submission.

## Verification and parity

The implementation gate built `test_gaia_voxel_world`, `test_gaia_voxel_world_coverage`, `test_bulk_materialization_dispatch`, `test_voxel_injection`, `test_soa_sdf_serialize`, `test_soa_mip_serialize`, `test_shell_octree_gpu`, and `benchmark_voxel_batch`. Final affected-suite counts are recorded in the worker handoff after the committed blob is rebuilt.

Documentation note: the TDD-linked `Vixen-Docs/03-Research/Gaia-Bulk-Voxel-Mutation-and-Upload-Research-2026-07.md` was not present in this worktree. The governing TDD section and `Lazy-Procedural-Delta-Baseline-Design-2026-07.md` were present and used. Historical Gaia async/parallel notes are now marked superseded.

## Not delivered vs brief

- GPU recipe materialization backend and GPU environment proof.
- Direct recipe-to-ESVO CPU construction without the current Gaia compatibility staging pass.
- Delta-log caller wiring and wholesale upload/coalescing/budget integration.
- Paged-pool allocator, atomic generation swap, stale-result rejection, eviction, and retirement implementation (design only, as scoped).
