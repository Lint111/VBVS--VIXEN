# Runtime Tiered Recipe Pipeline (JIT) — Direction

> **Status: HIGH PRIORITY (user, 2026-07-10) — elevated from "future/data-gated" after the M5
> switch-scaling data came in.** The target workload is **heavy procedural generation + lazy asset
> loading**, where the number of live recipe programs is exactly the axis that grows — and the
> measurement (§8) proved the universal switch collapses at N≈100 and hard-ceilings at N=500. So
> **"dynamically roll up shader dispatches according to asset usage"** (usage-triggered promotion of
> hot recipes to specialized pipelines, §2–§3) is the mechanism that target workload *requires*, not
> a someday-nicety. This is now a near-term epic, sequenced after Inc1 alongside the parameterization
> keystone (§5). Spun off from a user idea 2026-07-10 during
> [[Lazy-Procedural-Delta-Baseline-Inc0-Inc1-Plan-2026-07]] M5. **Gated on M5's measured
> switch-vs-rolled-out numbers** (N=3 vs N≈10 recipes) — this epic is the answer *iff* the single
> `switch(recipeId)` uber-shader degrades as recipe count rises. If M5 shows the switch scales fine,
> this stays parked. Do not build ahead of that data.
>
> **Foundation dependency (user decision 2026-07-10): recipe parameterization is the KEYSTONE —
> ✅ SHIPPED 2026-07-15** ([[Recipe-Parameterization-Plan-2026-07]], commits `353e6b8e..0ae8ea48`
> M1-M3 + M4 doc-closure, all four milestones DONE, Opus-validated APPROVED). This JIT epic can now
> build on top of it. Parameterization spans TWO layers (user correction 2026-07-10): the
> **existing node-level** `ParameterDefinition`/`SetParameter` capability (carries per-instance
> values to the node, unchanged by that plan) + the **recipe-VM `ReadParam`/`ReadParamFloat3`
> opcodes** (P4 dynamic params — now shipped, lets the recipe program consume them via
> `BodyInstanceGpu::recipeParams[6]`, no shader recompile on a pure value change). SPIR-V
> spec-constants for burned-in per-shape values were explicitly deferred out of that plan (v1 uses
> the instance-buffer path only) — still open for this JIT epic if/when it needs them. See §5.
>
> **Reprioritization (user, 2026-07-15): N=100 is a NEAR-TERM wall, not a someday-at-scale
> concern.** The §8 decision gate's framing ("when the engine needs to render on the order of
> 100+ distinct recipe programs concurrently") undersells how soon that threshold is actually
> crossed: any scene with real visual variety — a few dozen prop/rock/building/decal archetypes,
> each with a handful of structural variants — reaches N=100 distinct structural recipes almost
> immediately, well before the scene reads as "detailed" by any normal standard. The switch's knee
> at N=100 and hard hang at N=500 is therefore a **capability blocker for anything beyond the
> current uber-shader tech-demo scale**, not a distant scaling optimization to defer. Sequencing
> stays as scoped (Increment 1 → 2 → 3 → 4, §7), but treat the whole epic as urgent: move through
> increments back-to-back rather than pausing between them once Increment 1 is underway.
> [[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]] is Increment 1's implementation plan.

## 1. The idea (user, 2026-07-10, verbatim intent)

Dynamic shader loading at runtime from a dynamic registry that keeps *relevant* recipe shaders
pre-warmed and discards unused ones. Concretely: the **first** render call for a recipe uses the
**universal pipeline** (the `switch` uber-shader) for swift reaction — no compile stall. Because it
was just used, the recipe is marked hot, its program is **unrolled and baked into a runtime pipeline
fragment** (a specialized pipeline) in the background, which becomes available and **the renderer
swaps that recipe off the universal path as soon as it can**. Group **fragment families** — cache/
share pipelines across *similar* recipes. Future requirement: **assigned inputs to recipes
(parameterable recipes)** for content integration → enables **instance rendering per recipe type**.

## 2. What this actually is: a tiered JIT for recipes

This is the classic **interpret-hot-then-compile** model (JVM/V8 tiered compilation), applied to
SDF recipes:

- **Tier 0 — universal pipeline (interpret).** The `switch(recipeId)` uber-shader M5 ships. Every
  registered recipe renders immediately through it; zero per-recipe compile latency. The safety net.
- **Tier 1 — specialized pipeline (compile-on-hot).** On first *use*, a recipe is marked hot; its
  unrolled GLSL (`EmitProceduralFieldFunctionGlsl`, M4) is compiled into a dedicated `VkPipeline`
  **asynchronously**. The universal pipeline keeps serving until the specialized one is ready →
  **non-blocking promotion** (this is the key property; a frame never stalls on a compile).
- **Swap.** When the specialized pipeline exists, route that recipe's instances to it; the switch
  case becomes fallback.
- **Evict (discard unused).** LRU-evict cold specialized pipelines — destroy the `VkPipeline`, KEEP
  the recipe bytecode so it can re-promote on next use. This is a **GPU-LRU** — and it is EXACTLY
  the decision [[sparse-mip-esvo-lod-inc1-m1|Sparse-Mip Inc2 M4]] deferred behind a flip-trigger, so
  this epic is where that re-open lands, **with M5's evidence behind it** (not inherited assumption).

**Why the universal pipeline is load-bearing, not throwaway:** it's what makes tier-1 async. Without
a tier-0 that can render *anything* instantly, promotion would have to block the first frame. Keep it.

## 3. Trigger = usage, not registration (the improvement over the current plan)

Inc1's descoped "async recompile-and-swap" triggers on *registration*. The user's insight is a
better trigger: **usage**. A registered-but-never-rendered recipe costs nothing (tier 0 covers it);
only recipes that actually draw get a specialized pipeline. This bounds pipeline count by
*working set*, not registry size — the same "bounded by residency policy, not world size" property
the tiered-ESVO/lazy program leans on everywhere, applied to pipelines.

## 4. Existing code this builds on (CodeGraph-surfaced, verify at build time)

- **`PipelineCacher` / `TypedCacher` / `CacherBase`** (RenderGraph) — an existing pipeline-cache
  layer. A recipe→pipeline cache keyed by a **recipe-content hash** is the natural home for tier-1
  pipelines. This also gives **exact-duplicate fragment-family sharing for free**: two recipes with
  identical bytecode hash to the same key → one shared pipeline, no new mechanism.
- **`ParameterDefinition` on `NodeType`** (`SetParameter`/`GetParameter`) — the **existing
  node-level** parameter capability (§5 mechanism 1). This is the plumbing that carries per-instance
  values down to the render node; the recipe-VM `ReadParam` opcode (§5 mechanism 2) is the separate
  piece that consumes them. Both are needed for parameterable recipes.
- **M4 emitter** (`EmitProceduralFieldFunctionGlsl`) — already emits the unrolled per-recipe GLSL;
  the "bake into a pipeline fragment" step is emit → glslang → `VkPipeline`, proven compilable (M4's
  `RecipeGlslCompiles` gate).
- **M5's seam** — the user-directed "keep the recipe→pipeline binding swap-in-able" requirement in
  M5 is precisely the hook this epic swaps a per-recipe pipeline into.

## 5. Keystone dependency: recipe parameterization — TWO layers, wired together

**✅ SHIPPED 2026-07-15** ([[Recipe-Parameterization-Plan-2026-07]], all 4 milestones DONE,
Opus-validated APPROVED, commits `353e6b8e..0ae8ea48`). This was the foundation the whole JIT
rests on — it is now in place, and this epic can be un-parked once the §8 decision-gate data is
re-checked against current conditions. **Correction (user, 2026-07-10): parameterization spans TWO
distinct mechanisms — do not conflate them:**

1. **Node-level parameters — VIXEN node-graph capability, EXISTS TODAY (unchanged by the
   parameterization plan).** `NodeType` / `ParameterDefinition` with `SetParameter` / `GetParameter`
   (used across the instancing demos, ~34 callers). This is the plumbing that carries a per-instance
   value from the app/graph **down to the render node** and into an instance param buffer / uniform.
   It is NOT a shader primitive and NOT the recipe VM.
2. **Recipe-VM `ReadParam` opcode — a BYTECODE instruction, SHIPPED 2026-07-15.** `ReadParam`
   (opcode 96) / `ReadParamFloat3` (111) are recipe-VM opcodes (see
   [[Recipe-Container-Format-Contract-2026-06]] §6, P4 shipped) that let the **recipe program
   itself** read a dynamic parameter *instead of a baked-in constant*, evaluated by `evalRecipe`
   (CPU) and the field emitters (GPU), sourced from `BodyInstanceGpu::recipeParams[6]` (binding 10,
   already-existing per-instance upload path — no new GPU binding/buffer). Confirmed live: a
   `recipeParams[]` value change does NOT trigger a shader recompile (M3 Task 9, 50-frame
   instrumented gate) and the rendered geometry visibly tracks the value frame-to-frame (M3 Task 8).
   Recipes with `paramMask==0` on every instruction remain closed constant programs exactly as
   before — P4 only relaxes the gate for these two opcodes specifically.

**"Parameterable recipes" requires BOTH, wired:** node capability (1) supplies the value → it lands
in a per-instance param buffer → the recipe VM's `ReadParam` opcode (2) consumes it during
evaluation. Neither alone suffices — (1) without (2) means the recipe can't read the value; (2)
without (1) means nothing feeds the buffer.

This resolves two of the user's sub-ideas:

- A **parameterized** recipe = ONE pipeline serving ALL instances of that recipe type, with
  per-instance params in an instance buffer (fed via node params, consumed via `ReadParam`). **This
  is "instance rendering per recipe type"** — many bodies, one pipeline, one batched dispatch,
  per-instance data.
- **Parameter changes do NOT recompile — only STRUCTURAL edits do.** This is what makes tier-1
  promotion economical: compile once per recipe *shape*, vary instances by data. Without it, every
  content tweak is a recompile hitch and the JIT thrashes.
- Mechanism split: **SPIR-V specialization constants** for values to burn in at compile (the
  "unrolled and baked" straight-line code) + an **instance param buffer** (node-param-fed,
  `ReadParam`-consumed) for per-body values that must vary without recompile.
- **"Fragment families" falls OUT of this, it is not a separate feature.** Sharing a pipeline across
  *similar* recipes = normalizing the bytecode so the differing literals become *parameters* — i.e.
  detect "same shape, different constants," hash the SHAPE separately from the LITERALS, and serve
  the family from one parameterized pipeline. So similarity-grouping IS parameterization applied to
  a family. Build parameterization first; families are a downstream normalization pass.

## 6. Rejected / steer-away

- **SPIR-V multi-entry-point modules — NOT the tool.** A `VkPipeline` binds exactly ONE entry point,
  so multi-entry buys module *storage* savings, not multi-recipe-per-dispatch and not cheaper
  switching. The mechanism that actually delivers "one dispatch, many recipe kinds" is
  **indirect/batched dispatch grouped by pipeline** (sort instances by recipe → one dispatch per
  distinct pipeline) + specialization constants. Reach for those, not multi-entry.

## 7. Sketch increment cut (when un-parked, after the parameterization foundation)

1. **Recipe-content-hash pipeline cache** over the existing `PipelineCacher` (exact-dup family
   sharing free); recipes still all render via tier-0 switch. Pure infra, measurable.
   **✅ SHIPPED 2026-07-15** ([[Recipe-Pipeline-Cache-Inc1-Plan-2026-07]], merged main `bf8dfbf5`).
2. **Async tier-1 promotion on usage** — hot-mark → background emit+compile → swap when ready →
   universal fallback. Requires the async-compile-and-swap machinery (frame never blocks).
   **⚠️ SCOPING REVEALED A HARD BLOCKER (2026-07-15), design doc in progress, NOT YET a milestone
   plan.** Today's renderer is ONE full-screen dispatch where every pixel-thread marches every
   instance via an in-shader `switch(recipeId)` — there is no per-draw/per-dispatch seam for a
   second pipeline to intercept. Two decompositions were considered: (A) per-recipe full-screen
   re-dispatch + cross-pass compositing — REJECTED, at the real target scale (1000+ live recipes)
   this is N full-framebuffer passes per frame, inverting the whole point of tier-1 promotion; (B)
   **GPU instance bucketing** (partition instances by recipe pre-pass, indirect-dispatch each
   bucket sized to its actual screen coverage) — SELECTED as the only approach that scales toward
   1000+. See [[Recipe-GPU-Instance-Bucketing-Design-2026-07]] for the full design. **All 4 design
   questions RESOLVED 2026-07-15**: view-proj matrix gap closed (mirror `CameraNode`'s
   already-computed `projection*view`, expose as a new `CURRENT_VIEW_PROJ` output alongside the
   existing `PREV_VIEW_PROJ`); cross-bucket depth/hit compositing resolved WITHOUT new atomics
   (sequential `MultiDispatchNode`-style dispatches already get correct write-after-write barrier
   serialization by default, so a plain read-compare-write against `HitRecord` is correct);
   bucketing granularity resolved to exact-`recipeId` (not content-hash family — avoids coupling to
   Increment 4's not-yet-designed normalization work); hotness-gating shape recommended (bucket
   uniformly, gate pipeline assignment by hotness after bucketing). Only empirical validation (a
   measurement spike) remains before a milestone-mapped implementation plan can be written. The
   async-compile-and-swap half of this increment's original sketch still layers ON TOP of working
   bucketed dispatch, not before it.
3. **GPU-LRU eviction** of cold specialized pipelines (the Sparse-Mip M4 re-open, with M5 data).
4. **Shape/literal normalization → parameterized family pipelines** (depends on §5 shipped): group
   similar recipes onto one parameterized pipeline; batched dispatch-by-pipeline.

## 8. Decision gate — ✅ MEASURED 2026-07-10, EPIC IS DATA-JUSTIFIED FOR HIGH N

**The switch-vs-rolled-out measurement is DONE** (real GPU, AMD Radeon / D3D12-dzn, N distinct
structural recipes; full table in [[Perf-Ledger]] "Switch-scaling measurement"):

| N | steady FPS | outcome |
|---|---|---|
| 3 | ~420 | clean |
| 10 | ~396 | clean (flat) |
| 100 | ~51 | **~8× FPS collapse — runtime knee** |
| 500 | hang | **driver `vkCreateComputePipelines` hangs ~14 min, ~28 GB RAM, no dump — hard ceiling** |

**Verdict: the universal switch degrades hard by N=100 and is UNUSABLE at N=500.** So:
- **Tier-0 switch remains correct + sufficient for small N (≤~10–30).** Keep it; it's the instant
  safety net.
- **The rolled-out / per-recipe-pipeline path (this epic) is DATA-JUSTIFIED for order-100+ live
  recipes** — no longer "parked pending evidence." The knee at 100 and the driver hang at 500 are
  exactly the failure the tiered JIT (specialized pipelines, dispatch-by-pipeline, LRU) exists to
  avoid. When the engine needs to render on the order of 100+ distinct recipe programs concurrently,
  build this.
- **Corollary the data adds:** the ceiling is the *driver's* pipeline compiler choking on one giant
  switch, not glslang (glslang finished N=500 in ~22s). So the win of per-recipe pipelines is partly
  that each is a SMALL shader the driver can actually compile — the tiering also fixes a
  compilability wall, not just runtime perf.

See [[Lazy-Procedural-Delta-Baseline-Design-2026-07]] (§8.2 parameterization, §9 per-recipe-pipeline
rejected-for-v1), [[lazy-procedural-delta-baseline-program]], [[kernel-codegen-framework-direction]],
[[runtime-kernel-pipeline-direction]].
