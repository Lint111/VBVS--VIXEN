# Recipe Nested Invocation + Unroll-vs-Natural A/B — Direction (2026-07-18)

> **Status: SCOPING DOC, not yet a plan.** This is Step 2 of the user's user-described 3-step
> sequence (load-tiered recipes → this → world-streaming load/unload). Step 1
> ([[Recipe-Load-Tier-Contract-Direction-2026-07]], M1+M2) is DONE + Opus-APPROVED and merged to
> `main` (`3b3098c6`). Grounding research (2026-07-18, dedicated Explore-style pass) found the
> user's literal framing — "A/B test unrolled vs. natural recipes in a dense, recipes-calling-recipes
> environment" — has a load-bearing prerequisite gap: **recipe-calling-recipe does not exist anywhere
> in this codebase today.** This doc splits Step 2 into two sub-steps accordingly, per explicit user
> direction after being asked: build the minimal nesting mechanism first, then run the A/B test on
> top of it.

## 0. The grounding finding, precisely (do not re-derive — cite, don't re-research)

Confirmed by direct code read (2026-07-18):

- **`SdfOpCodes.g.h`** (`VIXEN/libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h:15-124`): the
  full opcode enum has zero opcodes that reference another recipe by ID. Every opcode is
  primitive-math, CSG-combinator, transform, or VM-control. `ComposeFloat3`/`DecomposeFloat3` pack/
  unpack scalars into a float3 — unrelated to recipe composition despite the name.
- **`SdfRecipeEval.h::evalRecipe`**: takes exactly one flat `const SdfInstruction* prog` + `count`,
  walks it in a single loop with ONE value stack (`stack[64]`), ONE position stack (`posStack[...]`),
  and ONE distScale stack — no recursive re-entry, no recipe-lookup-by-ID inside the walk, no
  opcode case that looks up a different `RecipeEntry` and recurses.
- **`RecipeRegistry.h`**'s `RecipeEntry` stores one flat `std::vector<SdfInstruction> bytecode` plus
  scalar metadata (bounds, gating/precision thresholds, occupancy grid) — no field referencing
  another recipe's ID. The public surface (`Register`/`Get`/`GetMutable`/`Ids`) has no
  wrapper/composition API of any kind.
- **`SdfRecipeCodegenGlsl.h`/`SdfRecipeCodegen.h`**: each emits exactly one self-contained
  `sdfRecipe_<id>(vec3 p, float params[6])` function per recipe from ONE flat bytecode walk. The
  caller-side dispatcher (`evalRecipeField` in `UberShaderSplice.h`) is a flat `switch(recipeId)`
  picking exactly one `sdfRecipe_<id>` per instance — **no emitted function ever calls another
  emitted `sdfRecipe_<id>` function.** No inline-vs-call branch point exists because the single-level
  premise is baked into the emitter's whole design.
- **What "composition" means today**: authoring-time only — a human (or an explicitly-not-yet-built
  "composer tool," per [[Recipe-Diversity-Stress-Scene-Inc6-Plan-2026-07]]'s own repeated framing)
  hand-writes ONE flat bytecode array that combines multiple primitives via CSG opcodes. This is
  composition of SHAPES within a single recipe, not composition of independently-registered RECIPES
  calling each other.

**Consequence**: "A/B test unrolled vs. natural under recipes-calling-recipes pressure" cannot be
scoped as a single step — nesting itself is unbuilt. Per explicit user direction, this doc scopes
**M1 (build minimal nesting)** and **M2 (the A/B test on top of it)** as two milestones of the same
direction, sequential, M2 depending on M1.

## Milestone Map

- **M1 (minimal nesting mechanism): DONE + APPROVED.** Branch `feat/recipe-nested-invocation`, based on
  `main` at `3ae96ec8`. Adds a new VIXEN-only opcode `InvokeRecipe = 113`
  (`SdfOpCodes.g.h`, next free slot after `DeclarePosition=112`, same hand-mirrored-addition
  category — no Yeroket `[SdfCoreOp]` counterpart). Semantics: `data[0]` = callee `recipeId`
  (resolved via `RecipeRegistry` at both interpret- and unroll-time); **position passthrough**
  — the callee samples the caller's current `pos`/`curPos` unmodified, no implicit transform (a
  caller wanting a transformed nested instance wraps `InvokeRecipe` in an explicit
  `Transform`/`RestorePos` pair, the existing precedent, not new machinery); **result
  composition** — pushes exactly 1 value onto the caller's value stack, identically to a leaf
  primitive, so `Union`/`SmoothUnion`/etc. operate on a nested-call result with zero changes
  (`RecipeStackArity(InvokeRecipe) = {0,1,0,0}`).
  - **Cycle/depth guard, registration-time (not runtime-only):** `RecipeRegistry::Register` gained
    3 new `RegisterResult` values — `UnknownCalleeRecipe`, `RecursiveInvocation`,
    `NestingTooDeep` — and a private `ValidateNestingGuard`/`WalkNestingGuard` transitive-graph
    walk. Because recipes must register in dependency order (a callee must already exist in
    `entries_` before a caller referencing it can register), the walk only needs `entries_` plus
    the not-yet-inserted entry — no separate two-pass registration needed. A direct
    self-invocation, an indirect 2-cycle (A→B, B→A), and a chain exceeding
    `kMaxRecipeNestingDepth = 4` are all rejected cleanly at `Register()`, never reaching
    `evalRecipe`/the GLSL emitter.
  - **Interpreter path** (`SdfRecipeEval.h::evalRecipe`): gained an optional
    `const RecipeRegistry* registry = nullptr` parameter (every pre-M1 call site compiles
    unchanged) and an `InvokeRecipe` case that looks up the callee's `RecipeEntry` and
    recursively calls `evalRecipe` on its bytecode with the same `pos`/`params`/`registry`.
  - **Unrolled/GLSL path** (`SdfRecipeCodegenGlsl.h::EmitProceduralFieldFunctionGlsl`): unroll
    strategy chosen was **recursive inlining**, confirmed against the actual emitter structure
    (not just the direction doc's non-binding recommendation) — this emitter does emit-time
    symbolic execution over STRING expressions (`stk`/`curPos` are `std::string`, not real GLSL
    locals needing scope management), so walking a callee's bytecode is structurally IDENTICAL
    to walking the caller's own: more `t<n>`/`pp<n>` lines appended to the same `body`, the
    callee's final expression pushed onto the same `stk`. Implemented as a self-recursive
    lambda (`auto walk = [&](auto&& self, ...){ ... }`) so `InvokeRecipe`'s case can recurse into
    the SAME `body`/`stk`/`n`/`curPos` state with zero duplication — no new codegen concept
    (cross-function calls, a callee parameter-passing convention, a second function signature)
    was needed at all, preserving today's "one totally self-contained `sdfRecipe_<id>` function"
    shape exactly. Gained the same optional `registry` parameter as the interpreter.
  - **Correctness gate, all passing, 0 regressions:**
    - New `test_recipe_nested_invocation.cpp` (7 tests): (1) CPU-interpreter parity — `evalRecipe`
      on `A = SmoothUnion(InvokeRecipe(B), Box)` matches a hand-composed CPU reference across 126
      sample points; (2) GLSL-emit — confirms the emitted source textually inlines B's own
      `SdfCore_Sphere(...)` call (proving recursive inlining actually happened, not a stray
      reference) and compiles through the real glslang-backed `ShaderCompiler`; (3)
      **GPU-verified numerical parity, mandatory** — `RecipeNestedInvocationGpuParityTest` (real
      discrete/integrated GPU, same device-gate/SKIP shape as
      `test_recipe_glsl_numerical_parity.cpp`) dispatches the compiled recursive-inlined GLSL and
      confirms it matches `evalRecipe`'s CPU values within tolerance — **ran on real hardware in
      this session (not skipped), 425ms, PASSED**; (4) 4 cycle/depth-guard tests — self-invocation,
      2-cycle, exceeding `kMaxRecipeNestingDepth`, and an unknown-callee reference, all rejected
      with the correct `RegisterResult`, no crash/hang.
    - `RecipeGlslOpcodeCoverage.CorpusCoversEveryValidOpcode` (existing test) needed one exemption
      line added — `InvokeRecipe` requires a `RecipeRegistry`+registered callee the shared
      corpus-loop harness doesn't thread through, same class of exemption `DeclarePosition`
      already has there, with its own dedicated coverage in `test_recipe_nested_invocation.cpp`
      instead of the shared loop.
    - Zero regressions: every existing recipe test binary re-run after this change
      (`test_recipe_eval_parity` 100/100, `test_recipe_codegen_glsl` 4/4, `test_recipe_registry`
      16/16, `test_recipe_glsl_numerical_parity` 5/5 after the exemption fix, plus
      `test_recipe_bake`/`test_recipe_bake_center`/`test_recipe_baker`/`test_recipe_boot_ingest`/
      `test_recipe_bounds`/`test_recipe_codegen`/`test_recipe_container_parity`/
      `test_recipe_ingest`/`test_recipe_manifest`/`test_recipe_occupancy`/
      `test_recipe_pack_loader`/`test_sdf_recipes`) — all passing, all green.
  - **Deviations from the implementer prompt:** none of substance. The prompt's suggested "2-4
    levels" depth range was resolved to the fixed constant 4; the doc's own non-binding
    recursive-inlining recommendation was independently confirmed (not just followed) against
    the actual emitter's string-splicing structure per the prompt's explicit instruction to do
    so.
  - Opus-validated APPROVED 2026-07-18 — independently re-traced the cycle/depth-guard logic by
    hand (confirmed the dependency-order argument holds via the 2-cycle test's `UnknownCalleeRecipe`
    rejection), independently re-ran the GPU parity test (confirmed genuinely ran on real hardware,
    not skipped) and 3 of the regression suites, confirmed the recursive-inlining choice is
    collision-free (single monotonic temp-name counter shared across nesting levels). One
    non-blocking concern noted: the cycle guard is registration-time only — `RecipeRegistry::GetMutable`
    could theoretically let a caller rewrite bytecode post-registration to introduce a cycle the
    runtime has no depth cap against, but the validator confirmed the only actual `GetMutable` caller
    (`RecipeBaker.h`) never rewrites bytecode, so this is a theoretical gap with no live trigger,
    appropriate for a minimal M1 mechanism. A separate build-system flake (a shared cross-worktree
    Yeroket CodegenTool file-lock race unrelated to this commit's own files) was hit and independently
    confirmed as infra noise, not a code defect, by re-running the affected target in isolation.
- **M2 (the A/B test): NOT STARTED.** Depends on M1 (done above). Scope per §2 below — out of
  scope for M1's own dispatch.

## 1. M1 — minimal recipe-calling-recipe mechanism

### 1.1 Scope: the smallest mechanism that makes M2 possible, not a general composition system

Per this program's own established discipline (hand-author/prove the mechanism before any tooling —
the same discipline the Load-Tier Contract's M1/M2 both followed), M1 should NOT build:
- A general recipe-composer/authoring tool (explicitly out of scope, mirrors Inc6's own repeated
  exclusion of this).
- Multi-level unbounded nesting depth as a design goal — a fixed, small nesting depth (e.g. 1-2
  levels) is sufficient to prove the mechanism and give M2 something to A/B test against.
- Cross-language (GLSL+HLSL) support in the FIRST cut — prove the mechanism in one target language
  (GLSL, since it's this codebase's primary live path per
  [[vixen-app-compute-only-raster-disabled]]) before considering HLSL.

### 1.2 The concrete opcode-level gap to close

A new VIXEN-only opcode is needed, following the exact precedent
`DeclarePosition` (`SdfOpCodes.g.h:108-123`) already set for a VIXEN-local, non-canonical-Yeroket
addition: not a real Yeroket `[SdfCoreOp]` (this is a VIXEN-runtime concept — bytecode-level recipe
invocation — with no C# kernel-callable counterpart, same category as `DeclarePosition`), value
chosen as the next free slot (113, after `DeclarePosition=112`), same hand-mirrored-addition comment
convention.

Call it `InvokeRecipe` (naming TBD at implementation time) — semantics to design at implementation,
but must resolve, at minimum:
- **How the callee recipe ID is encoded in the bytecode** (an immediate operand referencing a
  `recipeId`, resolved against `RecipeRegistry` at both interpret-time and unroll-time).
- **How arguments/position are passed through** — does the callee see the caller's current sample
  position (`p`) unmodified, or does the caller push a transformed position first (mirroring how
  `Transform`/`Twist` already rebind `curPos` before continuing the walk)? The existing
  `Transform`/`RestorePos` push/pop convention (`SdfRecipeCodegenGlsl.h:430-484`) is the closest
  existing precedent to crib from, not invent independently.
- **How the callee's result composes back into the caller's value stack** — presumably the callee's
  distance-field result gets pushed onto the caller's value stack exactly as a leaf primitive
  (`Sphere`, etc.) would, so existing CSG combinators (`Union`, `SmoothUnion`, ...) work unmodified on
  a nested-call result without needing their own changes.
- **Recursion/cycle guard** — a recipe invoking itself (directly or via a cycle through other
  recipes) must be rejected, presumably at `RecipeRegistry::Register` time (mirroring the existing
  arity-validation discipline `Register()` already applies) or via a fixed max-depth check at
  interpret/unroll time. Do not ship without SOME guard — an unguarded cycle is an infinite loop or
  infinite-recursion crash, not a graceful failure.

### 1.3 Both execution paths need the mechanism — interpreter first, unrolled second

Per the Unroll-Mechanism-Single-Sourcing doc's own framing (orchestration vs. formalized output are
mutually exclusive execution strategies for the same recipe, not layered), M1 needs to add nesting
support to BOTH:
1. **`SdfRecipeEval.h::evalRecipe`** (orchestration/interpreter path) — the new opcode's handler
   looks up the callee's `RecipeEntry` via `RecipeRegistry` and recursively invokes `evalRecipe` on
   its bytecode, threading position/value stacks per §1.2's design. This is the "natural" execution
   path M2's A/B test needs as one side of its comparison.
2. **`SdfRecipeCodegenGlsl.h`** (unrolled/formalized-output path) — per the grounding research's
   Q3 finding, no existing branch point decides inline-vs-call; this needs new codegen work either
   way. Two options to evaluate at implementation time (do not assume which is right without
   checking against the actual emitter structure):
   - **Recursive inlining**: the caller's unrolled function directly inlines the callee's own
     unrolled bytecode walk (recursively unroll the callee first, splice its straight-line code in).
     Simpler to reason about (still produces ONE self-contained function per top-level recipe,
     matching today's shape) but means the unrolled-vs-natural comparison at nesting depth N is
     really "one big flat unrolled function vs. N recursive interpreter calls" — arguably the more
     honest comparison for M2's actual question (does unrolling still win once the SOURCE recipe is
     structurally nested, even if the compiled OUTPUT ends up flat).
   - **Call-to-already-unrolled-function**: the caller's unrolled function calls the callee's own
     separately-emitted `sdfRecipe_<calleeId>()` function directly (GLSL function calls are legal;
     this needs no inlining). Closer to what "unrolled" conventionally means in general compilers,
     but is a bigger departure from today's "each recipe compiles to one totally self-contained
     function" shape and may reintroduce some of the register-pressure/icache cost that made the
     switch-cost-knee pre-check's flat-N collapse happen in the first place (worth checking whether
     this defeats the point of unrolling at all — a real risk to flag, not assume away).

   **Recommendation, not a decision**: recursive inlining is the more conservative first cut (smaller
   blast radius, doesn't require solving cross-function-call codegen), but confirm against the actual
   emitter code before committing — this is exactly the kind of "read the source before assuming"
   discipline this program has repeatedly needed.

### 1.4 Correctness gate for M1

- **GPU-verified test, mandatory** (same discipline every Load-Tier Contract milestone used): a
  hand-authored recipe A that invokes recipe B (a simple leaf primitive, e.g. `Sphere`), confirm BOTH
  execution paths (interpreted via `evalRecipe`, and unrolled via the GLSL emitter) produce
  numerically matching distance-field results at a range of sample points — this is the
  `RecipeParityCorpus.h`-style CPU/GPU parity discipline this codebase already has precedent for
  (Increment 6 and earlier used exactly this pattern; find and follow it, don't invent a new
  parity-test shape).
- **Cycle/recursion guard test**: confirm a self-referencing or cyclic recipe registration is
  rejected with a clear `RegisterResult` (mirroring `BadGateFootprintThreshold`/
  `BadPrecisionFootprintThreshold`'s existing pattern), not a crash or hang.
- **No regression** to any existing recipe/bytecode path that doesn't use the new opcode — the exact
  "n=0 case, non-participating recipes unaffected" discipline every Load-Tier Contract milestone
  already established.

## 2. M2 — the actual A/B test (depends on M1)

Once M1 exists, M2 answers the user's original question: **does unrolling remain a performance win
under dense, nested/composed recipe pressure, or does the win only hold at the flat top-level-N
scales already measured (Inc6, the switch-cost-knee pre-check)?**

### 2.1 What to measure, and against what baseline

- **Scale target, per explicit user direction (2026-07-18): push well past Inc6's own N=250 ceiling.**
  Inc6's flat-N sweep topped out at N=250 DISTINCT recipes with a 192-INSTANCE ceiling (the
  register-all/instantiate-capped design — see [[Recipe-Diversity-Stress-Scene-Inc6-Plan-2026-07]]
  M2). The user wants M2's own sweep to target **1000+ total instances, drawn from a distinct-recipe
  count in the 250-1000 range** — i.e. BOTH more distinct recipes AND enough instances-per-recipe
  that the total instance count clears 1000, deliberately exceeding every prior measurement's scale
  (Inc6's 192-instance cap, the switch-cost pre-check's own N≤100 cases) to stress this scene
  authentically at the scale a "grass/terrain/city/biome" scene implies. This is a genuinely new scale
  regime for this codebase's recipe-perf measurements — confirm at scoping/implementation time whether
  the existing 192-instance ceiling itself needs to be raised/reconsidered to reach 1000+ instances,
  or whether that cap was specific to Inc6's own scene design and doesn't apply here.
- **Per-recipe bytecode complexity (`m_i` in the switch-cost-knee pre-check's own decoupling
  terminology), per explicit user direction (2026-07-18): 20-200 opcode steps per recipe.** This is
  itself a wide range spanning simple (20-step) to genuinely complex (200-step) hand/generated
  recipes — the sweep should sample across this range (not just pick one fixed `m_i`), since the
  switch-cost pre-check already showed `m_i` (code size) is a primary driver of the existing flat-N
  collapse; M2 needs `m_i` as an explicit, independently-varied axis, not a fixed constant, to
  determine whether nesting depth's effect is independent of or entangled with code-size effects
  already known to matter.
- Construct a nesting-depth axis (e.g. depth 1, 2, 4, 8 — chosen at implementation time, informed by
  what M1's guard/design actually supports) crossed against the distinct-recipe-count axis (250-1000),
  the total-instance-count axis (targeting 1000+), AND the per-recipe-complexity axis (20-200 steps)
  — a genuinely 4D sweep (depth × recipe-count × instance-count × m_i), not just depth alone, since
  the open question is specifically whether NESTING changes the flat-N story at a scale and
  complexity this codebase hasn't tested before, not nesting in isolation at the old, smaller scale.
  A full 4D grid is almost certainly too expensive to sweep exhaustively — at implementation time,
  design a reduced set of representative cells (e.g. corners + a few interior points of the space)
  rather than a naive full cross-product, and document what was dropped/sampled vs. exhaustively
  covered (per this program's own "no silent caps" discipline).
- Compare: (a) fully-unrolled nested recipes (per whichever of §1.3's two unroll strategies M1
  actually built) vs. (b) the SAME nested recipes evaluated via the interpreter/orchestration path
  (`evalRecipe`, recursing through `InvokeRecipe`) — this is literally "unrolled vs. natural" as the
  user phrased it, now meaningful because nesting exists.
- Reuse the switch-cost-knee pre-check's `m_i`/`k_i` decoupling discipline (code-size vs.
  instance-re-evaluation-count, kept independently variable) — nesting depth is plausibly a THIRD
  axis interacting with both, not simply folded into `m_i`. Confirm this rather than assuming nesting
  depth is just "bigger `m_i`."

### 2.2 Rigor bar (established precedent — match, don't improvise)

Per the grounding research's findings on this codebase's own precedent:
- **Switch-cost-knee pre-check** shape: `test_switch_cost_isolation.cpp`-style harness, Windows-
  native Debug (`vixen-ninja`), validation layers ON, hard discrete-GPU-selection `ASSERT`, 30
  steady-state iterations per case + 1 excluded warm-up, pipeline-compile timed separately and
  excluded, fixed-seed (`std::mt19937`) randomized-but-arity-valid bytecode trees re-validated through
  the REAL `RecipeRegistry::Register` (never assumed valid), axis-decoupling cases that pin two of
  three variables and vary the third to attribute causation not just correlation.
- **Inc6 M4** shape: 3 independent runs per data point, `VIXEN_EXIT_AFTER_FRAMES=900`, steady-state
  window = frames 150-900, `PerfCsvWriter`/`VIXEN_PERF_CSV`, mean-of-run-means WITH the actual
  cross-run range reported (not hidden), documented+excluded confounds (e.g. the window-minimize
  `cpu_frame_time_ms < 0.5ms` exclusion rule) rather than silently averaging them in, full regression
  suite re-run and diffed against baseline every time.

M2 should match this bar: multiple runs per (depth, N) cell, real discrete GPU + validation layers,
explicit confound-handling discipline, and — critically, since this is a genuinely new axis — an
axis-decoupling design (pin depth, vary N; pin N, vary depth) so a result can be attributed to
nesting depth specifically rather than "more total instructions" in general (which the switch-cost
pre-check already showed is the real driver of the flat-N collapse — M2 needs to confirm whether
nesting depth is just MORE of that same driver, or something qualitatively different).

## 3. Explicitly NOT yet answered

- Whether recursive-inlining or call-to-already-unrolled-function (§1.3) is the right unroll
  strategy — needs a read of the actual emitter code at implementation time, not decided here.
- Whether nesting depth interacts multiplicatively, additively, or independently with the existing
  flat-N collapse driver (code-size/register-pressure) — the entire point of M2, not assumed.
- Whether a real production use case (grass/terrain/city/biome composition, per the user's own
  framing) actually needs MORE than 1-2 levels of nesting, or whether shallow nesting is sufficient
  for realistic content — worth checking against actual authored-content plans before over-scoping
  M1's depth support.
- Cross-language (HLSL) nesting support — explicitly deferred past M1's first cut (§1.1).
- **Whether Inc6's 192-instance registration/instantiation ceiling needs to be raised to reach M2's
  1000+-instance scale target (§2.1), or was specific to Inc6's own scene design.** Not yet checked
  against the actual cap's implementation (find where it's enforced — `RecipeRegistry` registration
  limit vs. a scene-authoring-time instantiation cap vs. a GPU-buffer-sizing limit — before assuming
  either "just raise a constant" or "this needs real redesign").

## Related

- [[Recipe-Load-Tier-Contract-Direction-2026-07]] — Step 1, DONE + merged (`3b3098c6`). This doc is
  Step 2 of the same 3-step user sequence.
- [[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]] — orthogonal (GLSL/HLSL emitter
  mechanism de-duplication, not nesting), but its Research Finding 2 (the value/position/distScale
  triple-stack shape) is directly relevant grounding for how M1's `InvokeRecipe` opcode must thread
  state across a recipe-call boundary.
- [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]] — the switch-cost-knee pre-check
  this doc's M2 rigor bar and axis-decoupling discipline are drawn from. Premise killed for its own
  original question, but its measurement discipline is exactly what M2 should reuse.
- [[Recipe-Diversity-Stress-Scene-Inc6-Plan-2026-07]] — the flat-N FPS-collapse baseline M2's sweep
  extends with a nesting-depth axis; also the source of the perf-test statistical-rigor precedent
  (3 runs/point, documented confound exclusion) and the repeated "composer tool is out of scope"
  precedent this doc's M1 also excludes.

## Provenance

- User, 2026-07-18 (original 3-step framing): "...then we need the proper unrolling functionality to
  do an AB testing of vaccine [sic — recipes], composed of an enrolled [sic — unrolled] recipes and
  and natural recipes and do a comparison between them..." — Step 2 of the 3-step sequence, following
  Step 1 (load-tiered recipes, now DONE).
- Grounding research, 2026-07-18 (dedicated research pass): confirmed recipe-calling-recipe does not
  exist in any form — no opcode, no interpreter support, no unrolled-emitter precedent. Full findings
  cited in §0 above.
- User, 2026-07-18, when asked how to proceed given the gap: "Build minimal nesting first, then A/B"
  — chose to scope M1 (nesting mechanism) and M2 (the A/B test) as two milestones of this same
  direction, sequential, rather than reframing away from literal nesting or pausing Step 2 entirely.
- User, 2026-07-18 (M2 scale target, given while M1 was being scoped): "we should aim to 1000+
  instances from recipes that are N 250 to 1000, i want to really push the scope of the performance"
  followed by "with recipes themselves having 20-200 steps range" — sets M2's sweep target well past
  every prior measurement's scale in this codebase (Inc6's own N=250/192-instance ceiling, the
  switch-cost pre-check's N≤100 cases): 250-1000 distinct recipes, 1000+ total instances, 20-200
  opcode steps (`m_i`) per recipe. Captured in §2.1 and flagged as an open question in §3 (whether
  Inc6's 192-instance ceiling needs raising to reach this target).
