# Recipe Spatial Contract + Two-Pass Culling — Direction (2026-07-16)

> **Status: FUTURE DIRECTION, not yet scoped into milestones.** Synthesizes three research threads
> from the same session (tag/accessor extraction-point feasibility, mip/LOD-screen-footprint reuse,
> ESVO-tree-as-bucket feasibility) plus several rounds of user refinement into one coherent
> architecture idea. Not started — no design doc, no implementation plan. Spawned during
> [[Recipe-Bucketed-Dispatch-Overhead-Inc3-Plan-2026-07]]'s M3 closeout, not part of that increment's
> scope.

## The idea, in its final, converged form

Require that a recipe wanting to be **spatially useful and efficient** (eligible for tree-based
bucketing, LOD selection, prefetch/eviction) declare, EARLY in its own opcode stream, its
**local-to-world (L2W) transform and AABB** — computed via the recipe's own opcodes, parameterized
(not baked to compile-time literals, so per-instance variation doesn't force a recompile — reusing
[[Recipe-Parameterization-Plan-2026-07]]'s already-shipped `ReadParam`/`ReadParamFloat3` pattern), and
exposed via the unroll mechanism as **property-accessor-shaped outputs** (mirroring the existing
multi-`out`-param GLSL convention already shipped in `UberShaderSplice.h`'s `getRecipeBoundSphere`/
`getRecipeOccupancyGrid`).

**The key structural move, reached through iteration**: partition a contract-declaring recipe's own
bytecode into two ordered segments —
- **`[0, n)` — the META segment**: computes ONLY what a culling/dispatch pass needs (L2W transform,
  AABB, any other declared spatial/gating info), cheap by construction since the recipe author
  controls exactly where the boundary sits.
- **`[n, m)` — the RESOLVE segment**: the actual SDF field evaluation (today's full recipe), which
  only needs to run for instances that survive culling.

This makes the render pipeline genuinely **two-pass**, generalizing what
[[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]]'s M1 pre-pass already does today (cheap flat
`worldPos`+`boundCenter` external data → bucket, THEN only hot buckets get the expensive specialized
shader) — except the "cheap prefix" becomes part of the recipe itself, declared and computed rather
than externally-supplied uniform data, while staying capped at a known-cheap boundary the author
controls.

**Contract is opt-in, not universal**: recipes that don't declare a meta segment (`n=0`) keep working
exactly as today — flat `worldPos` + per-recipe `boundCenter`/`boundRadius`, no tree participation, no
LOD tiering. Nothing is forced on simple recipes that don't need computed/animated placement. Once a
recipe DOES opt in, the contract is uniform — every opted-in recipe's meta segment is walked and read
the same way by the culling pass, regardless of what's inside it.

## Why this resolves three separate problems this session surfaced

**1. Solves the "implicit vs. explicit placement" tension** (early discussion this session). Neither
pure implicit (today's flat `worldPos`, free but can't express computed/rotated/animated placement)
nor pure explicit (placement buried anywhere in a recipe's bytecode, defeats cheap pre-pass culling)
was fully satisfying. The two-pass contract gets both: uniform, cheap, early-extractable data for the
culling pass (implicit's virtue) while allowing recipes to COMPUTE that data via real opcodes,
including parameterized/animated transforms (explicit's virtue) — because the boundary between
"cheap enough to run per-instance every frame" and "only for survivors" is a declared contract, not
an assumption.

**2. Grounds and resolves the tag/accessor-opcode feasibility question** (research round: "is there a
partial-evaluation capability, would a tag survive the GLSL unroll"). Confirmed findings, directly
reusable here:
- No partial-evaluation capability exists today (`evalRecipe`/`EmitProceduralFieldFunctionGlsl` are
  both unconditional full walks, `SdfRecipeEval.h:35`, `SdfRecipeCodegenGlsl.h:55`) — but this
  contract idea doesn't strictly need early EXIT, per the same research: since GLSL locals
  (`t0`,`t1`,`pp3`...) stay in scope for the rest of an emitted function body, exposing meta-segment
  values as `out` params can be done INLINE during the single walk (assign to the `out` param the
  moment the meta segment's steps produce it, keep walking to the resolve segment's own return) —
  no early-exit machinery needed, a much smaller change than it first sounds.
- `Output`/`Passthrough` opcodes already exist as zero-payload, zero-arity marker opcodes in BOTH the
  CPU VM and the GLSL emitter (`SdfRecipeEval.h:469-471,497-499`, mirrored in
  `SdfRecipeCodegenGlsl.h:731-734,779-781`) — structurally exactly the "marker in the instruction
  stream" shape a meta-segment boundary or a property-accessor tag needs. They'd need new payload
  fields (each `SdfInstruction` has spare `data[]` floats) and new dispatch logic to actually do
  something, but the opcode-table slot and precedent already exist.
- Real, ALREADY-SHIPPED precedent for multi-output GLSL functions exists:
  `UberShaderSplice.h:105-106`'s `getRecipeBoundSphere(uint recipeId, out vec3 center, out float
  radius, out float relaxation)` and `:127-128`'s `getRecipeOccupancyGrid(...)` (4 `out` params) —
  both per-recipeId `switch`-and-return functions. Note the difference from what this idea needs:
  those two return CPU-baked, position-INDEPENDENT constants (from `RecipeEntry` fields, computed
  once at registration); this idea's meta segment needs POSITION-DEPENDENT values computed by
  executing real opcode steps. The precedent proves the `out`-param CONVENTION works in this
  codebase's codegen; it doesn't prove position-dependent multi-output is already solved — that part
  is genuinely new, but structurally small (per the inline-assignment point above).
- **Genuinely NOT solved by any existing code, still open**: how a recipe COMPOSER (the thing that
  decides where opcode N's boundary sits between meta and resolve segments) gets built — this is
  new authoring/tooling work, not just an emitter change.

**3. Reframes the "reuse the ESVO tree as a bucket" idea correctly, avoiding a real dead end.**
Research confirmed the ESVO tree and recipe/body instances are **completely disconnected systems
today** — zero existing linkage in either direction, no point-location/spatial-hash-into-tree query
anywhere, and `ChildDescriptor` (the 8-byte ESVO node) is confirmed at literally zero spare bits,
already triple-context-overloaded (`SVOTypes.h:36-141`, `static_assert(sizeof(ChildDescriptor) ==
8)` at line 154). Retrofitting occupant-references onto existing tree nodes would require inventing
an entire spatial query this codebase has never built. **This contract idea sidesteps that dead end
entirely**: instead of asking "which existing tree node does this un-baked instance occupy" (no
query exists, would need building from scratch), the recipe DECLARES its own AABB/transform directly
— no tree-node lookup needed at all. If tree-based bucketing is still wanted later, the declared AABB
is exactly the input a NEW, purpose-built spatial structure would need — this doesn't require
reusing or retrofitting the ESVO tree specifically.
- Also confirmed real, reusable precedent for whatever NEW per-instance-derived spatial data storage
  this idea produces: whenever this codebase has needed new per-node/per-instance data without
  widening an existing fixed-size struct, it went into a **parallel out-of-band SoA array, indexed by
  ordinal** (`MipPool`, per `MipBake.h:56-63`) — not packed into the struct itself. This is the
  established idiom to follow for storing extracted meta-segment results (transform/AABB per
  instance), not a new pattern to invent.
- Also confirmed: the one genuinely reusable formula across ALL of this session's spatial-adjacent
  research is `raySizeCoef`'s screen-footprint math (`SceneBindings.glsl:197,291-299`,
  `TraceWorld.glsl:130-152`) — camera-derived, already proven at recipe/bound-sphere granularity,
  already the exact shape an LOD-tier decision on a declared AABB would want to reuse. See
  [[Recipe-Bucketing-LOD-Screen-Footprint-Reuse-Addendum-2026-07]] for the full grounding on this
  specific piece.
- The real brick-residency precedent this session checked (`ResidencyTrigger.h`'s
  `InstanceWantsBrickResidency`) is whole-instance, reactive, distance+frustum-based — NOT
  tree-locality/prediction-based (the prefetch-shaped code in `SVOStreaming.h` is confirmed
  unimplemented dead code, no `.cpp`, no callers). This means "prefetch nearby recipes" from the
  original tree-as-bucket framing has no existing prediction mechanism to build on either — if
  pursued, it would be new work mirroring `InstanceWantsBrickResidency`'s reactive shape, not a
  tree-locality lookahead this codebase has already solved elsewhere.

## What this unlocks (per the user's own framing)

Once instances have a declared, cheaply-extractable transform + AABB:
- **Spatial bucket assignment** — feeds a (possibly NEW, purpose-built, not necessarily ESVO-tree-
  based) spatial structure for bucketing, using the declared AABB directly rather than requiring a
  tree-node lookup.
- **LOD-tier dispatch** — the declared AABB/transform, combined with the already-proven
  `raySizeCoef` screen-footprint formula, gives a genuine multi-tier LOD signal (today's ESVO
  `LOD_ENABLED` and the `TraceWorld.glsl:143` bound-sphere early-out are both confirmed BINARY —
  sub-pixel or not; no existing system in this codebase currently answers "at this screen size, use
  tier N" with more than 2 states for anything).
- **Two-pass culling/render split**: cull (walk meta segment only, cheap, every instance every
  frame) → resolve (walk full recipe, only for survivors) — a direct generalization of Increment 2's
  existing bucket-then-specialize shape, but with the "cheap prefix" now potentially expressive
  enough to include real per-instance computation (animated transforms, computed bounds), not just
  flat external data.

## Explicitly NOT yet answered — needs real design work, not assumed

- **The recipe-composer/authoring question**: who/what decides where the `[0,n)`/`[n,m)` boundary
  sits when a recipe is authored or generated? This is new tooling work, not just an emitter change
  — nothing in the existing codegen pipeline currently has this concept.
- **Enforcement mechanism**: how is the "declared L2W+AABB, computed early" contract actually
  verified/enforced for a recipe that opts in — a build-time/registration-time check (mirroring
  `RecipeRegistry::Register`'s existing arity validation), a runtime assert, or something else?
- **What happens to `RecipeEntry`'s existing `boundCenter`/`boundRadius`** (a single, CPU-computed,
  position-independent bound today) once a recipe can declare a richer, position-DEPENDENT AABB via
  its own opcodes — do these coexist (flat bound as a fast-reject pre-check, declared AABB as the
  precise culling input), or does one subsume the other? Not decided.
- **Whether the "new purpose-built spatial structure"** this idea's bucket-assignment goal implies is
  itself in scope, or whether this direction is ONLY about the recipe-side contract/two-pass split,
  leaving spatial-structure design to a separate future increment. Leaning toward the latter (smaller,
  more tractable first step) but not decided.
- **Interaction with [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]]**: that direction
  wants the GLSL/HLSL unroll MECHANISM itself single-sourced (currently ~85-90% hand-duplicated
  between the two emitters). A meta/resolve segment split and property-accessor `out`-params would be
  a real, additional feature of whatever unroll mechanism eventually exists — worth sequencing
  together rather than adding accessor logic to today's two independent, soon-to-be-superseded
  hand-written emitters twice.
- **Cost honesty**: recipes that opt into the contract pay real computation cost for their meta
  segment (however cheap the author makes it) — this is not zero-cost the way today's flat `worldPos`
  is. This is a legitimate, accepted tradeoff (more expressive placement in exchange for
  non-free evaluation) but should stay an explicit, stated cost in any future design doc, not
  something glossed over as strictly better than the flat scheme in all cases.

## Relationship to other in-flight/captured directions

- Builds on/resolves open questions from:
  [[Recipe-Bucketing-LOD-Screen-Footprint-Reuse-Addendum-2026-07]] (the `raySizeCoef` reuse finding),
  [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]] (the property-accessor/multi-output
  mechanism would naturally live in whatever single-sourced unroll mechanism eventually exists),
  [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]] (a two-pass cull/resolve split is a
  related but DISTINCT idea from single-dispatch-no-switch selection — both reduce cost around
  already-unrolled recipe functions, from different angles; worth checking for a unified design once
  either is picked up, not assumed to compose automatically).
- Distinct from [[gpu-precision-tiering-direction]] (half/full-precision recipe params) — both want a
  distance-driven signal, worth checking for overlap if either is scoped, per the LOD addendum's own
  note.
- Depends on (shipped): [[Recipe-Parameterization-Plan-2026-07]]'s `ReadParam`/`ReadParamFloat3` —
  the mechanism that makes "parameterized, not baked to literals" transforms/AABBs possible without
  forcing a recompile per instance, exactly the JIT epic's own economics requirement.

## Suggested first step, if picked up

Do NOT jump to building the meta/resolve segment split or the accessor opcodes. First: resolve the
"is a new spatial structure in scope, or is this direction ONLY the recipe-side contract" scoping
question above, since that determines whether this is a contained codegen feature or a much larger
program touching a new spatial-acceleration structure. If the answer is "recipe-side contract only,"
the honest next step is a small prototype: ONE recipe with a hand-authored meta segment (declared
transform + AABB via `ReadParam`-sourced values), confirm the inline-`out`-param-assignment approach
actually produces correct GLSL by hand before any tooling/composer work is attempted.
