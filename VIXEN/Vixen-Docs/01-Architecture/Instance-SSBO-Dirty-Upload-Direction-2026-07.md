---
title: Partial/Dirty-Only Instance SSBO Upload — Direction
status: future / not scheduled
created: 2026-07-15
---

# Partial/Dirty-Only Instance SSBO Upload — Direction

> **Not scheduled.** Captured while validating
> [[Recipe-Parameterization-Plan-2026-07]] M3 (live no-recompile proof, in flight). This is a
> genuinely separate optimization from what that plan proves — do not conflate them. Write a real
> plan doc when this is picked up; this is a direction note only.

## 1. The idea (user, 2026-07-15, verbatim intent)

Now that recipes are parameterized (`recipeParams[]` read via `ReadParam`), can the per-instance
SSBO upload be made partial — only refresh the parameter data that is actually dirty/changed this
frame, instead of re-uploading every instance's full record every frame?

## 2. Current state (verified against the M3 worktree, `BodyOctreeSceneNode.cpp`)

Today's upload is a **full unconditional re-upload every frame, regardless of whether anything
changed**:

- `SetInstances()` (~line 135) just stashes the whole `std::vector<BodyInstanceGpu>` — no per-field
  dirty tracking exists at all, for `recipeParams` or anything else.
- `ExecuteImpl()` (~line 401-419) rebuilds a packed byte buffer from the ENTIRE `instances_` vector
  every single frame (`PackInstances(toPack)`) and `memcpy`s the whole thing into the current
  frame's ring slot, unconditionally — there is no early-out for "nothing changed since last frame."
- The destination is a **per-frame-in-flight RING buffer** (`perFrame_`, `kRingSize =
  MAX_FRAMES_IN_FLIGHT`, comment at line 40) — NOT one persistent buffer. Each ring slot is a
  physically distinct allocation that only gets written when its frame index comes around.

## 3. Why this is genuinely harder than "skip unchanged bytes" — the ring-buffer trap

The naive version of this idea ("only memcpy the instances whose data changed this frame") is
**wrong against a ring buffer** and would introduce a real correctness bug if implemented naively:
slot N's contents are NOT slot N-1's contents — they're whatever was written into physical slot N
the LAST time that slot's frame index came up, which for a `kRingSize`-deep ring is
`kRingSize` frames ago, not 1. If instance 7's params didn't change THIS frame but did change
`kRingSize-1` frames ago, skipping the write into slot N leaves stale (correct-`kRingSize`-frames-
ago, not correct-now) data in that slot.

Real options, roughly in order of complexity:

1. **Track dirty state per ring slot, not globally.** Each slot needs its own "what's stale in me"
   bitset — an instance is dirty for slot N until slot N has been written since that instance's
   last change. This means a changed instance stays "dirty" for up to `kRingSize` frames (once per
   slot), not just the next one. More bookkeeping than it sounds — the dirty flag's lifetime is
   tied to the ring's rotation, not to "did it change."
2. **Copy-forward + patch.** On each frame, start from the PREVIOUS slot's full contents (a
   GPU-side or host-side copy) and only patch the genuinely-changed bytes on top. Trades the
   full-memcpy cost for a full-copy-forward cost plus a small patch — only wins if patch count
   is much smaller than instance count, and only if the copy-forward itself is cheaper than the
   direct memcpy it replaces (not obviously true at small `N`).
3. **Move to a single persistent buffer + explicit sync instead of a ring.** Sidesteps the
   staleness problem entirely (one buffer, one source of truth, dirty-only writes are trivially
   correct) but requires explicit GPU/CPU synchronization the ring buffer currently gets "for free"
   from the frame-fence wait (`ExecuteImpl`'s own comment: "The frame fence for frameIndex was
   waited before Execute fired, so this slot is guaranteed not in flight" — line ~412). Removing
   the ring means re-deriving that safety property some other way (a barrier, a fence, or
   accepting a race window). This is the most invasive option and probably not worth it unless
   the ring's own memory-multiplication cost (kRingSize× the buffer size) is itself a problem.

## 4. Is this actually worth it yet? — measure first

`BodyInstanceGpu` is 64 bytes (per `Recipe-Parameterization-Plan-2026-07.md`'s own reuse notes).
At the instance counts this engine currently exercises (the uber-recipe demo scenes: single/low
digits to N≈10 structurally, per
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]]'s own switch-scaling table — the SAME
table that measured the shader-switch knee at N=100), a full per-frame re-upload of the whole
instance array is **kilobytes**, not a meaningful bandwidth cost next to everything else this
engine already uploads every frame (brick residency streaming, mip pools, etc. — see
[[Perf-Ledger]]'s "steady bandwidth" column, which tracks exactly this class of cost and currently
has nothing attributed to instance-buffer upload because it's never been the bottleneck found).

**Before building any of §3's options, measure whether this is actually load-bearing** at the
instance counts this engine will realistically carry. The natural trigger to revisit this is
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]]'s own N≥100 target — if/when that epic's
tier-1 promotion ships and the engine is routinely carrying order-100+ live parameterized
instances, a 64B×100+ per-frame full re-upload becomes worth a second look, and by then the JIT
epic will likely have ALSO restructured how instances are grouped/dispatched (batched by pipeline
family), which may change what "dirty" even means at that point (e.g. dirty per DISPATCH GROUP,
not per raw instance) — so this may be better solved as part of that epic's own batching design
rather than bolted onto today's simple SSBO ring independently.

## 5. Open questions (for whoever scopes this properly)

- Is the actual pain point upload BANDWIDTH (bytes/frame) or CPU-side PACKING cost
  (`PackInstances` re-serializing the whole vector every frame, independent of the GPU transfer
  itself)? These have different fixes — a packing-cost problem might be solved by caching the
  packed bytes and only re-serializing changed instances into an already-packed buffer, without
  touching the ring-buffer/GPU-upload side at all.
- Does [[Recipe-Declared-Gaia-Query-Direction-2026-07]]'s per-recipe-type batched dispatch (also
  captured 2026-07-15) subsume this? If sim-side data is already grouped/batched per recipe type
  before it reaches the render side, "dirty" might naturally become "this recipe-type's batch
  changed" rather than per-individual-instance tracking — worth reconciling these two directions
  before building either in isolation.
- Whether `kRingSize` (currently `MAX_FRAMES_IN_FLIGHT`) could shrink or whether the ring pattern
  itself is worth keeping if a persistent-buffer approach (§3 option 3) turns out to be simpler in
  practice than it looks on paper — needs someone to actually prototype both before committing.

See [[Recipe-Parameterization-Plan-2026-07]] (the prerequisite — this only matters once
`recipeParams[]` carries real per-frame-varying content, which M1-M3 establish),
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] (the batching epic this may converge
with), [[Recipe-Declared-Gaia-Query-Direction-2026-07]] (the sim-side batching idea captured the
same day — likely related).
