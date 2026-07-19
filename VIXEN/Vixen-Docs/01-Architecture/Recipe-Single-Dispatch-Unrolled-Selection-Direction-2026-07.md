# Recipe Single-Dispatch Unrolled Selection — Direction (2026-07-16)

> **Status: PREMISE NOT SUPPORTED (2026-07-18) — the "avoid the runtime switch" framing is killed
> by this doc's own suggested pre-check; a reframed sub-idea survives. See "Pre-check outcome"
> below before considering any further scoping.** Originally captured as a follow-on idea surfaced
> while closing out [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] (M4's honest negative perf
> finding).

## Pre-check outcome (2026-07-18) — premise killed as framed, one sub-idea survives

The "Suggested first step" below was run as a small, isolated gating spike
(`spike/switch-cost-knee-precheck`, no production code touched, reused the pre-existing
`test_switch_cost_isolation.cpp` harness from [[Recipe-Bucketing-Overhead-Inc3-Plan-2026-07]]'s
own M0). Opus-validated APPROVED after independent re-derivation (5 fresh runs vs. the
implementer's 3, direct test-code inspection of the axis-pinning claims, an independently-rebuilt
SPIR-V probe).

**Finding: switch-dispatch cost is RULED OUT as the N=100 knee's driver.** The real driver is
per-case code size (`m_i`, register pressure/instruction-cache from having every case's unrolled
body resident in one compiled shader) combined with per-instance re-evaluation count (`k_i`), NOT
switch-case count (N) itself:
- A 100-case switch with trivial per-case bodies and low instance count costs ~1.07x vs. control
  (essentially free).
- A 10-case switch with large per-case bodies and high instance count costs ~2.33x vs. control —
  MORE expensive than the 100-case trivial switch, inverting what a pure case-count theory would
  predict.
- Independent SPIR-V inspection (both the implementer's and the validator's own separately-built
  probe) confirms glslang already lowers the switch to a single genuine multi-way `OpSwitch` with
  zero `OpBranchConditional` cascade — the switch mechanism itself is already cheap/well-lowered.

**Important provenance correction, found by the validator**: this spike RE-CONFIRMS an existing
finding from [[Recipe-Bucketing-Overhead-Inc3-Plan-2026-07]]'s own M0 (2026-07-16,
[[Perf-Ledger]]), which already concluded switch/branch-dispatch cost is ruled out and
code-size-per-case + instance-count is the driver, with matching decoupling numbers within noise.
This spike is not the first answer to the question — its genuinely new contributions are (a)
applying that existing finding specifically to THIS direction's premise, and (b) the SPIR-V
jump-table confirmation, which Inc3 M0 did not do.

**Consequence for this direction's two candidate mechanisms**: neither addresses the actual
driver. Specialization constants would relocate the same resident code into more pipelines without
reducing per-case code size or per-instance re-evaluation (and risks re-becoming the per-pipeline
dispatch-overhead cost class Inc2 M4 already found slower). Jump-table/indirect dispatch is a
non-fix by construction — the switch is already a jump table with no branch cascade to improve on.

**One sub-idea survives, per the validator's own sharper read — do not kill this along with the
premise**: the direction's stated goal of keeping same-recipe threads contiguous/subgroup-local
within a single dispatch could still help the REAL driver via a different mechanism than "avoiding
the switch" — subgroup-coherent recipe locality means only one recipe's code path is hot per
subgroup at a time, which can relieve icache/register pressure even though the switch itself
stays. This is an instance-locality/divergence argument, not a dispatch-avoidance argument, and
would need its own honest re-scoping (starting from "does subgroup-coherent recipe locality reduce
register pressure/icache thrash" as the question, not "does avoiding the switch help") rather than
inheriting this doc's original framing.

**Recommendation**: do not scope this direction's original framing into an increment. If picked up
again, re-scope from the surviving subgroup-locality angle as a fresh, narrower question with its
own pre-check, not as a continuation of "avoid the switch."

## The problem this responds to

Two dispatch-selection strategies for routing many distinct SDF recipes through GPU compute now
have real, measured costs, and both lose in different ways:

1. **`UberShaderSplice.h`'s tier-0 switch** (`evalRecipeField(uint recipeId, vec3 p, float
   params[6])`, one dispatch, per-thread `switch(recipeId)` picks which unrolled function to call).
   Measured (2026-07-10, [[Perf-Ledger]] "Switch-scaling measurement"): flat through N≈10, an
   **~8× FPS collapse by N=100**, a **hard driver-level pipeline-compile hang at N=500**. Cost:
   branch divergence / register pressure / instruction-cache thrash across a much larger switch as
   N grows, all inside ONE compiled shader.

2. **`SpecializedRecipeShaderGlsl.h`'s per-recipe specialized dispatch** (Increment 2, M1-M4,
   [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] — bucket instances by exact `recipeId`, one
   specialized zero-switch shader + one `vkCmdDispatchIndirect` per hot bucket). Measured (M4,
   2026-07-16, real discrete GPU): **consistently SLOWER than the tier-0 baseline at every tested N**
   — 0.31× at N=3, 0.25× at N=10, 0.05× at N=100 (gap widens with N). Cost: N separate
   dispatch/descriptor-bind/`MultiDispatchNode`-auto-barrier calls, fixed overhead per bucket that
   scales linearly with N and dominates the per-thread switch cost it was meant to avoid.

Both costs are real and independently confirmed on real hardware — this isn't a coin-flip between
two roughly-equal options, it's two different failure modes: **(1) grows with N inside one
dispatch, (2) grows with N because of MANY dispatches.**

## The idea

A third shape that hasn't been built or measured: **keep everything in ONE dispatch (avoiding
(2)'s per-bucket dispatch multiplication) while avoiding a runtime per-opcode OR per-recipe-ID
switch inside that dispatch (avoiding (1)'s divergence/switch cost).**

Concretely: sort/bucket instances by `recipeId` into contiguous thread index ranges within a
SINGLE dispatch's thread grid (not per-bucket dispatches — one dispatch whose threads are just
laid out so threads assigned to the same recipe are contiguous or at least subgroup-local), then
have each thread call ITS bucket's specific pre-unrolled `sdfRecipe_<id>(vec3 p, float params[6])`
function — the exact same unrolled functions `EmitProceduralFieldFunctionGlsl` already produces
today, zero new codegen work there — without going through `UberShaderSplice.h`'s N-case runtime
`switch`.

Two candidate mechanisms for that per-thread "which unrolled function" selection, neither built or
measured yet:

- **Specialization constants**: compile N tiny shader *variants* at pipeline-creation time, one per
  workgroup-shape/recipe assignment, selected via `VkSpecializationInfo` rather than a runtime
  branch. Open question: does this just become "N pipelines" again (re-introducing cost class (2)'s
  per-pipeline overhead, just moved from dispatch-time to bind-time), or can enough recipes share
  one specialized pipeline via workgroup-level assignment to stay net-cheaper? Untested.
- **Indirect/jump-table dispatch**: some form of function-pointer-style indirection (GLSL/SPIR-V
  doesn't have real function pointers, so this would likely mean either (a) a bindless-style
  indexed buffer of specialized sub-shaders invoked via a subgroup-uniform branch, or (b) accepting
  SOME branch but making it a single indexed jump rather than an N-case linear/binary-search switch
  — GPU compilers may already lower a `switch` to a jump table for large N, worth checking whether
  `UberShaderSplice.h`'s existing switch already gets this treatment and the N=100 knee is NOT a
  dispatch-instruction cost at all but something else (register pressure from having every
  recipe's code resident in one compiled shader, instruction cache thrash, etc.) — **if so, this
  entire idea's premise needs re-examining before scoping further.**

## What's already built (no new work needed for these parts)

- **The unroll itself.** `EmitProceduralFieldFunctionGlsl`
  (`libraries/SVO/include/Recipe/SdfRecipeCodegenGlsl.h:30-905`) already compiles any recipe's
  opcode bytecode into ONE straight-line GLSL function, `sdfRecipe_<id>(vec3 p, float params[6]) ->
  float`, with zero runtime opcode branching — confirmed via a 2026-07-16 research pass (see
  Progress note below). This is the SAME function `UberShaderSplice.h`'s switch calls into and
  `SpecializedRecipeShaderGlsl.h`'s specialized dispatch calls into today — both existing strategies
  already consume this primitive, they just differ in the SELECTION layer above it. A genuine HLSL
  sibling (`SdfRecipeCodegen.h::EmitProceduralComputeShader`) exists too, same technique, but is
  test-only/not production-wired — not relevant to this idea unless the GLSL path is abandoned.
- **Per-recipe thread bucketing/compaction.** `shaders/RecipeInstanceBucketing.comp` (Increment 2
  M1) already buckets `bodyInstances[]` by exact `recipeId` via atomic-counter compaction into
  per-bucket compacted instance-index SSBOs. This is exactly the "contiguous thread ranges by
  recipe" data this idea needs — it just currently feeds N separate indirect dispatches (cost class
  (2)) rather than one dispatch with per-range thread assignment. Re-purposing this data for a
  single-dispatch layout is plausible without redoing the bucketing pass itself.

## What's NOT built / genuinely open

- The single-dispatch per-thread selection mechanism itself (specialization constants vs. jump
  table vs. something else) — zero prototyping done.
- Whether GPU compilers already lower `UberShaderSplice.h`'s switch to a jump table, which would
  mean the N=100 knee's ROOT CAUSE isn't "switch dispatch cost" at all — this needs to be checked
  BEFORE scoping further, since it determines whether this whole direction addresses the actual
  bottleneck or a mistaken theory of it. (SPIR-V/driver disassembly or a targeted micro-benchmark
  isolating dispatch-instruction cost from register-pressure/icache cost would answer this.)
- Whether keeping per-thread selection subgroup-COHERENT (so a subgroup's threads mostly agree on
  which recipe, avoiding intra-subgroup divergence) requires the bucketing pass to also do some
  screen-space-locality-aware reordering, not just a flat compacted list — unclear if this matters
  or if GPU schedulers already handle arbitrary thread-to-recipe assignment fine within a single
  dispatch's occupancy.

## Relationship to other in-flight work

- Supersedes/reframes, not replaces: [[Recipe-GPU-Instance-Bucketing-Inc2-Plan-2026-07]] (M1-M4,
  DONE — this idea reuses M1's bucketing data, doesn't redo it) and
  [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] (the parent epic — this would likely
  become a candidate Increment 3, replacing the original "async tier-1 promotion" framing's
  per-recipe-dispatch assumption with a single-dispatch one, IF the switch-cost premise above holds
  up).
- Independent of: [[Recipe-Declared-Gaia-Query-Direction-2026-07]] (dispatch-pattern-by-recipe-type
  batching idea, a different axis — that one's about avoiding per-recipe Gaia queries, this one's
  about avoiding per-recipe dispatch/switch cost; they could compose but aren't the same idea).

## Suggested first step, if picked up

Before writing any implementation plan: a small, targeted measurement to test the "is the N=100
knee actually switch-dispatch-cost, or something else (register pressure / icache)" open question
above. This is cheap to check (a synthetic shader with a large switch but where each case is
trivial/identical, vs. today's real recipes where each case's unrolled body differs) and would
either validate or kill this whole direction's premise before any design/implementation investment.
