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
  - [x] **DONE (2026-07-17) — prototype SUCCEEDED cleanly, M2 proceeds on contract-based placement.**
    Resolved the scoping question: recipe-side contract only, no new spatial structure (per this
    increment's own decision, §0). Hand-authored ONE recipe:
    `[ReadParamFloat3(idx=0), DeclarePosition, Sphere(center=0,r=0.5)]` — a new marker opcode
    `DeclarePosition` (VIXEN-only, hand-mirrored into `SdfOpCodes.g.h` alongside the existing
    `ReadParam`/`ReadParamFloat3` hand-mirrors; pops a float3 off the value stack, captures it as the
    declared position, translates the sample point for the rest of the walk). The direction doc's own
    inline-assignment argument HELD exactly as described: `EmitProceduralFieldFunctionGlsl` gained an
    opt-in `emitDeclaredPositionOutParam` flag that assigns `declaredPos` the moment `DeclarePosition` is
    walked, then keeps emitting the resolve segment into the same linear function body — no early-exit
    machinery needed. `evalRecipe` got a mirrored `outDeclaredPos` out-param on the CPU side. Both are
    opt-in (default-off/nullptr), so every pre-Inc6 call site is unaffected — confirmed by a full rebuild
    + the entire pre-existing SVO test suite (test_recipe_codegen, test_recipe_codegen_glsl,
    test_recipe_eval_parity [100 tests], test_recipe_registry, test_recipe_bake) passing unchanged.
    **Correctness gate**: `RecipeGlslNumericalParityTest.DeclaredPositionMatchesAcrossCpuAndGpu`
    (new, `test_recipe_glsl_numerical_parity.cpp`) ran on REAL discrete/integrated GPU hardware (not
    skipped) and passed: declared position matches between CPU/GPU and the `ReadParam`-supplied value at
    4 swept positions; the resolve segment's field value at a fixed query point correctly tracks the
    declared position (outside when declared elsewhere, exactly `-radius` when declared==query point);
    compiled the SPIR-V module exactly once and re-dispatched across all 4 params values (no-recompile
    invariant confirmed, mirroring P4's own proven claim). **Live-render gate**: a new standalone
    2D-distance-field-slice GPU test (`test_recipe_declared_position_render.cpp` — a full 3D ray-traced
    render wasn't available for the GLSL field-function-only emitter, so a direct flat-slice
    visualization was used instead, an equally valid and cheaper visual proof) rendered the same
    recipe at 3 declared positions on real hardware and wrote 3 PNGs; each showed the sphere's disc
    silhouette at the pixel location matching its declared world position (visually confirmed + a
    centroid-position numeric assertion in the test itself). Commit: `754442d1`. New opcode registered
    in `RecipeStackArity`/`IsValidSdfOpCode`; excluded (with an explicit, documented exemption) from the
    shared `RecipeParityCorpus`/`RecipeGlslOpcodeCoverage` loop since it needs the out-param emitter path
    the shared harness doesn't thread through — its own dedicated tests cover it instead.
  - **Opus re-validator: APPROVED (2026-07-17).** Independently re-derived every claim, did not trust
    the report on the strength of it reporting a clean success — this is the load-bearing milestone the
    rest of the increment builds on. Ran a fresh full `build.bat all` (confirmed correct worktree source,
    new test binary's mtime postdates its source), re-ran the CPU/GPU parity test on real GPU hardware
    (not skipped) and independently confirmed all 3 of its claims, including reading the actual generated
    GLSL to verify the `declaredPos` assignment is genuinely INLINE mid-function (not hoisted) and that
    the SPIR-V module compiles exactly once across the 4-value redispatch. Re-ran the live-render test
    and — critically — opened and inspected the actual 3 PNGs rather than trusting the centroid-distance
    assertion alone: confirmed `insidePixels=812` identical across all 3 (translation-only, no
    scale/distortion), and all 3 positions visually correct with no axis swap or sign flip (one apparent
    vertical-placement oddity was investigated and correctly identified as a display y-flip convention,
    not a bug — internally consistent with the test's own u/v mapping). Confirmed the opcode-coverage
    exemption is real, documented, and doesn't weaken the coverage check for any OTHER opcode. Confirmed
    scope discipline (diff limited to exactly the 13 allowed files, no `BuildRenderGraph.cpp`/
    `VulkanGraphApplication.cpp`/`RecipeEntry`/spatial-structure/composer-tool/enforcement-mechanism
    changes). Re-ran the full regression suite independently (own pass counts matched exactly:
    codegen 10, codegen_glsl 4, eval_parity 100, registry 16, bake 3, glsl_numerical_parity 5, zero
    failures/skips). **Own independent conclusion: the prototype genuinely succeeded — M2 can proceed on
    contract-based placement.** One minor non-blocking note for M2: the render test's PNG output path
    resolves to Windows `C:\tmp\...` (not WSL `/tmp`) under the Windows-native exe — a path-convention
    note for any M2 tooling that reads rendered output, not a defect.
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
  - [x] **DONE (2026-07-18) — renders correctly at N=20 and N=250, mechanism generalized from
    M1's single hand-authored recipe.** New gated demo `VIXEN_RECIPE_DIVERSITY_STRESS_DEMO`
    (env-var N, default 20) added to `BuildRenderGraph.cpp` — NOT an in-place extension of
    `VIXEN_PROCEDURAL_UBER_DEMO`, so that demo's own N=100/500 measurement baseline stays
    byte-identical for future reference (diff is purely additive, +286/-0). Every generated
    recipe is prefixed with M1's meta segment (`[ReadParamFloat3(idx=0), DeclarePosition,
    <shape/CSG/modifier resolve segment>]`), shapes authored body-local (not baked-literal
    world positions), placed on a genuine 2D grid (spacing=30, centered) instead of the
    uber-demo's stacked +Z line; each instance's declared position supplied via
    `recipeParams[0..2]`. **192-instance ceiling decision**: register all N recipe programs
    unconditionally, instantiate exactly `min(N,192)` strictly 1:1 (no rotation, no silent
    truncation) — chosen because every recipe is genuinely unique, so cloning instances of the
    same recipe would add count without adding diversity. Live-confirmed at N=250: "registered
    250/250 distinct recipe programs, seeded 192 body instances on a 16x16 grid."
    **Real production bug found+fixed in the same commit**: `UberShaderSplice.h`'s production
    splice path unconditionally called the GLSL emitter with `emitDeclaredPositionOutParam=false`
    — meaning M1's mechanism, despite being proven in isolation, was never actually reachable in
    production and would have failed to compile any `DeclarePosition`-using recipe. Fixed via
    per-recipe usage detection that threads the out-param + a local only for recipes that need
    it; every other recipe's emitted call shape is unchanged (one precise caveat found in
    validation: the spliced source as a whole gains one unconditional unused-local declaration
    even for a zero-`DeclarePosition` registry — harmless, compiles fine, but means "byte-
    identical" was slightly overstated in the original report). Commit: `10b37445`.
  - **Implementer reported DONE_WITH_CONCERNS**: 2 of ~5 live-run attempts at N=100/250 silently
    crashed (exit 1, no logged exception/VUID) during development, attributed to transient
    concurrent-machine load rather than a bug in the diff, based on a clean isolated
    `test_uber_shader_splice` repro (new N=100 case, production `ShaderBundleBuilder` path,
    passing reliably) and process-monitoring evidence of heavy concurrent load at failure time.
    Also saw a `KI-033`-signature VUID-`09600` cascade once, attributed to that already-open,
    already-documented issue.
  - **Opus re-validator: APPROVED (2026-07-18) — concern independently confirmed non-blocking.**
    Did not accept the load attribution on its face — reproduced the crash directly (1 failure
    in 6 N=250 runs; 4/4 at N=20, 5/5 at N=100, 3/3 on an untouched uber-demo control all clean),
    then root-caused it via log forensics: the failure boundary sits at the shared, unconditional
    Cornell-background voxel bake / `GaiaVoxelWorld` ECS-init transition — well after this
    milestone's 250-recipe shader splice had already compiled successfully — and grep-confirmed
    zero added non-comment lines in the M2 diff touch VoxelGrid/GaiaVoxelWorld/DDGI/descriptor/
    swapchain code. This matches the documented-unsound path in **KI-027** (GaiaVoxelWorld
    concurrent voxel creation heap corruption), a pre-existing issue this milestone doesn't
    touch. Separately independently verified the VUID-09600 cascade reproduces byte-identically
    on the untouched uber-demo control, confirming it's pre-existing and not newly introduced.
    Confirmed the production-wiring bug fix is real and correct by reading the emitter's own
    `assert(emitDeclaredPositionOutParam && ...)` plus unconditional `declaredPos` write that
    prior code would have hit for any `DeclarePosition` recipe. Confirmed the 192-ceiling
    `min(N,192)` 1:1 logic, both PNG live-captures (genuinely distinct shapes at genuinely
    distinct grid positions, no stacking, no per-frame recompile storm), full regression suite
    (own counts matched exactly: codegen 10, codegen_glsl 4, eval_parity 100, registry 16,
    bake 3, glsl_numerical_parity 5, declared_position_render 1, uber_shader_splice 7 up from 6,
    zero failures/skips), scope discipline (no M3 per-frame loop, no switch-cost fix, no
    composer/enforcement tooling, `RecipeEntry` untouched), and the camera-preset framing
    limitation (`kOrbitDistanceMax=120` vs. an actual ~450-unit/side N=250 grid span — the code
    comment's own "~156-234" estimate undersells this, but the gate only requires confirming
    SOME bodies at distinct positions, which is met). Tree integrity: `2ff25767..10b37445` one
    coherent commit. **Own independent conclusion: M3 should proceed** — recommends (non-
    blocking, for later) filing/escalating the Cornell/GaiaVoxelWorld boot-bake flakiness
    separately, since M3's per-frame live gates will exercise this same shared path repeatedly.
    - **Diversity generation**: reuses `VIXEN_PROCEDURAL_UBER_DEMO`'s own
      `{sphere/box/torus} x {6 extra CSG ops} x {none/Round/Onion}` product generator verbatim
      (same legacy-3 byte-for-byte preservation for i<3), confirmed to genuinely produce N=250
      distinct opcode programs. Every generated program is prefixed with M1's proven meta
      segment `[ReadParamFloat3(idx=0), DeclarePosition]` instead of baking each shape's world
      center as a literal — shape instructions are now authored in body-local space (origin),
      relying on `DeclarePosition`'s eval-time `curPos -= declaredPos` translation for world
      placement. This is the actual generalization from M1 (1 hand-authored recipe) to N
      programmatically-generated ones.
    - **Spatial distribution**: replaced the uber-demo's stacked-Z-line layout with a genuine
      2D grid in the XZ plane (`ceil(sqrt(N))` columns), spacing=30 world units (> 2×12 bound
      radius, no neighbor bound-sphere overlap), centered on world (64,64,64) matching every
      other demo's convention. Each instance's declared position is supplied via
      `BodyInstanceGpu::recipeParams[0..2]` (the `ReadParamFloat3`-sourced parameter M1's
      mechanism expects), authored ONCE at scene setup (a static-per-run layout, per this
      milestone's own scope — M3 will later mutate this same field per-frame via the identical
      `SetInstances()` path).
    - **192-instance ceiling decision (documented, per the milestone's own requirement)**:
      register ALL `N` distinct recipe programs unconditionally (registration has no hard cap),
      but instantiate exactly `min(N, 192)` body instances — a strict 1:1 registered-recipe :
      instantiated-instance mapping up to the ceiling, then flat-capped. At N=20:
      registered=20, instantiated=20 (under the ceiling, no capping needed). At N=250:
      registered=250, instantiated=192 (confirmed live: "registered 250/250 distinct recipe
      programs, seeded 192 body instances on a 16x16 grid"). Chosen over a rotating-subset
      scheme (option (a) in the milestone prompt) because every recipe here is genuinely
      unique — cloning multiple instances of the SAME recipe adds instance count, not diversity
      value, so 1:1 is both the simplest correct shape and avoids inventing subset-rotation
      machinery M4 would have had to redo anyway.
    - **Production wiring gap found and fixed**: `UberShaderSplice.h`'s
      `SpliceProceduralRecipesIntoSource` (the real production emitter, used by
      `BodyInstanceRayMarch.comp`'s splice — NOT the same path as M1's standalone test shader)
      called `EmitProceduralFieldFunctionGlsl` with `emitDeclaredPositionOutParam` always
      `false`. Any `DeclarePosition`-using recipe would have failed that emitter's own assert
      (Debug) or emitted GLSL referencing an undeclared `declaredPos` identifier (Release) the
      moment it reached production splicing — M1's mechanism was proven correct in isolation but
      never actually wired into the real render path. Fixed centrally (not per-demo): the splice
      now detects per-recipe whether a program contains `DeclarePosition` and, only for those
      recipes, emits with the out-param flag true and supplies a throwaway local
      (`unusedDeclaredPos`) at the `evalRecipeField` call site — every other recipe (every
      pre-Inc6 demo, and any Inc6 recipe that doesn't use `DeclarePosition`) keeps the original
      3-arg call shape byte-identical. Verified via a new isolated repro test
      (`UberShaderSplice.OneHundredDeclarePositionRecipesCompile`, 100 `DeclarePosition`-using
      recipes through the exact production `ShaderBundleBuilder` path) — passes standalone in
      ~4.2s, proving the GLSL itself is correct independent of the full app.
    - **Camera preset**: new `VIXEN_RECIPE_DIVERSITY_STRESS_DEMO` orbit preset (center
      (64,64,64), distance=118 near `CameraNode::kOrbitDistanceMax`=120's hard clamp, pitch≈0.9
      rad looking down at the grid). Documented, accepted limitation: at N's upper end the full
      grid (up to ~156-234 world units per side) cannot fit in one frame within the 120-unit
      orbit-distance ceiling at the shared 45° FOV — the live-run gate only requires
      confirming SOME bodies at genuinely distinct positions/shapes, not literally all N framed
      simultaneously (no single screenshot could show 250 legible distinct shapes regardless of
      framing).
    - **Live-run results**: both N=20 and N=250 (interpreted as 192 instantiated, per the
      ceiling decision above) rendered successfully on real Windows-native discrete GPU hardware,
      validation layers on, `VIXEN_HUD_CAPTURE_FRAMES` screen captures inspected directly.
      N=20 capture: 9 visibly distinct shapes on screen (spheres, boxes, a torus, rounded
      variants) at clearly distinct grid positions, none stacked/overlapping. N=250 capture
      (192 instantiated): similarly distinct shapes/positions across a denser field, "registered
      250/250... seeded 192... on a 16x16 grid" confirmed in the log. No crash, no hang, no
      per-frame recompile storm observed at either N (one-time boot recompile only, matching
      every other demo's own startup behavior).
    - **Known flakiness investigated and ruled non-blocking**: 2 of ~4 live-run attempts at
      N=100/250 crashed with no exception/error logged (silent process termination) during
      shader compile or Cornell-background-scene baking; retries succeeded cleanly. Isolated via
      a standalone repro test (see above) that the GLSL/splice logic itself is correct
      independent of the full app, and confirmed via `Get-Process`/`Get-CimInstance` that the
      machine was under heavy concurrent multi-agent CPU load during the failing attempts (not
      memory-exhausted) — consistent with transient resource contention, not a deterministic bug
      in this milestone's code. One clean run at each N is on record with full logs + PNG
      captures. Separately, a real but ALREADY-KNOWN, ALREADY-OPEN pre-existing issue,
      `KI-033` (`VIXEN_PROCEDURAL_UBER_DEMO` boot-recompile leaves a shared descriptor set
      stale, producing a VUID cascade — root-caused to `body_octree_scene`'s one-time boot
      recompile racing swapchain-settle, unrelated to recipe content), reproduced on ONE of the
      N=100 attempts (VUID-vkCmdDraw-None-09600 on 4 DDGI/probe images, not on any recipe/param
      binding) — confirmed via git diff that this milestone's changes touch neither
      `RecompileDirtyNodes` nor the DDGI/probe image lifecycle, so this is the same pre-existing
      dormant bug, timing-exposed by a slower shader compile at higher N, not a new regression.
    - **Regression suite**: all baseline counts unchanged — codegen 10, codegen_glsl 4,
      eval_parity 100, registry 16, bake 3, glsl_numerical_parity 5, declared_position_render 1;
      `test_uber_shader_splice` grew from 6 to 7 (new N=100 repro test added, all passing).
    - Files touched: `BuildRenderGraph.cpp` (new demo block + camera preset, additive only, no
      existing demo modified), `UberShaderSplice.h` (per-recipe `DeclarePosition` detection —
      a correctness fix required for ANY `DeclarePosition`-using recipe to reach production, not
      new machinery scoped to this demo), `test_uber_shader_splice.cpp` (new regression test).
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
