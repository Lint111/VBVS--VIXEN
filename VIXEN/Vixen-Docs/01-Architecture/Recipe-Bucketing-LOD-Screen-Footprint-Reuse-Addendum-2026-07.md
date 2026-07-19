# Recipe Bucketing LOD — Screen-Footprint Reuse Addendum (2026-07-16)

> **Status: FUTURE DIRECTION, not yet scoped into milestones.** A short addendum capturing a grounded
> research finding — not a design doc, not a plan. Spawned from a user idea during
> [[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]]'s M3 closeout: "could the mip-map pattern's
> existing spatial functionality be reused for LOD in recipe bucketing?"

## The idea as asked

Recipe GPU Instance Bucketing ([[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]]) has no LOD
concept today — every instance is bucketed and screen-projected at full detail regardless of
distance/screen size. The question: could VIXEN's existing mip-map infrastructure (which already has
"some spatial functionality") be reused rather than building LOD from scratch?

## What the grounding research found (2026-07-16)

**The premise conflates three systems that share the word "mip" but store fundamentally different
data — only one is actually reusable, and it's not a mip pyramid.**

- **`MipSample`/`MipPool`** (Sparse-Mip ESVO LOD epic, shipped+wired) — a CONTENT pyramid: filtered
  SDF distance + color, ONE value per octree node, addressed by tree ordinal. **Zero spatial/extent
  payload** — no bound sphere, no AABB, no radius field anywhere in `MipSample`. The direction doc's
  own design notes are explicit this was deliberate ("indexed by level-local node ordinal... NOT
  widening the 8-byte ESVO node"). Not usable for "how much screen space does this instance occupy" —
  would require bolting on an entirely new per-level spatial field, not reading what's there.
- **`RecipeOccupancy`** — a 2-level empty-space-skip grid over ONE recipe's own field (conservative
  min-|SDF| per coarse cell). Orthogonal to instance-level screen coverage; unrelated to bucketing.
- **`raySizeCoef` screen-space-footprint formula** (`SceneBindings.glsl`, `TraceWorld.glsl:130-152`,
  CPU mirror `SVOLOD.h`) — **THIS is the real find.** `footprint = distance * raySizeCoef +
  raySizeBias`, camera-derived (`raySizeCoef = 2*tan(fov/screenHeight/2)`), already applied at
  EXACTLY the recipe/bound-sphere granularity bucketing operates at: `TraceWorld.glsl:143`'s
  `footprint >= 2.0 * boundRadius` is a sub-pixel early-out using the SAME `RecipeBoundSphere`
  convention (`center + worldPos`) `RecipeInstanceBucketing.comp` already reads and projects. This is
  a formula/convention to crib, not a stored pyramid to read from.

**The gap, precisely**: `raySizeCoef`/`raySizeBias` are NOT currently passed into
`RecipeInstanceBucketing.comp`'s push constants (only `viewProj`, `screenWidth/Height` are) — even
though the same camera data flows through the same `CameraNodeConfig`-shaped binding structure
already. And System B as it exists today is **binary** (sub-pixel or not, a hard march early-out),
not a multi-tier LOD selector — no existing system in this codebase answers "at this screen-space
size, use LOD level N" with more than 2 states for anything (ESVO's own `LOD_ENABLED` path is itself
a hard switch per its direction doc, "v2 nicety: fractional-LOD lerp... not yet built").

## The reframed, grounded opportunity

Not "reuse the mip pyramid" (there isn't a spatially-shaped one to reuse) — instead: **extend the
already-proven `raySizeCoef` screen-footprint formula from a binary march early-out into a
multi-tier LOD/hotness signal at the bucketing pre-pass level**, since:
- The formula is already camera-derived, cheap, and proven correct at bound-sphere granularity.
- The bucketing pass already has per-instance `worldPos` + per-recipe `RecipeBoundSphere` in scope —
  computing `footprint` alongside the existing screen-space coverage-AABB projection is a small
  addition (push `raySizeCoef`/`raySizeBias` into the pass's push constants, compute one extra float
  per instance), not new infrastructure.
- This could feed EITHER Increment 2/3's hotness gate (today `kHotnessThreshold=4` instances/bucket,
  a placeholder — see [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] M3) with a screen-size
  dimension alongside instance-count, OR a genuinely new LOD-tier concept (reduce recipe complexity /
  skip promotion for buckets whose footprint is small regardless of instance count).

## Explicitly NOT yet answered

- Whether combining footprint with instance-count (`k_i`) as a hotness signal is actually the right
  gating shape — no design work done, this is a spawned idea, not a decision.
- Whether "multi-tier LOD" for recipes means simplifying the RECIPE ITSELF at distance (fewer opcode
  steps — a genuinely hard problem, would need to interact with
  [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]]'s codegen work) vs. just gating
  WHETHER a recipe gets bucketed/promoted at all (much simpler — a threshold on the existing
  binary-shaped formula, no recipe-content changes needed). These are very different scopes wearing
  the same "LOD" name — worth being precise about which one is meant before scoping further.
- Relevant echo: [[gpu-precision-tiering-direction]] (half/full-precision recipe params,
  runtime distance-driven bucketing) is a SEPARATE, previously-captured direction that also wants a
  distance-driven signal for recipes — worth checking for overlap/consolidation if either is picked
  up, rather than building two independent distance-gating mechanisms.

## Relationship to other in-flight work

- Spawned during [[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]]'s M3 closeout, not part of
  that increment's scope.
- Distinct from [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]] (dispatch-selection
  cost) and [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]] (codegen duplication) — this
  is about WHETHER/HOW MUCH to render, not how dispatch or codegen work.
