# Descriptor Flight-Ring Cleanup — Increment 5 Plan (2026-07-17)

> **Status: SCOPED, not yet started.** Follow-on from
> [[Recipe-Live-App-Bucketed-Dispatch-Inc4-Plan-2026-07]]'s M3 Opus re-validator finding: a benign but
> real Vulkan validation warning (`VUID-vkUpdateDescriptorSets-None-03047`) introduced by Inc4's two new
> descriptor-writing paths, closeable by adopting a pattern the engine already uses everywhere else.

## §0 Scope

**The bug.** Inc4 wired two new descriptor-writing code paths into the live `VixenApp` for the first
time: the recipe-bucketing pre-pass's descriptor set, and the specialized-pipeline promotion path's
descriptor set. Both trigger `VUID-vkUpdateDescriptorSets-None-03047` ("descriptor set must not be in
use by a pending command buffer") — confirmed by Inc4's own re-validation to be a validation-layer
false-positive, not a real GPU hazard (3000+ frame runs render correctly, the warning is self-limited to
~20 occurrences near startup and never recurs). Nothing here is correctness-blocking; this increment
exists to make the warning disappear because the engine already has a proven, idiomatic fix for exactly
this class of problem, sitting unused by these two new paths.

**Why now, not deferred further.** Inc4's own re-validator explicitly flagged this as "a follow-up for a
later cleanup pass, not a gate to closing Inc4" — this increment IS that later pass. Small, contained,
does not block on anything else in flight.

**Grounding research finding that reshapes this increment's scope (2026-07-17, pre-plan):** a dedicated
research pass independently re-verified the Inc4 doc's own root-cause framing and found it needs
correction before real work starts — see §1 below. The two paths are NOT equally hard to fix, and one
path's actual contribution to the VUID count is unmeasured, not confirmed. This increment is
**explicitly structured to avoid Inc4's own scope-inflation mistake** (Inc4 started as "just wire GPU-LRU
eviction," then discovered 3 real prerequisites once it looked closely) — Milestone 2 here is
deliberately left unscoped-in-detail until Milestone 1's own measurement tells us whether it's even
needed.

## §1 Grounding — what the research found (corrected from Inc4's own doc framing)

Inc4's Progress Log characterized the fix as "adopt the engine's existing `DescriptorSetNode` flight-ring
pattern for the bucketing pre-pass's descriptor sets, instead of the current single-set +
`vkDeviceWaitIdle` approach." Independent verification found this framing is **partially wrong** in a
way that matters for scoping:

- **`DescriptorSetNode.cpp:22-29`** genuinely implements a 4-deep descriptor-set ring
  (`DESCRIPTOR_SET_RING_DEPTH = 4`, matching `FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT`), with a comment
  explicitly citing this VUID class as the reason it exists. `ExecuteImpl` (`:322-331`) selects which set
  in the ring to write based on `CURRENT_FRAME_INDEX` when that optional input is wired, falling back to
  the swapchain `IMAGE_INDEX` otherwise. Four production instances (`compute_descriptors`,
  `direct_lighting_descriptors`, `spatial_reuse_descriptors`, `probe_update_descriptors`) all wire
  `CURRENT_FRAME_INDEX` from `FrameSyncNode`. Their consumers (`ComputeStageNode`) apply the identical
  fallback logic and are explicitly documented as needing to agree with the producer's choice.

- **The bucketing pre-pass (Path A) is NOT "a single set, bypassing the pattern" as Inc4's doc implied —
  it already IS a real `DescriptorSetNode` graph node** (`BuildRenderGraph.cpp:699`), wired through the
  normal `ShaderLibraryNode -> DescriptorResourceGathererNode -> DescriptorSetNode -> ComputePipelineNode`
  pipeline exactly like the 4 precedents. **What's actually missing is one wire**: its
  `CURRENT_FRAME_INDEX` input is left unconnected (`BuildRenderGraph.cpp:5386-5412`, the omission is
  explicitly commented), while its OWN consumers (`recipe_bucketing_mode_init/bucket/final`
  `ComputeStageNode`s) DO have `CURRENT_FRAME_INDEX` wired (`:5473-5474`) — so producer and consumer
  disagree on which ring slot is "current" (producer effectively cycles by image index 0-2, consumer by
  frame index 0-3), which is the real, more precise, more mechanically fixable root cause. This is a
  smaller, better-understood gap than "doesn't use the pattern at all."

- **The specialized-pipeline promotion path (Path B) is genuinely NOT a place `DescriptorSetNode` can be
  reused directly**, and this is a structural fact, not a preference. Its descriptor set is created inside
  `VulkanGraphApplication::RunRecipeBucketedDispatchPreTick()`
  (`VulkanGraphApplication.cpp:729-837` — pool creation, allocation, the `vkDeviceWaitIdle` + write), which
  runs inside `PreTick()`, strictly BEFORE `RenderFrame()` (the graph's Compile/Execute lifecycle) each
  tick — confirmed via the app's own tick order (`PreTick -> Update -> Render -> PostTick`) and
  `RenderGraph`'s own rule that `AddNode`/`ConnectNodes` cannot happen after `Compile()`. The promotion
  path's shader/descriptor layout is only known at RUNTIME, per-recipeId, the first time that recipe is
  promoted — there is no fixed shader for a static `ShaderLibraryNode` to reflect at graph-build time.
  "Just use `DescriptorSetNode` as a node" is not available here; only its ring-depth/index-selection
  LOGIC could be extracted into a reusable helper for imperative (non-graph) callers — a real, if modest,
  design task, not a one-line change.

- **Path A, not Path B, is the dominant (possibly sole) source of the observed VUID count.** Inc4's own
  Opus re-validator ran a zero-promotion repro (flag-set, no recipe ever promotes, so Path B's write code
  never executes) and the VUID still fired 20×— proving Path A's per-frame producer/consumer ring-index
  mismatch is the real, recurring source. Path B's one-time-per-recipeId write (already `vkDeviceWaitIdle`-
  guarded) may or may not contribute anything additional — **this has never been isolated and measured**.

- **A second, independent, already-existing mechanism for the same VUID class was found**:
  `SwapChainNode.cpp:181-198` implements a per-image `imagesInFlight` fence wait (the canonical
  Vulkan-tutorial pattern), citing the same VUID among others. This coexists with `DescriptorSetNode`'s
  ring-sizing approach today and jointly explains why the 4 pre-existing instances show zero occurrences.
  Worth a quick check during M1 implementation: confirm this doesn't already partially cover the
  bucketing pre-pass's image-acquisition timing in some way that changes the fix.

## Milestone Map

- **M1 — Wire `CURRENT_FRAME_INDEX` into the bucketing pre-pass's `DescriptorSetNode`, measure what's
  left.** Small, mechanical, one wiring connection at `BuildRenderGraph.cpp:~5412` mirroring the 4
  existing precedents exactly (`:6173`, `:6315`, `:6532`, `:6888`). **Live-run gate mandatory** (this
  program's own established discipline: every milestone touching production shader/graph wiring must be
  proven live, not just by standalone test). Confirm via the real `VixenApp`, flag SET
  (`VIXEN_RECIPE_BUCKETED_DISPATCH=1`), hot+cold demo scene, validation layers on, sustained run: does the
  `03047` count drop to the flag-unset baseline (0)? Report the exact before/after count. This
  measurement is what M2's scope depends on.
  - [ ] Not started.
- **M2 — Path B's residual, scoped based on M1's measurement.** NOT scoped in detail yet. Three possible
  outcomes once M1's number is in:
  - If M1's fix already brings the count to 0 (or to a number matching only the 3 known
    promotion-events in the demo scene, i.e. Path B contributes nothing beyond its own already-
    `vkDeviceWaitIdle`-guarded one-time writes): **this increment may be DONE after M1** — document Path
    B's residual (if any) as accepted, already-safe, non-blocking, same treatment it already has in
    Inc4's doc, and close the increment without further code changes.
  - If a real, recurring (not just once-per-recipeId) residual remains attributable to Path B: scope a
    real M2 — likely extracting `DescriptorSetNode`'s ring-depth/index-selection logic into a small
    reusable non-node helper `RunRecipeBucketedDispatchPreTick` can call directly, changing its
    descriptor set from a single one-time-allocated set to a ring, and confronting the "buffer handles
    never change, so why version by frame" question the current one-time-write design's own comment
    raises (this needs a real answer, not just churn-for-churn's-sake, before writing the fix).
  - If the residual is real but only fires a small, self-limited number of times matching exactly the
    number of DISTINCT recipes ever promoted (not per-frame) — this may itself be judged acceptable
    non-blocking debt, same bar Inc4 already applied to the whole VUID before this increment existed;
    the decision point is real per-frame recurrence vs. one-time-per-promotion-event.
  - [ ] Not scoped — depends on M1.

## Tasks

### M1 — Wire the bucketing pre-pass's frame index, measure residual

**Task 1.** In `BuildRenderGraph.cpp`, find the `recipeBucketingDescriptorSet`/`recipe_bucketing_
descriptors` `DescriptorSetNode` construction/connect block (~line 5386-5412) and add the same
`CURRENT_FRAME_INDEX` wire the 4 existing production `DescriptorSetNode` instances already use — connect
`frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX` to
`recipeBucketingDescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX`, matching the exact pattern
at `:6173`/`:6315`/`:6532`/`:6888`. Do not invent a new wiring convention — copy the existing one exactly.

**Verify the producer/consumer ring-index agreement directly**: confirm (by reading the code, and live)
that `recipe_bucketing_mode_init/bucket/final`'s `ComputeStageNode` consumers, which already have
`CURRENT_FRAME_INDEX` wired (`:5473-5474`), now agree with the producer's newly-wired selection — this is
the actual invariant being restored, not just "add a wire and hope."

**Gate**: real `VixenApp`, Windows-native, discrete GPU, validation layers on, flag SET
(`VIXEN_RECIPE_BUCKETED_DISPATCH=1`), the existing hot+cold demo scene (`VIXEN_RECIPE_HOT_COLD_DEMO=1`),
sustained run (thousands of frames, mirroring Inc4's own bar). Report the EXACT `VUID-vkUpdateDescriptorSets-
None-03047` occurrence count before and after this change, and whether it matches the flag-unset baseline
(0) or some other number. Also confirm zero new validation-error classes and zero regressions in the
existing render/dispatch test suites (same suites Inc4's M3 validated:
`test_rendergraph_criticalnodes_gpurender1`, `test_recipe_pool_render`, `test_mip_fallback_render`,
`test_rendergraph_dispatch`, `test_recipe_multi_bucket_compositing`).

**This measurement is the deliverable, not just the fix.** M2 cannot be scoped without this number.

## Risks / decision points

- **Do not assume Path B needs the same treatment as Path A before M1's number is in.** This increment
  exists specifically to avoid repeating Inc4's own early mistake (assuming a "small ask" — GPU-LRU
  eviction — had no hidden prerequisites, then discovering 3 real ones only after digging in). Here the
  digging-in already happened BEFORE dispatch, and it already found the two paths are asymmetric in
  difficulty — respect that finding by gating M2's scope on real measurement, not by scoping both
  upfront out of a desire for certainty.
- **If M1's fix doesn't fully close the VUID to zero, do not immediately conclude Path B is guilty.**
  Re-run the zero-promotion repro Inc4's own re-validator used (flag-set, no recipe ever promotes) AFTER
  M1's fix to isolate whether any residual is genuinely Path B's or some third factor neither path
  research anticipated.
- **The one-time-write design in Path B has its own stated rationale** ("buffer handles never change
  after creation, so no per-frame rewrite is needed" — `VulkanGraphApplication.cpp:804-809`) — if M2 ever
  becomes real work, don't discard that rationale reflexively; a ring-based rewrite only makes sense if
  the actual hazard (a still-in-flight command buffer reading the OLD descriptor set while a NEW promotion
  event overwrites it) can occur, which needs to be checked against how promotion events are actually
  spaced in real usage, not assumed.
- **`SwapChainNode`'s independent `imagesInFlight` fence-wait mechanism** (`SwapChainNode.cpp:181-198`)
  already coexists with `DescriptorSetNode`'s ring approach across the whole engine and may already be
  doing some of the real work here — don't treat this increment as introducing serialization from
  scratch; check what's already covered before adding anything new.
