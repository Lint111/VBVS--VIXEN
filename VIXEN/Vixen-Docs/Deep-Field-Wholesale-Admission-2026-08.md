---
title: Deep-Field Wholesale Admission — demand, signature, and L1-warm contract
status: implementation-ready design
created: 2026-08-10
tags: [architecture, residency, bandwidth, svo, cache, vulkan]
---

# Deep-Field Wholesale Admission

## Decision

Adopt **stable descriptor binding + capacity reserve + demand population**, with an
explicit per-octree `WholesaleAvailability` ledger.  The descriptor identity and
buffer capacity are created at Compile; content is copied only after admission, and
the shader is told that it may consume that payload only after the copy is complete.
This is the existing, proven brick-pool shape: the brick SSBO reserves full capacity
and is a transfer destination, the upload is asynchronous, and `brickResident` is
published only when the copy is visible (`BodyOctreeSceneNode.cpp:934-955`,
`:1357-1374`; `BodyOctreeSceneNode.h:336-344`).  It is the smallest safe first
slice and has no descriptor rebind on a regime transition.

This is deliberately a two-level result:

* `populated_shader_readable_bytes` is the admitted working set and is the primary
  “minimum data signature” measure.
* `allocated_capacity_bytes` remains separately visible.  Capacity reserve avoids
  transfer/rebind churn but cannot honestly claim to minimize physical allocation.
  A later segmented/sparse arena is the only route to reducing that number; it is
  gated on proof that population bytes, rather than capacity, remain the limiting
  footprint.

The initial implementation must not silently call this L1 residency. A GPU cache is
hardware-managed, not pin-able by this protocol. The claim to test is that each
hot shader access has a small enough active signature and locality to be cacheable;
not that a whole scene is resident in every SM’s L1.

## Why this protocol, and not the alternatives

| option | decision | evidence |
|---|---|---|
| Late/deferred descriptor binding | reject | Compile publishes every buffer output and downstream descriptor construction consumes those handles (`BodyOctreeSceneNode.cpp:442-466`). A later null→real bind changes graph/descriptor lifetime rather than just content readiness. |
| Full realloc + descriptor rebind per transition | reject | Existing buffer growth requires `vkDeviceWaitIdle` precisely because old command buffers could still reference it (`BodyOctreeSceneNode.cpp:1148-1157`); doing that at camera-regime cadence is a hitch and identity risk. |
| Sparse/segmented allocation now | defer | It is the eventual capacity-minimizing ladder rung, but requires a virtual-address/page-table and descriptor-range contract. No such wholesale allocator exists; the current buffers are one contiguous SSBO each (`SceneBindings.glsl:44-76`). |
| **Reserve + populate, stable binding** | **choose** | Matches the brick state machine, supplies a valid placeholder from frame zero, and makes the publication point explicit. `RequestBrickResidency` already defers mutation from the setter to Execute (`BodyOctreeSceneNode.h:230-238`). |

The current state has no wholesale transition seam: `CreateOctreeBuffers` allocates
and fills nodes, bricks, materials, config, channel pool, lookup, mip, tier and
occupancy during Compile (`BodyOctreeSceneNode.cpp:918-1086`). Instrumentation now
records all nine events (`:1075-1086`), but does not make them lazy.

## Admission state and publication protocol

`WholesaleAvailability[octree]` is CPU-owned, deterministic, and has the fields
`desiredRegime`, `committedRegime`, `generation`, `pendingMask`, `readyMask`, and
the byte counts per payload. Its shader-visible mirror is packed into the existing
per-octree config upload (or a new small availability SSBO if ABI growth is chosen);
it is not a push constant. Per-frame values already travel in ring-slot SSBOs and
the node re-emits the current slot each Execute (`BodyOctreeSceneNode.cpp:425-448`,
`:563-584`), which is the required precedent.

For a monotonic generation:

1. CPU classifies each active instance with `ClassifyFootprintRegime` from
   `libraries/SVO/include/FootprintRegime.h:37-64`, using the exact frame parameters
   supplied to the shader. OR-reduce duplicate instances by octree index: admission
   is deterministic because the set is sorted by octree index, then payload kind.
2. The CPU applies hysteresis: promote to Surface only after two consecutive Surface
   classifications; demote only after four consecutive non-Surface classifications.
   It uses the already-established n±1 residency window/frustum anti-thrash intent
   (`Deep-Field-Residency-Unification-2026-08.md:278-282`). A generation change resets
   the counters. One-frame lateness is permitted: data stays fail-soft until ready.
3. On promotion, allocate/reserve the already-bound destination if absent, enqueue
   the payload copies via `BatchedUploader`, and retain `readyMask=0`. On completion,
   copy the matching availability/config record, submit it after the transfer, and
   only then set `readyMask`. The existing brick implementation demonstrates why queued
   is not ready (`BodyOctreeSceneNode.h:219-226`). The graph edge must express stage
   topology with the `ORDERING_WAIT_SEMAPHORE` convention; that input is deliberately
   ordering-only, not a real wait (`ComputeStageNodeConfig.h:419-426`).
4. On demotion, clear the ready bit first in the next safe frame. Do not overwrite or
   free the payload in slice 1. It becomes non-readable, so correctness never depends
   on stale bytes. Reuse it on the next promotion. A later capacity-reclaim slice
   waits for the last referencing frame fence, then returns its segment to the arena.

`DeriveResidencyDefault` remains a capability precondition, not a live regime
decision (`Deep-Field-Residency-Unification-2026-08.md:205-210`). The CPU twin is
already hand-synced to the GLSL classifier by contract (`FootprintRegime.h:5-35`);
slice 1 adds a table-driven parity test at thresholds and an MD5-normalized source
constant check. The owner of any changed threshold is the VIXEN render-policy owner;
without their sign-off the old constants remain authoritative.

## Payload policy and fail-soft semantics

The invariant is: a false `ready` bit may reduce detail, but must never cause an
out-of-range read, a fabricated hit, or a shadow occluder. The availability bit is
checked **before** each payload’s first address calculation; a 1-byte placeholder is
only a descriptor-validity device, never a range-validity proof.

| payload | may be absent | legal shader result / exact sites |
|---|---|---|
| channelPool + brickLookup | Yes, together, for MipHit/Cosmic or pending Surface | `StoredSdf.glsl:153-169` consumes `brickLookup` then `channelPool`; `:492-500` uses lookup for brick allocation. If `FineSdfReady==false`, skip both and return the existing no-fine-data sentinel (`1e9`) / no allocation. `marchBrickSdf*` callers then take the mip policy instead of entering fine march. Never admit one without the other. |
| mipPool | **No for a mip-baked visible octree** | Regimes 2/3 require it: `readMipSample` checks semantic and bounds then returns `{missing,0}` only for non-mip-baked data (`MipFallback.glsl:55-69`). “mip is enough” means channel/lookup fine data never uploads, not that mip data may vanish. A non-mip-baked tree keeps the established `{missing,0}` fallback and must remain Surface/capability-pinned. |
| tierRefTable | No when a visible parent has `farBit`; yes otherwise | `SceneBindings.glsl:2474-2529` bounds-checks the table before child routing. If unavailable, treat the far reference as unresolved and use the parent’s mip fallback; do not descend with an invented child index. Any-hit already returns false for non-resident/sub-pixel paths (`:2497-2520`). |
| occupancyGrid | Yes unless its recipe reports `gridDim>0` | The procedural path is already specified to skip the fast path for `gridDim==0` and bounds-check the grid (`BodyInstanceRayMarch.comp:61-80`; `BodyOctreeSceneNode.cpp:1062-1067`). Admission must make the generated recipe metadata report zero until ready, so it falls back to normal recipe evaluation. |
| nodes, materials, config | No in slice 1 | They are routing/control metadata for every path, and config carries bases and readiness. Keeping them resident is the minimum correctness root. |
| bricks | Existing rule | `brickResident==0` selects the mip/no-occluder behavior rather than reading fine data (`SceneBindings.glsl:2536-2562`). Wholesale readiness extends this rule; it does not weaken it. |

Primary visible traversal may use mip coverage/shading under the established policy;
any-hit/shadow must return no occluder for absent fine or tier data. This follows the
existing explicit rule that coarse coverage is not proof of a real crossing
(`SceneBindings.glsl:2499-2520`, `:2536-2541`).

## Metrics contract

Add these CSV columns, all unsigned bytes except counts: `scene_leg`, `frame`,
`wholesale_desired_mask`, `wholesale_ready_mask`, `wholesale_generation`,
`allocated_capacity_bytes`, `populated_shader_readable_bytes`,
`reusable_populated_bytes`, `boot_bytes_uploaded`, `steady_state_bytes_uploaded`,
`whole_buffer_upload_bytes`, `channel_pool_populated_bytes`,
`brick_lookup_populated_bytes`, `mip_pool_populated_bytes`,
`tier_ref_populated_bytes`, `occupancy_grid_populated_bytes`, and
`resident_signature_fnv64`.

`resident_signature_fnv64` hashes a canonical ascending tuple of `(octreeIndex,
payloadKind,generation,ready,size,contentHash)`; it is a signature, not a byte count.
`allocated_capacity_bytes` sums all Vulkan allocations reserved for the payloads;
`populated_shader_readable_bytes` sums only ready ranges legal for shader reads;
`reusable_populated_bytes` records non-readable but retained content. This resolves
the prior ambiguity instead of hiding it behind “resident.”

For every benchmark leg report the first-ready frame, median and max populated set,
capacity, signature, boot transfer, steady transfer, and the policy dispatch mix.
The known baseline is 118,979,794 boot bytes, including two 37,453,824-B shell-cache
events (the task’s two prior reports); no “after” number is claimed until Windows-native
N>=3 captures exist.

On an RTX 3060, L1 is per-SM and dynamically partitioned with shared memory; a scene
cannot be promised to “live in L1” globally. The honest L1-warm bar is: for each hot
access site, report the active per-dispatch payload/range and access reuse, demonstrate
it is substantially smaller than the working cache hierarchy, and collect cache
metrics where the native profiler permits. The design success is low per-access
signature and locality; L2/L1 hit rate is evidence, not an allocation guarantee.

## Compaction and reuse ladder

1. Keep `DeriveShellPool` and the two-slot shell cache independent and first: it
   already produces a compact, render-equivalent pool (`BodyOctreeSceneNode.cpp:1204-1232`)
   and reuses a slot when capacity suffices (`:1251-1265`). Its two bootstrap uploads
   explain the measured shell events (`:1303-1307`).
2. Reuse a ready-but-demoted whole payload on re-admission before copying; record it as
   `reusable_populated_bytes` and transfer zero new bytes.
3. Compact channelPool+lookup as an inseparable pair only after per-octree admission
   is stable; preserve/remap `poolBrickBase` and `brickLookupBase` together.
4. Introduce a segmented arena/range ledger only when capacity metrics prove that
   retained reserve dominates. Then fence-retire old segments, recycle them, and only
   then consider sparse Vulkan backing. Never reclaim merely because the regime changed.

## Slice ladder and gates

1. **S1 — one-buffer proof: channelPool + readiness bit, with its mandatory
   brickLookup companion retained as the existing compact-shell mapping.** Files:
   `libraries/RenderGraph/{include/Nodes/BodyOctreeSceneNode.h,src/Nodes/BodyOctreeSceneNode.cpp}`,
   `libraries/SVO/include/FootprintRegime.h`, `application/main/source/VulkanGraphApplication.cpp`,
   `shaders/{SceneBindings.glsl,StoredSdf.glsl}`, `application/main/include/PerfCsvWriter.h`,
   and the CSV writer call site. Gate: flag-off byte identity; stored-control two-hash
   state-set discipline; N>=3; no OOB validation errors; pending and absent paths show
   zero fine reads and mip fallback; Windows-native transfer result only.
2. **S2 — admit channelPool+brickLookup as an atomic payload and add reuse ledger.**
   Gate: promotion/demotion/re-promotion has identical ready frames and zero second
   transfer when retained; `resident_signature_fnv64` is deterministic.
3. **S3 — mipPool policy.** Keep it ready for all visible mip-baked Regime-2/3 legs;
   prove the fine pair never uploads on mip-only legs. Gate: existing mip-policy frame
   parity, including the known two-hash set.
4. **S4 — tierRefTable and occupancyGrid.** Add the exact parent-mip and normal-recipe
   fallbacks above. Gate: nested-tier and procedural fixtures, plus any-hit no-occluder
   behavior.
5. **S5 — shell compaction/reuse integration, then capacity arena.** Gate: slot handoff
   and descriptor identity remain safe across both frames-in-flight; only this slice may
   claim reduced allocation capacity.

All performance claims require Windows-native benchmark runs; no Linux/WSL result is
substituted. Every changed stored-control leg compares its two permitted frame hashes,
not one convenient boot.

