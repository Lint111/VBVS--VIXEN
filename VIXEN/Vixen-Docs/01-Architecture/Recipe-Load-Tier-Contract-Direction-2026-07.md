# Recipe Load-Tier Contract — Reconciling Direction (2026-07-18)

> **Status: SCOPING DOC, not yet a plan.** Reconciles two previously-separate, unstarted direction
> stubs that each independently proposed "distance-driven runtime selection of which recipe
> resource/variant/precision to use" without checking each other for overlap —
> [[Recipe-Bucketing-LOD-Screen-Footprint-Reuse-Addendum-2026-07]] and
> [[GPU-Struct-Precision-Tiering-Direction-2026-07]]. Written because the user described a "special
> recipe" abstraction — pluggable into a distance-based load ladder, reused for mip and lighting
> selection — and asked for the existing design to be found first; grounding research
> (2026-07-18) confirmed no such consolidated doc exists yet, only these two overlapping stubs. This
> doc is Step 1 of a user-described 3-step sequence (special/load-tiered recipes → unroll-mechanism
> A/B testing → world-streaming load/unload); only this step is scoped for build now, the other two
> are stubbed separately (see Related, below) so the shape is visible without committing to them.

## Milestone Map

- **M1 (gating tier): DONE + APPROVED.** Commit `41efea64` (branch `feat/recipe-load-tier-contract`,
  worktree `recipe-load-tier-contract`). Threads `raySizeCoef`/`raySizeBias`/`cameraPos` into
  `RecipeInstanceBucketing.comp`'s push constants (same live nodes the main march's own
  push-constant gatherer already reads, including the tier-crossing-LOD-override branch). Adds a
  per-recipe opt-in `gateFootprintThreshold` (`RecipeRegistry.h`, default 0.0 = not opted in,
  rejected if set nonzero-non-positive via a new `BadGateFootprintThreshold` validation result). An
  instance whose screen-space footprint (`distance * raySizeCoef + raySizeBias`, the SAME formula
  `TraceWorld.glsl` already uses) falls below its recipe's threshold is excluded from
  bucketing/promotion for that frame; non-participating recipes are byte-identical to before.
  GPU-verified test `GatingTierExcludesFarInstanceKeepsNearInstanceAndNonParticipant` (real discrete
  GPU) confirms: far gated instance excluded, near instance on the same recipe still bucketed, and a
  non-participating control recipe at the SAME far distance unaffected. Opus-validated APPROVED
  2026-07-18 — independent re-build, independent re-run of all 4 touched test binaries (11/11
  passing, 0 regressions), independent formula/wiring/scope-discipline verification.
- **M2 (precision tier): DONE + APPROVED.** Two pieces, per [[GPU-Struct-Precision-Tiering-Direction-2026-07]]
  §3: (1) **Codegen** — a new `[PrecisionEligible]` field attribute (kernel-framework
  `Runtime/GpuStructAttributes.cs`, mirrors `[NotView]`'s exact shape/precedent) and a new
  `GpuStructPrecisionEmitter` + CLI `--struct-precision` flag that emit BOTH a full-precision and a
  half-precision (`packHalf2x16`-packed, core GLSL, no device feature needed) layout from ONE
  `[GpuStruct]` schema — `FieldShapeRecognizer`/`GpuStructModel.StructLayout` themselves unchanged, a
  new consumer behavior on top. First-ever `[GpuStruct]` schema for a recipe render-param block
  (`codegen/config-schemas/RecipeParams.cs`, all 3 fields — radius/displaceAmp/displaceFreq — marked
  eligible; no kind/index field on this struct to withhold, but the per-field mechanism itself is
  proven). Generated: `libraries/SVO/include/Recipe/generated/RecipeParams.g.h` (12B full / 8B half,
  a genuine ~33% bandwidth reduction) + `shaders/Generated/RecipeParams.glsl` (with an
  `UnpackRecipeParams` helper). (2) **Runtime** — reuses M1's exact footprint signal
  (`distance * raySizeCoef + raySizeBias`) to derive a per-instance precision tier in
  `RecipeInstanceBucketing.comp`, and buckets ADDITIVELY into a second, parallel bucket dimension
  (`PrecisionBucketCountBuffer`/`PrecisionBucketIndicesBuffer`, bindings 9-10, compound-keyed
  `recipeId*2+tier`) alongside the existing plain recipe bucket — confirmed the
  runtime-tiered-recipe-pipeline-JIT direction's §6 rejection of single-dispatch-multi-format
  reasoning transfers to precision (a `VkPipeline` reads one buffer layout, so tier routing must be a
  separate dispatch per format, not a per-thread branch). New opt-in `precisionFootprintThreshold`
  (`RecipeRegistry.h`, same "0 = not opted in" convention as `gateFootprintThreshold`,
  `BadPrecisionFootprintThreshold` validation). Wired into the live production graph
  (`BuildRenderGraph.cpp`/`VulkanGraphApplication.cpp`/`VulkanGraphApplication.h`) as an additive
  binding pair alongside M1's; a full specialized-precision-format dispatch consumer (the "give it a
  real home in `VixenApp`" depth M1's own Inc4 reached) is NOT built this milestone — out of scope
  per the prompt's own "hand-author, prove the mechanism" framing; the GPU-verified test is the
  correctness gate for the mechanism itself. GPU-verified test
  `PrecisionTierRoutesFarInstanceToHalfNearInstanceToFullNonParticipantAlwaysFull` confirms: near
  instance -> tier 0, far instance on the same opted-in recipe -> tier 1, non-participating control
  recipe at the SAME far distance always tier 0 (n=0 case), and the plain per-recipe bucket is
  byte-unaffected (additive, not a replacement). All 4 M1-touched test binaries re-run clean (12/12
  passing, 0 regressions) after updating their hand-mirrored `RecipeBoundSphere`/descriptor-binding
  shapes to match the shader's new binding count — the SAME staleness-risk class M1's own
  `PushConstants`-mirror comment already flagged (KI-034), just for a struct/descriptor-count change
  instead of a push-constant field change. The content-detail tier (§1's third tier type) remains
  explicitly DEFERRED, pending mipmap-integration finalization — not part of this direction's build
  until then. Opus-validated APPROVED 2026-07-18 — independent re-build (full configure+build,
  253/254 targets, 0 failed), independent re-run of all 4 touched test binaries on real discrete
  GPU (12/12 passing incl. M1's own gating test alongside M2's, 0 regressions), independent
  confirmation of the dual-layout codegen (no `shaderFloat16`/16-bit-storage dependency), the
  additive/non-colliding bucket-key math, the §6 single-vs-bucketed-dispatch reasoning, and that
  the kernel-framework repo's changes are genuinely uncommitted (not pushed to a shared branch).

## 0. What this reconciles, precisely

Both prior stubs independently rediscovered the same shape — **runtime, per-instance, live-distance-
driven selection of which resource a recipe instance uses** — from two different angles:

- The **LOD addendum** asked: should distant recipe instances render simplified content, or just be
  gated on/off for bucketing? Found the reusable primitive is the `raySizeCoef` screen-footprint
  formula (`SceneBindings.glsl`/`TraceWorld.glsl:130-152`, camera-derived, already used as a binary
  march early-out), NOT a stored mip pyramid (`MipSample`/`MipPool` has zero spatial/extent payload
  and isn't reusable for this).
- The **precision-tiering direction** asked: can far instances upload/store recipe render params at
  half precision, switching per-instance at runtime based on live distance? Same "runtime selection
  driven by live distance signal" shape, explicitly patterned after this codebase's own shipped LOD
  precedent (Sparse-Mip ESVO LOD, Tiered-ESVO Observer Addressing).

Both stubs explicitly namecheck each other as "worth checking for overlap" and neither did the
checking. They are the same underlying mechanism at two different granularities (recipe-detail-level
selection vs. render-param-precision selection) — this doc treats them as two INSTANCES of one
shared contract, not two separate systems to build independently.

**Explicitly NOT part of this reconciliation** (checked and ruled out as unrelated, despite
superficial "tiering"/"unroll" name overlap):
- [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]]'s tier-0/tier-1 — that tiering axis is
  usage-HOTNESS-driven (compilation/specialization), not distance-driven. Different trigger, different
  mechanism, already shipping independently. Do not conflate.
- [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]] — dispatch-selection cost between
  already-unrolled recipes. Premise killed 2026-07-18 (switch-dispatch cost ruled out as the driver).
  Unrelated axis (how dispatch works, not what gets rendered).
- [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]] — codegen-mechanism duplication (GLSL
  vs. HLSL emitters both hand-write the same bytecode-walk). Orthogonal: about HOW recipes compile to
  straight-line code, not about WHICH variant/precision/detail-level gets selected at runtime. Relevant
  only as future context for Step 2 (below), not this step.

## 1. The core abstraction: a "load-tier contract" on a recipe

A recipe that opts into this contract declares (at minimum) **more than one resource/variant it can
be evaluated as**, tagged by an ordered tier, and exposes enough information for a runtime system to
pick a tier from a live distance/footprint signal — without forcing every recipe to participate
(mirrors the spatial contract's own opt-in shape from [[Recipe-Spatial-Contract-Two-Pass-Culling-Direction-2026-07]]:
recipes with no declared tiers keep working exactly as today, n=0 case).

**What "a tier" is, concretely, is intentionally left open at this scoping stage** — the two source
stubs propose two different answers, and this doc's job is to confirm they're the same abstraction at
different granularities, not to force a premature choice between them:

- **Content-detail tier** (from the LOD addendum): a simplified RECIPE BODY at distance — fewer
  opcode steps, a coarser approximation of the same shape. Hard: interacts with
  [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]]'s codegen work if pursued, since each
  tier is a genuinely different bytecode program needing its own unrolled emission.
- **Gating tier** (also from the LOD addendum, the simpler alternative): not simplified content, just
  a binary/graduated threshold on whether an instance gets bucketed/promoted at all, based on
  footprint. No recipe-content changes needed — a threshold on the existing formula.
- **Precision tier** (from the precision-tiering direction): same recipe body, same opcode program,
  but render-param upload/storage at half vs. full float precision, selected per-instance.

These are NOT mutually exclusive — a recipe could plausibly declare gating AND precision tiers
without content-detail tiers, or all three. The contract's job is to give each of these a common
"how do I declare a tier, how does the runtime pick one from a live distance signal" shape, so mip
selection, lighting, and recipe-detail selection can all be instances of the SAME mechanism (per the
user's own framing) rather than three independent ad-hoc distance checks.

## 2. Why "reused for mip and lighting" is plausible, not yet proven

The user's framing — that this mechanism should be reusable for load-based mipmap selection and
lighting functionality, not just recipes — matches the LOD addendum's own finding: `raySizeCoef` is
already a camera-derived, distance-driven signal used today ONLY as a binary march early-out in
`TraceWorld.glsl`, and the codebase's existing LOD lineage (Sparse-Mip ESVO, Tiered-ESVO Observer
Addressing) already selects "which resource to use" via live per-frame distance checks — just each
via its own bespoke mechanism, not a shared contract.

**What this scoping doc does NOT do**: design the mip/lighting reuse. That would mean generalizing
this contract beyond recipes into the ESVO mip system and the lighting/ReSTIR system — a much larger
blast radius than "recipes opt into a tier contract." The right sequencing (per this doc's own
scope): build the contract for recipes first (where two independent stubs already want it and the
user's immediate need is), prove it out, THEN evaluate whether the same shape genuinely generalizes to
mip/lighting or whether those systems' own distance-selection mechanisms are different enough in
practice that forcing them into one contract would be the wrong abstraction. Do not build the
generalized version speculatively before the recipe-specific version exists and is validated.

## 3. Explicitly NOT yet answered (real open questions, do not assume answers)

Carried forward from both source stubs, not yet resolved by this reconciliation:

- **Content-detail tier vs. gating tier vs. precision tier — which is Step 1's actual first build
  target?** These have very different implementation costs (gating is a threshold on an existing
  formula; content-detail requires per-tier bytecode + codegen; precision requires dual-layout
  codegen per [[GPU-Struct-Precision-Tiering-Direction-2026-07]] §3). This doc does not pick one —
  that's the next scoping step, informed by which the user actually needs first for "grass, terrain,
  city, biomes" content (plausibly gating first, since it's cheapest and already has a formula to
  extend, but this is a recommendation, not a decision made here).
- **Where does the per-instance tier signal get computed and threaded through?** Natural extension of
  existing residency-trigger machinery, or a new independent per-recipe-instance pass? Both source
  stubs left this open; the answer likely differs by which tier type is chosen (content-detail tiers
  need this earlier, at bucketing time; precision tiers could plausibly be threaded through
  `SetInstances()`'s existing per-frame update path used by Increment 6's `ReadParam` mechanism).
- **`raySizeCoef`/`raySizeBias` are not currently passed into `RecipeInstanceBucketing.comp`'s push
  constants** (only `viewProj`, `screenWidth/Height` are) — a real, small, concrete gap identified by
  the LOD addendum that any content-detail or gating tier implementation will need to close first.
- **Enforcement/composer question, inherited from the spatial-contract precedent**: who decides how
  many tiers a recipe declares and where the thresholds sit — hand-authored per recipe (matching this
  program's own established "prove it by hand first" discipline — see
  [[Recipe-Diversity-Stress-Scene-Inc6-Plan-2026-07]] M1's spatial-contract prototype precedent) or
  some later authoring/composer tool? Per that same precedent, do NOT build composer tooling before a
  hand-authored prototype proves the mechanism.
- **Does this need measured evidence before design locks in**, per this program's own
  evidence-before-building discipline (the switch-scaling gate, the Inc6 M4 sweep, the switch-cost
  knee pre-check all followed this)? Plausibly yes for the precision-tier question specifically (is
  upload bandwidth actually measured as a bottleneck at realistic instance counts? — the precision
  doc's own stated gating condition, still unmet) but plausibly less needed for the gating-tier
  question (the "many registered recipes, only some instantiated" problem is already concretely felt —
  Inc6 M2's 192-instance-ceiling handling is a live, already-encountered instance of exactly this
  need, just solved with a flat cap rather than a distance-driven tier).

## 4. Suggested first step, if picked up (mirrors this program's own established discipline)

Per the pattern that worked for [[Recipe-Diversity-Stress-Scene-Inc6-Plan-2026-07]] M1 (hand-author
ONE recipe with the new contract before any tooling) and the switch-cost-knee pre-check (answer the
narrow empirical question before scoping an increment): **do not jump to implementing all three tier
types.** First:

1. Resolve §3's first open question — pick ONE tier type to prototype (gating tier is the leading
   candidate: cheapest, closes an already-identified concrete gap, doesn't require new codegen work).
2. Hand-author the contract on ONE existing recipe (or reuse an Increment-6-style generated one),
   threading `raySizeCoef`/`raySizeBias` into the bucketing pre-pass's push constants, and confirm a
   distance-driven gate actually changes bucketing/instantiation behavior correctly on real hardware —
   a live-run gate, per this program's own mandatory-live-gate discipline.
3. Only after that prototype is proven: evaluate whether content-detail and precision tiers fit the
   same contract shape cleanly, or need their own adjustments.

## Related (Step 2 / Step 3 stubs, not scoped for build — parallel-scoping placeholders only)

The user described this as Step 1 of a 3-step sequence. Steps 2 and 3 are captured here as pointers
only, not designed, per "sequential build, parallel scoping so the shape is visible":

- **M3 (precision-tier widening — beyond binary half/full): scoped as its own doc,**
  [[Precision-Tier-Widening-M3-Direction-2026-07]] (2026-07-18), NOT started. User idea while M2 was
  in flight: don't stop at a binary half/full-precision switch — offer a small set of named precision
  tiers, including a packed-composite layout stacking multiple params into one wider field. See that
  doc for the full scoping (candidate tiers, open questions, suggested first step).

- **Step 2 — nested invocation + unroll-vs-natural A/B testing: scoped as its own doc,**
  [[Recipe-Nested-Invocation-Unroll-AB-Direction-2026-07]] (2026-07-18), NOT started. Grounding
  research found recipe-calling-recipe does not exist in this codebase today (no opcode, no
  interpreter support, no unrolled-emitter precedent) — the doc scopes M1 (build a minimal nesting
  mechanism) and M2 (the actual unrolled-vs-natural A/B test under nesting + flat-N pressure) as two
  sequential milestones, per explicit user direction after this gap was surfaced.
- **Step 3 — world-streaming load/unload of unrolled recipes at runtime.** Register/evict recipes (and
  their compiled tier variants) as a streaming world's working set changes — a live-churn cost this
  session flagged as genuinely untested by every existing measurement (Inc3 M0, the switch-cost
  pre-check, and Inc6's whole sweep were all static-N; none touched recompile-on-register/evict cost
  under continuous churn). Depends on Step 1 (a recipe needs a load-tier contract before "which tier to
  stream in" is a meaningful question) and likely Step 2 (whether unrolled variants are cheap enough to
  churn depends on whether unrolling holds up under composition pressure). Not yet its own doc — needs
  dedicated grounding research (what does registering/evicting a recipe from the tier-0 switch actually
  cost today — presumably a shader recompile, unconfirmed) before any real scoping.

## Provenance

- User idea, 2026-07-18: "special recipes" pluggable into a distance-based load ladder, reused for
  mip/lighting selection, as Step 1 of a 3-step sequence (load-tiered recipes → unroll A/B → world
  streaming). User pointed at "there was already a design doc" — grounding research (Explore agent,
  2026-07-18) confirmed no such consolidated doc exists; found instead the two overlapping,
  unreconciled stubs this doc consolidates.
- [[Recipe-Bucketing-LOD-Screen-Footprint-Reuse-Addendum-2026-07]] (2026-07-16) — LOD/gating angle.
- [[GPU-Struct-Precision-Tiering-Direction-2026-07]] (2026-07-13) — precision angle.
- [[Recipe-Diversity-Stress-Scene-Inc6-Plan-2026-07]] — hand-author-first prototyping discipline
  precedent (M1), 192-instance-ceiling precedent (M2) as a concrete example of the gating-tier need
  already being felt.
- [[Recipe-Spatial-Contract-Two-Pass-Culling-Direction-2026-07]] — opt-in contract shape precedent
  (n=0 case, out-param convention) this doc's §1 borrows from.
