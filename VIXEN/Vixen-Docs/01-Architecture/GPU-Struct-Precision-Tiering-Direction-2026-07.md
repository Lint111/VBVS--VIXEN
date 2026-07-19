# GPU Struct Precision Tiering — Direction

> **Status: FUTURE / not scoped for build.** Spun off from a user idea 2026-07-13 during
> [[View-ReadModel-Codegen-Plan-2026-07]] Milestone 0 (the Bodies/Recipes wire split). Captured here
> as a named direction so it isn't lost, not as an increment ready to pick up. Gated on nothing
> specific yet — pick this up when procedural-recipe render-param bandwidth (to GPU) is actually
> measured as a bottleneck at high instance counts, the same evidence-before-building discipline
> [[runtime-tiered-recipe-pipeline-jit-direction]] used for its own switch-scaling gate.

## 1. The idea (user, 2026-07-13, verbatim intent)

Add precision handling so GPU-struct / view data passing can move data "at half the price" —
especially useful for procedural recipes at low LOD, where less precision is acceptable. Concretely:
render 1000 far instances at half precision, and the 2 nearby instances at full (double) precision,
switching **at runtime** based on live distance — not a flag baked once at codegen time.

## 2. Why this is a runtime concern, not a codegen-time attribute (resolved via precedent)

An initial framing considered a `[Precision(Half)]`-style schema attribute — clean, mirrors
`[NotView]`'s shape, but bakes ONE precision into a field forever. That's the wrong shape: distance
is a live, per-instance, per-frame quantity, not a schema-time fact.

The user pointed at this codebase's own precedent — the shipped LOD/tiering lineage
([[sparse-mip-esvo-lod-inc1-m1]], [[tiered-esvo-observer-address-direction]],
[[lazy-procedural-delta-baseline-program]]) — as the right template. Verified directly: every one of
those mechanisms selects *which resource to fetch/use* via a **live runtime check against distance**,
re-evaluated every frame (`raySizeCoef` per-ray cone-footprint check; per-tree residency triggers on
frustum/resolvability/occlusion; the future JIT epic's usage-triggered tier promotion) — never a
compile-time-chosen variant. The parallel is real **in spirit** (LOD-driven runtime selection by live
distance) even though the precedent operates on a different axis (which asset/detail-level to load,
not which byte format to read) — no existing code path branches on data format today, and "precision"
in every existing doc means float32 coordinate/traversal precision (the orbital-scale problem), not
fp16 render-param payloads. This is a genuinely new axis, patterned after an existing one.

## 3. The actual split: codegen emits variants, runtime selects them

Mirroring the precedent's shape (tier-0 universal path + tier-1 specialized path, swapped by a live
signal — see [[runtime-tiered-recipe-pipeline-jit-direction]] §2), this splits into two pieces that
should NOT be built as one:

1. **Codegen: emit dual layouts from one schema.** A new emission mode on the existing
   `GpuStructModel`/`FieldShapeRecognizer` pipeline (the same "layout is a consumer of transpilation"
   architecture [[kernel-codegen-framework-direction]] established) — mark which fields are
   precision-eligible (e.g. recipe render params: yes; a kind/enum byte: no), and emit BOTH a
   full-precision and a half-precision std430 layout of the same logical struct from one schema
   declaration, so the two can never drift apart. `FieldShapeRecognizer` itself does not change —
   this is a new consumer behavior, same as `[NotView]` is a new consumer behavior, not a recognizer
   change.
2. **Runtime: a live per-instance precision bit + bucketed dispatch.** A NEW runtime system (not
   codegen's job) derives each instance's precision tier from the SAME distance/LOD signal already
   driving residency/tier-crossing, buckets instances by tier, and dispatches each bucket against its
   matching pre-generated buffer — structurally the same shape as "sort instances by recipe, batch
   dispatch by pipeline" in the JIT direction (§6 there explicitly rejects a single-dispatch
   multi-format trick for the analogous recipe-switching problem; the same rejection likely applies
   here — expect precision-bucketed dispatch, not a single dispatch branching per-instance on format).

Piece 1 is small and mechanical, and fits naturally as a future milestone inside a codegen-focused
program. Piece 2 is the real work, and belongs closer to the tiered-recipe/LOD system than to the
codegen program — likely sequenced alongside or after
[[runtime-tiered-recipe-pipeline-jit-direction]] rather than as part of View-ReadModel-Codegen.

## 4. Open questions (not yet resolved, resolve before scoping an increment)

- Which fields are actually precision-eligible in practice? (Position/scale-like fields plausibly
  yes; anything used as an array/lookup index or enum discriminant plausibly no — needs a per-field
  opt-in, not a blanket struct-level switch.)
- Where does the per-instance precision bit get computed and threaded through — is it a natural
  extension of the residency-trigger machinery, or a new independent LOD-bucket pass?
- Half precision on the CPU→GPU upload path (bandwidth) vs. GPU-internal storage (VRAM footprint) vs.
  both — the user's framing ("passing data at half the price") suggests the upload-bandwidth case is
  the primary motivator; confirm before design locks that in.
- Does this need actual measured evidence (like the JIT epic's switch-scaling table, §8 there) before
  it's worth building, or is the bandwidth cost already visibly a bottleneck? Follow the same
  data-before-building discipline.

See [[View-ReadModel-Codegen-Plan-2026-07]] (where this idea was spun off, Milestone 0),
[[runtime-tiered-recipe-pipeline-jit-direction]] (the precedent + the closest sibling direction doc
in shape), [[kernel-codegen-framework-direction]] (the shared FieldShapeRecognizer/layout
architecture this would extend), [[sparse-mip-esvo-lod-inc1-m1]] and
[[tiered-esvo-observer-address-direction]] (the runtime-distance-selection precedent itself).
