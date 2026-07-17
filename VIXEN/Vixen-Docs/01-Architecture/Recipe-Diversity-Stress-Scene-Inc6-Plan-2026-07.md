# Recipe Diversity Stress-Test Scene — Increment 6 Plan (2026-07-17)

> **Status: SCOPED, not yet started.** New increment, not a continuation of
> [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]]'s own numbered increment sketch (that epic's
> next candidate item, single-dispatch-unrolled-selection, explicitly needs its own cheap pre-check
> before scoping — separate, not started here). This increment builds a large, realistic stress-test
> scene: many DISTINCT diverging recipes, spatially distributed, with parameters updated in real time
> every frame — and uses it to characterize the tier-0 switch's FPS-collapse curve across N=20-250 in a
> live, diverse, dynamic scene, not the synthetic/stacked scenes the existing measurements used.

## §0 Scope

**The goal, as the user framed it**: a large stress-test scene with unique, actually-diverging recipes
showing in different areas of the scene, updating their parameters in real time, for a proper dynamic
scene — as opposed to the small, narrow synthetic scenes used so far (`VIXEN_RECIPE_HOT_COLD_DEMO`'s 6
opcode-identical sphere clones; `VIXEN_PROCEDURAL_UBER_DEMO`'s N diverging recipes but all stacked along
one camera-facing Z-axis line, only 1 of N bodies parameterized).

**Recipe-count range, per user direction**: **N=20 to N=250 distinct recipe complexities**, deliberately
spanning below, at, and above the already-measured N=100 tier-0-switch knee (~2-8x FPS collapse) and
approaching the documented N=500 driver-hang territory (staying below it). This is intentional: the
existing N=100/500 measurements were taken on synthetic, non-diverse, non-spatial, non-dynamic scenes
(`test_switch_cost_isolation.cpp`'s self-contained synthetic tracer; `VIXEN_PROCEDURAL_UBER_DEMO`'s
stacked-Z-line layout) — this increment produces the first measurement of the same collapse curve in a
scene that actually looks and behaves like a real, varied, moving scene. That is new information, not a
re-run of a known result.

**Placement mechanism, decided after user review (2026-07-17): use the real spatial contract, not a
flat-literal workaround.** A separate, unstarted future direction
([[Recipe-Spatial-Contract-Two-Pass-Culling-Direction-2026-07]]) already designs the "right" way for a
recipe to declare a computed, parameterized world position (a meta-segment/resolve-segment bytecode
split, `ReadParam`-sourced L2W transform + AABB, exposed via `out`-params mirroring the already-shipped
`getRecipeBoundSphere` convention) — but that doc explicitly says NOT to jump to building the full
mechanism; its own "suggested first step" is to (a) resolve one open scoping question (is a new spatial
structure in scope, or is this only the recipe-side contract?) and (b) hand-author ONE recipe with a
manual meta/resolve split to prove the approach produces correct GLSL, before any tooling/composer work.
**This increment's M1 IS that suggested first step** — not a parallel workaround that would need
reconciling with the contract later, and not the full contract either. If M1's prototype works, this
increment's stress scene builds its spatial distribution on real contract-based placement for all N
instances. If the prototype reveals the approach doesn't work cleanly, M1 documents why and this
increment falls back to flat-literal placement (the original, simpler plan) for the remaining
milestones — a cheap, early decision point, not a mid-increment scramble.

**Why this needs a real infrastructure gap filled, not just "turn a demo flag up"** (beyond the
placement mechanism question above): grounding research also found no existing demo updates MANY
distinct recipes' parameters simultaneously, every frame. The shipped `ReadParam`/`ReadParamFloat3`
mechanism (Recipe Parameterization, "P4") is proven correct and live-gated, but only exercised on
exactly ONE hand-coded demo body today (`VIXEN_PROCEDURAL_UBER_DEMO`'s single swept-radius sphere).
Generalizing "mutate parameters on N instances every `PreTick()`, re-submit via `SetInstances()`" from 1
body to N is straightforward in principle (same shipped code path) but is new orchestration code, not
something that exists.

**A hard, orthogonal ceiling to respect, not work around**: `TraceWorld.glsl`'s tier-0 march hard-clamps
`numInstances = clamp(pc.instanceCount, 0, 3*64)` — **192 total body instances**, across ALL recipes
combined, regardless of recipe diversity. This bounds how many instances-per-recipe this stress scene
can use at the high end of the N range (e.g. at N=250 recipes, even 1 instance per recipe already
exceeds 192 — the scene design must account for this explicitly, likely via a many-recipes/few-
instances-each shape rather than assuming uniform per-recipe instance counts).

## §1 Grounding — what's already built vs. genuinely new (2026-07-17 research pass)

- **Recipe registration itself has no hard cap** (`RecipeRegistry`, a `std::map`, unbounded except by
  memory/id space) — the real constraint is the tier-0 switch's driver-compile behavior, empirically
  measured, not enforced by any assertion.
- **`VIXEN_PROCEDURAL_UBER_DEMO`'s recipe-generation loop is the right template to generalize** for
  "many distinct, genuinely diverging programs" — it already cycles a `{sphere/box/torus} x {6 CSG ops}
  x {none/Round/Onion}` product past the first 3 legacy-shape recipes, producing opcode-distinct programs
  at arbitrary N (clamped to 2000, though 500 is a documented driver hang). Its placement (stacked +Z
  line) and its single-parameterized-body limitation are what need to change, not its recipe-diversity
  generation.
- **`VIXEN_RECIPE_HOT_COLD_DEMO`'s scene-construction shape is the WRONG template for recipe diversity**
  (its 6 instances are all the same opcode-identical Sphere recipe, differing only by tint/position) —
  its population-mix env-var pattern and PreTick-orchestration precedent are still reusable ideas, just
  not its recipe-generation content.
- **Real-time parameter updates are proven correct and live-gated** (Recipe Parameterization "P4," all 4
  milestones shipped, Opus-validated) via `BodyInstanceGpu::recipeParams[6]` mutated CPU-side and
  re-submitted through `SetInstances()` — confirmed NOT to trigger a recompile as long as instance count
  is unchanged. This is the exact mechanism to generalize from 1 body to N, and the same mechanism the
  spatial contract's own design explicitly builds on (`ReadParam`-sourced transform values).
- **The spatial contract is a real, only-partially-derisked design, not a drop-in mechanism.** Confirmed
  by reading its direction doc directly (not secondhand): the `out`-param multi-output CONVENTION is
  proven in this codebase (`getRecipeBoundSphere`/`getRecipeOccupancyGrid`), and inline assignment
  during a single walk (no early-exit machinery needed, since GLSL locals stay in scope for the rest of
  an emitted function) is argued to make position-DEPENDENT multi-output "structurally small" — but this
  is an argument, not yet a proven prototype. Several real open questions remain explicitly unresolved
  in the doc itself: the recipe-composer/authoring question (who decides the meta/resolve boundary),
  the enforcement mechanism, whether `RecipeEntry`'s existing flat `boundCenter`/`boundRadius` coexists
  with or is subsumed by a declared AABB, and whether a new spatial structure is in scope at all. This
  increment's M1 resolves the LAST of these (declare recipe-side-only, no new spatial structure) and
  produces the hand-authored prototype the doc's own "suggested first step" calls for — it does not
  resolve the composer/enforcement questions, which stay genuinely open for whoever builds the full
  contract later.
- **The bucketed-dispatch alternative is not a way to exceed the switch's practical ceiling.** Three
  independent measurements (Inc2 M4, Inc3 M3, Inc4 M4) all agree specialized per-recipe dispatch is
  slower than the tier-0 switch at every tested N. This stress scene uses tier-0 (the only production
  path that's actually competitive) throughout its N range — it does not attempt to route around the
  wall via bucketing.
- **The switch knee's shape is m_i/k_i-driven (per-recipe complexity x instance count), not literally
  case-count-shaped** (Inc3 M0 finding) — this is directly relevant to how this increment should vary
  "recipe complexity" across its N=20-250 sweep: a flat sweep of N alone, with uniform low-complexity
  recipes, may not reproduce the collapse the same way a mix of complexities would. The plan's own name
  ("N=20-250 recipe complexities," per user direction) should be read as varying BOTH count and
  per-recipe complexity, not holding complexity fixed while only N varies.

## Milestone Map

- **M1 — Spatial-contract scoping decision + hand-authored prototype (the contract doc's own suggested
  first step, not the full mechanism).** Two parts, in order:
  1. **Resolve the scoping question** the contract doc leaves open: is a new spatial/bucketing
     structure in scope for this prototype, or is this ONLY the recipe-side meta/resolve contract? For
     this increment's purposes, the answer is the latter (no new spatial structure — this increment
     needs placement, not bucketing) — document this decision explicitly rather than silently assuming
     it, since the contract doc itself treats it as a real open fork.
  2. **Hand-author ONE recipe** with a genuine meta/resolve bytecode split: a meta segment that computes
     a `ReadParam`-sourced local-to-world position (NOT a baked literal) and exposes it via an `out`-param
     GLSL convention mirroring `getRecipeBoundSphere`, followed by the existing resolve-segment SDF
     evaluation. Confirm this actually produces correct, compilable GLSL and renders the recipe at the
     declared position — both via the CPU eval path (`SdfRecipeEval.h`) and the GPU emit path
     (`SdfRecipeCodegenGlsl.h`), matching each other, per this codebase's own established parity-testing
     convention (see `RecipeEvalParity`/`RecipeGlslNumericalParityTest` test families for the pattern to
     follow).
  **Gate**: this is exploratory/prototype work, but still needs a real correctness proof — CPU/GPU
  parity for the declared position + the resolve segment's own field value, at a handful of `ReadParam`
  values, mirroring the existing parity-test convention. **Decision point, not just a gate**: if the
  prototype produces correct results cleanly, M2 onward builds the stress scene's placement on this
  mechanism (generalized to N recipes). If it reveals a real blocker (composer complexity, enforcement
  gaps that actually bite, GLSL codegen issues beyond what the doc anticipated), STOP, document exactly
  what broke down, and fall back to flat-literal placement (M1's original, simpler plan, preserved below
  as the fallback) for the rest of this increment — do not force the contract through if it doesn't
  actually work cleanly at prototype scale.
  - [ ] Not started.
- **M2 — Spatial placement + recipe-diversity generation, scaled to N=20-250.** Generalize M1's proven
  placement mechanism (contract-based if M1 succeeded; flat-literal fallback if M1 found a real blocker)
  across N distinct, genuinely-diverging recipe programs — reuse/extend `VIXEN_PROCEDURAL_UBER_DEMO`'s
  shape/op/modifier product generator for the diversity itself (already proven to produce opcode-distinct
  programs at arbitrary N), computing a real spatial distribution (a grid or scatter across a
  meaningfully large world-space area — not stacked on one camera-facing line as the uber-demo does
  today) instead of that demo's line-stacking. Respect the 192-total-instance ceiling explicitly
  (document and enforce the instances-per-recipe math for the chosen N range, don't silently truncate or
  overflow). **Live-run gate**: confirm the scene actually renders (no crash, no validation errors) at
  both ends of the N range (N≈20 and N≈250) before proceeding.
  - [ ] Not started.
- **M3 — Real-time parameter updates across all N instances.** Generalize the existing single-body
  `ReadParam` sweep pattern (mutate parameters every `PreTick()`, re-submit via `SetInstances()`,
  confirmed no recompile) from 1 body to all N — every recipe instance's parameters change every frame.
  If M1's contract-based placement is in use, the declared position ITSELF should be one of the
  per-frame-updated parameters for at least some instances (animated placement, exercising the
  contract's own stated value proposition over flat placement), not just an unrelated shape parameter.
  **Gate**: confirm the existing no-recompile invariant still holds at full N (re-run/extend the
  existing `ReadParamValueSweepNeverMarksNodeNeedsRecompile`-style test logic, or a new equivalent, at
  scale) — this is the single most important correctness bar, since a silent recompile-per-frame at
  N=250 would be a severe, misleading performance artifact unrelated to the actual switch-cost question
  this scene exists to measure.
  - [ ] Not started.
- **M4 — Sweep + measurement.** Run the scene across the N=20-250 range (a reasonable sampling, not
  necessarily every integer — e.g. 20, 50, 100, 150, 200, 250, informed by where the existing N=100 knee
  and N=500 hang already are), real live `VixenApp`, validation layers on for a correctness pass and off
  (or noted as a fixed tax, per Inc4 M4's own precedent) for the FPS numbers, record honestly whatever
  the curve looks like — record it plainly in [[Perf-Ledger]] whether it matches, differs from, or
  refines the existing synthetic-scene N=100/500 findings. **This is a measurement milestone, hold its
  own numbers to the same statistical scrutiny Inc4 M4 required** (multiple independent runs per N, not
  single-sample points, given this machine's own documented run-to-run GPU clock-state noise).
  - [ ] Not started.

## Risks / decision points

- **M1 is a real fork point, not a formality — respect its own "fall back if it doesn't work cleanly"
  clause.** The spatial contract doc itself is careful to call out real unresolved questions (composer,
  enforcement, `RecipeEntry` bound coexistence) — this increment is not the place to force resolution of
  all of them under stress-test time pressure. If the hand-authored prototype reveals the mechanism is
  harder than the doc's own optimistic "structurally small" framing suggests, fall back to flat-literal
  placement honestly rather than pushing a half-working contract mechanism into the rest of the
  increment.
- **Do not let recipe-diversity generation collapse into "N copies with different literals."** The
  whole point (per the user's explicit framing: "unique actual diverging recipes") is genuine opcode/
  structural diversity, not parameter-only variation — reuse `VIXEN_PROCEDURAL_UBER_DEMO`'s shape/op/
  modifier product generator, which already does this, rather than a simpler but diversity-free
  generator.
- **The 192-instance ceiling interacts with the N=250 end non-trivially.** At N=250 distinct recipes,
  even 1 instance per recipe already exceeds the ceiling (192 < 250) — M2 must explicitly decide and
  document the instances-per-recipe shape across the N range (e.g. fewer instances per recipe as N
  grows, or fewer total distinct recipes actually instantiated at any one frame even if 250 are
  registered) rather than discovering this as a build-time surprise.
- **M3's no-recompile invariant is the correctness bar that actually matters most for this increment.**
  A stress test whose "FPS collapse" is secretly dominated by a per-frame recompile bug (not the switch-
  dispatch cost this scene exists to characterize) would produce a misleading, unusable result — treat
  this test as seriously as any of Inc4's mandatory live-app gates.
- **This increment does not attempt to fix or work around the switch-cost wall** — it measures it, in a
  more realistic scene than existing measurements used. Any fix (single-dispatch-unrolled-selection,
  GPU-LRU eviction, etc.) stays out of scope, per the parent epic's own existing (separate, unstarted)
  direction docs.
- **This increment does NOT build the spatial contract's composer/authoring tooling or its enforcement
  mechanism** — M1 hand-authors exactly one recipe's meta/resolve split to prove the underlying
  mechanism, then M2 generalizes that PROVEN shape programmatically for the stress scene's own N
  generated recipes (code generating the split directly, not a general-purpose recipe-authoring tool).
  Building a real composer/authoring tool for arbitrary user-authored recipes stays out of scope,
  deferred to whoever picks up the full contract direction later.
- **Live-run gates are mandatory for every milestone**, per this program's established discipline
  (Inc4's own history: every milestone that skipped a live gate shipped a real bug that only a live run
  caught). M1's prototype, M2's placement/diversity generation, and M3's per-frame parameter churn all
  touch production codegen/render-graph paths — do not accept a standalone-test-only pass for any of
  them without also confirming live.
