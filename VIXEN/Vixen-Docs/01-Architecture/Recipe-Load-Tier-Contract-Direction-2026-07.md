# Recipe Load-Tier Contract — Reconciling Direction (2026-07-18)

> **Status: 🚧 PARTIAL, reconciled 2026-07-26.** The original scoping pass produced two implemented
> milestones: **M1 footprint gating ✅ DONE** and **M2 precision-layout/routing 🚧 MECHANISM DONE**.
> M2's complete specialized-precision dispatch consumer is not built. Authored **content-detail
> recipe variants remain 💡 DEFERRED**, as do async compile/swap, GPU-LRU, and generalized
> mip/lighting reuse. This document reconciles two previously-separate direction stubs that each
> proposed "distance-driven runtime selection of which recipe resource/variant/precision to use" —
> [[Recipe-Bucketing-LOD-Screen-Footprint-Reuse-Addendum-2026-07]] and
> [[GPU-Struct-Precision-Tiering-Direction-2026-07]]. It was written because the user described a "special
> recipe" abstraction — pluggable into a distance-based load ladder, reused for mip and lighting
> selection. It remains Step 1 of a user-described 3-step sequence (special/load-tiered recipes →
> unroll-mechanism A/B testing → world-streaming load/unload); the status map below is authoritative.

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

**The original scoping left the tier concrete type open. M1/M2 have now resolved two of the three
instances:** gating and precision use the same footprint signal but retain distinct buffers/consumers.
Content-detail remains the unbuilt semantic variant:

- **Content-detail tier — 💡 DEFERRED** (from the LOD addendum): a simplified RECIPE BODY at distance — fewer
  opcode steps, a coarser approximation of the same shape. Hard: interacts with
  [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]]'s codegen work if pursued, since each
  tier is a genuinely different bytecode program needing its own unrolled emission.
- **Gating tier — ✅ M1 SHIPPED** (also from the LOD addendum): not simplified content, just
  a binary/graduated threshold on whether an instance gets bucketed/promoted at all, based on
  footprint. No recipe-content changes needed — a threshold on the existing formula.
- **Precision tier — 🚧 M2 ROUTING MECHANISM SHIPPED / FULL CONSUMER OPEN** (from the
  precision-tiering direction): same recipe body, same opcode program,
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

## 3. Remaining open questions (post-M1/M2)

Resolved facts:

- M1 threads `raySizeCoef`/`raySizeBias`/`cameraPos` into the real bucketing push constants and proves
  distance/footprint gating on hardware.
- M2 reuses that signal for additive full/half precision buckets and generates both layouts from one
  `[GpuStruct]`.
- Gating and precision can coexist, but a `VkPipeline` consumes one concrete layout; the full
  specialized-precision consumer therefore needs separate dispatch plumbing.

Still open:

- **Content-detail representation:** how a recipe declares ordered semantic variants (separate VRC
  programs, shared family id, param-layout compatibility, conservative bounds, minimum representation).
- **Selection policy:** thresholds, hysteresis, retain-last/placeholder/drop fallback, and whether an
  interactable instance may ever be fully gated.
- **Complete precision consumption:** the production dispatch that actually binds/executes the half
  layout selected by M2, with a fallback when the specialized pipeline is unavailable.
- **Authoring ownership:** hand-authored variants first versus later composer tooling. Keep the
  established rule: prove one family by hand before building a composer.
- **Measured value:** real-scene transition frame-time percentiles, upload bytes, pop/continuity, and
  fallback duration. The existing bucketed-specialization path is opt-in because its measurements did
  not establish a clear win; content-detail work must not assume otherwise.
- **Mipmap/lighting generalization:** plausible, still unproven, and not a reason to force one
  abstraction across systems before the recipe-specific contract works.

For UNDERTOW integration, the sim-owned View/projection contract must provide recipe identity,
compatible typed params, conservative bounds, allowed variants, visibility, and fallback policy.
VIXEN may select presentation tiers from that projected contract; it must not query hidden
authoritative sim state to invent policy.

## 4. Recommended next slice

1. Hand-author **one two-variant content-detail recipe family** with a shared semantic identity,
   compatible typed param layout, conservative bounds, footprint thresholds, hysteresis, and an
   explicit fallback. Do not add composer tooling.
2. Route the existing M1 footprint signal to variant selection while preserving stable instance
   identity and the existing gating/precision paths. An unavailable fine variant must retain the
   coarse variant rather than silently drop an interactable target.
3. Live-prove one continuous zoom/focus transition in the real app, then measure frame-time
   percentiles, upload bytes, transition pop, fallback duration, and gate-off byte identity.
4. Only after that gate, decide whether to add more content families, finish M2's precision consumer,
   or generalize the contract toward mip/lighting.

## Related (Step 2 / Step 3 stubs, not scoped for build — parallel-scoping placeholders only)

The user described this as Step 1 of a 3-step sequence. Steps 2 and 3 are captured here as pointers
only, not designed, per "sequential build, parallel scoping so the shape is visible":

- **M3 (precision-tier widening — beyond binary half/full): scoped as its own doc,**
  [[Precision-Tier-Widening-M3-Direction-2026-07]] (2026-07-18), NOT started. User idea while M2 was
  in flight: don't stop at a binary half/full-precision switch — offer a small set of named precision
  tiers, including a packed-composite layout stacking multiple params into one wider field. See that
  doc for the full scoping (candidate tiers, open questions, suggested first step).

- **Step 2 — nested invocation + unroll-vs-natural A/B testing: scoped as its own doc,**
  [[Recipe-Nested-Invocation-Unroll-AB-Direction-2026-07]] (2026-07-18): **M1 minimal nested-invocation
  mechanism ✅ SHIPPED** (`0eb6c092`, merged by `d3d7066b`); **M2 actual unrolled-vs-natural A/B under
  nesting + flat-N pressure remains open**.
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
