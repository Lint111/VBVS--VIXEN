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
> **Foundation dependency (user decision 2026-07-10): recipe parameterization is the KEYSTONE,
> planned as the increment AFTER Inc1** — this JIT epic sits on top of it. Parameterization spans
> TWO layers (user correction 2026-07-10): the **existing node-level** `ParameterDefinition` /
> `SetParameter` capability (carries per-instance values to the node) + the **deferred recipe-VM
> `ReadParam` opcode** (P4 dynamic params — lets the recipe program consume them) + SPIR-V
> spec-constants for burned-in values. Both layers, wired together. See §5.

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

**Planned as the increment after Inc1 (user decision 2026-07-10).** This is the foundation the whole
JIT rests on. **Correction (user, 2026-07-10): parameterization spans TWO distinct existing/planned
mechanisms — do not conflate them:**

1. **Node-level parameters — VIXEN node-graph capability, EXISTS TODAY.** `NodeType` /
   `ParameterDefinition` with `SetParameter` / `GetParameter` (used across the instancing demos,
   ~34 callers). This is the plumbing that carries a per-instance value from the app/graph **down to
   the render node** and into an instance param buffer / uniform. It is NOT a shader primitive and
   NOT the recipe VM.
2. **Recipe-VM `ReadParam` opcode — a BYTECODE instruction, deferred to kernel-codegen "P4 dynamic
   params".** `ReadParam` (opcode 96) / `ReadParamFloat3` (111) are recipe-VM opcodes (see
   [[Recipe-Container-Format-Contract-2026-06]] P4, and the SDF-Recipe-Kernel-Codegen P2.4 specs
   where they are explicitly DEFERRED). They let the **recipe program itself** read a dynamic
   parameter *instead of a baked-in constant*, evaluated by `evalRecipe` (CPU) and the field emitters
   (GPU). Today recipes are closed constant programs (`paramMask==0` enforced).

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
2. **Async tier-1 promotion on usage** — hot-mark → background emit+compile → swap when ready →
   universal fallback. Requires the async-compile-and-swap machinery (frame never blocks).
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
