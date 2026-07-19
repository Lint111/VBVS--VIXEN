# Precision Tier Widening (M3) — Direction (2026-07-18)

> **Status: FUTURE DIRECTION, not yet scoped into a plan.** Spun off from
> [[Recipe-Load-Tier-Contract-Direction-2026-07]] while its M2 (the precision tier) was in flight.
> User idea, 2026-07-18: don't stop at a binary half/full-precision switch — offer a small set of
> named precision tiers, including a **packed-composite** layout that stacks multiple params into
> one wider field, not just a narrower float format per param. User explicitly chose to let M2 land
> as scoped (half/full only) first, then widen here — this doc exists so the idea isn't lost, not as
> an increment ready to pick up.

## 1. What M2 already proved, and what this widens

M2 ([[Recipe-Load-Tier-Contract-Direction-2026-07]] Milestone Map, commit `d7bd8811`, Opus-approved)
shipped exactly two layouts per opted-in `[GpuStruct]`: full-precision (unchanged) and half-precision
(`packHalf2x16`-packed pairs into `uint32`, core GLSL, no device-feature dependency), selected
per-instance at runtime via the same `raySizeCoef`/`raySizeBias`/`cameraPos` footprint signal M1's
gating tier already threads into `RecipeInstanceBucketing.comp`. The mechanism (per-field
`[PrecisionEligible]` opt-in, dual-layout codegen off ONE schema, additive precision-bucketed
dispatch keyed `recipeId*2+tier`) is proven end-to-end on real hardware for exactly ONE non-full tier.

This doc's idea: **generalize from "1 alternate tier" to "N named tiers,"** and add a genuinely
different kind of tier — not just a narrower per-field float format, but a **packed-composite**
layout that stacks multiple logical params into one physical slot (e.g. 4 short/half-width params
packed into a single `double`/`vec4`-width field), trading per-param precision for a different kind
of bandwidth/slot-count win than halving a single field's own width.

## 2. Why packed-composite is a different transform, not just "one more narrow format"

M2's half-precision emission narrows ONE field's own type (`float` -> half, 2 packed per `uint32`) —
the field count and field identity stay 1:1 with the schema. A packed-composite tier instead **merges
multiple distinct logical fields into one physical slot** — this is a structurally different
transform:

- M2's transform: `struct { float a; float b; }` -> `struct { uint ab_packed; }` where `ab_packed`
  IS `a` and `b`, just narrower and interleaved. One GLSL unpack call yields both back.
- A packed-composite transform: `struct { float a, b, c, d; }` (4 SEPARATE logical fields, each
  independently meaningful to the recipe) -> one `vec4`/`double`-width slot holding all 4 at reduced
  precision each. The unpack site needs to know it's unpacking 4 independent values, not 2 halves of
  a conceptual pair.

**This doc does NOT assume `GpuStructModel`/`FieldShapeRecognizer`'s dual-layout mechanism (from M2)
generalizes for free to this shape** — confirming whether it does, or needs rework, is explicitly the
first open question for whoever scopes this (§4).

## 3. Candidate named tiers (a short list, not an open-ended format matrix)

Per the user's framing ("more choice... short or even composite params stacked in the same byte"),
a plausible short list to choose from when scoping — **not a decision made here**:

- **Full** (existing, M2's tier 0) — unchanged float32 per field.
- **Half** (existing, M2's tier 1) — `packHalf2x16`, 2 fields per `uint32`, already proven.
- **Short/quantized** — a narrower-than-half integer encoding (e.g. a fixed-point 16-bit or 8-bit
  quantization with an explicit range) for fields where even half-float's exponent range is
  overkill (a normalized `[0,1]` shape param, say) — NOT yet grounded against any specific recipe
  field's actual value range; needs real fields to check against before assuming this is a net win
  over half.
- **Packed-composite** — N independent fields stacked into one wider slot (the user's specific ask),
  at whatever per-field precision the packing scheme allows (e.g. 4×8-bit in one `uint32`, or
  4×16-bit in one `double`-width slot as the user's own example framed it).

## 4. Open questions (real, unresolved — do not assume answers when scoping)

- **Does M2's dual-layout codegen mechanism generalize to N tiers, or does packed-composite need its
  own emitter path?** M2's `GpuStructPrecisionEmitter` was built for exactly 2 layouts from one
  schema. Extending to N named tiers (full/half/short/packed) is at minimum a signature change; going
  from "narrow one field" to "merge several fields into one slot" is a different kind of model
  transform (§2) — confirm this by reading `GpuStructPrecisionEmitter.cs`'s actual structure before
  assuming either "trivially extends" or "needs a full rewrite."
- **Is packing per-field-declared (like M1's/M2's own opt-in fields) or a separate schema-level
  grouping construct?** A single `[PrecisionEligible]`-style marker per field doesn't capture "these
  4 specific fields pack together" — that's a GROUP relationship, not a per-field one. Needs its own
  attribute shape (e.g. `[PackedWith(...)]` naming which fields share a slot, or an explicit group
  block) — not yet designed.
- **Which recipe fields, in practice, actually benefit from more than 2 tiers?** M2's own prototype
  used all-3-fields-eligible on `RecipeParams` (radius/displaceAmp/displaceFreq) with no field to
  withhold. Before scoping a build, identify at least one real recipe/field set where a 3rd or 4th
  tier is plausibly worth its added complexity — don't build tier granularity speculatively.
- **Does this need measured bandwidth evidence before design locks in?** M2's own Milestone Map entry
  is explicit that its ~33% bandwidth claim was demonstrated "at the codegen level," with NO
  upload-path A/B measurement at scale (no dispatch consumer was built). Per this program's own
  evidence-before-building discipline, a widening to MORE tiers should have LESS speculative
  justification than M2's already-unmeasured-at-scale starting point, not more — consider whether
  M2's own deferred bandwidth measurement should happen before adding more tiers on top of an
  unmeasured foundation.
- **Runtime dispatch-bucket growth**: M2's precision bucketing is already a second bucket dimension
  keyed `recipeId*2+tier` (2 tiers). N named tiers means `recipeId*N+tier` — confirm this scales
  cleanly (buffer sizing, bucket-count math) rather than assuming it's a trivial constant change.

## 5. Suggested first step, if picked up (mirrors this program's established discipline)

Per the same discipline M1/M2 both followed (hand-author one thing before generalizing): **do not
jump to implementing all candidate tiers or a general N-tier framework.** First:

1. Pick ONE additional tier beyond M2's existing full/half (the packed-composite tier is the user's
   own specific ask, and the more novel transform worth proving — leading candidate over
   short/quantized, which is a smaller variation on already-proven work).
2. Read `GpuStructPrecisionEmitter.cs` and `FieldShapeRecognizer`/`GpuStructModel.StructLayout`
   directly (not from memory of M2) to determine whether packed-composite fits as an extension of
   the existing emitter or needs a new one — this determines the actual scope before committing to
   an implementation plan.
3. Hand-author the packed-composite tier on ONE recipe/field-group (reuse `RecipeParams` or a
   purpose-built prototype struct with 4 genuinely independent fields, unlike M2's 3-field struct
   with no field to withhold) and prove it end-to-end on real hardware (GPU-verified test, same
   near/far/control discipline M1 and M2 both used) before considering a 3rd or 4th named tier.
3. Only after that prototype is proven: evaluate whether a general "declare N tiers, including
   packed groups" authoring shape is worth building, or whether hand-authored packed-composite
   recipes remain the right scope for now.

## Related

- [[Recipe-Load-Tier-Contract-Direction-2026-07]] — the parent direction; M1 (gating tier) and M2
  (precision tier, full/half) are DONE + Opus-APPROVED and merged to `main` (`3b3098c6`). This doc
  widens M2's precision axis specifically; it does not touch M1's gating tier or the still-deferred
  content-detail tier.
- [[GPU-Struct-Precision-Tiering-Direction-2026-07]] — M2's original technical grounding; this doc's
  §3/§4 extend that direction's own open questions rather than replacing them.

## Provenance

- User idea, 2026-07-18, while M2 (precision tier) was in flight: "maybe we should offer more choice
  in the precision upload type? for example currently its double float, we can also offer short or
  even composite params stacked in the same byte such as double containing 4 stacked params." User
  chose to let M2 land as scoped first ("let M2 finish as scoped, widen in M3"), then scope this as
  its own direction doc once M2 was merged.
