# Recipe Parameterization ("P4") — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager pipeline to
> implement this plan milestone-by-milestone (fresh implementer + Opus validator per milestone,
> worktree-isolated, progress persisted in this doc; pre-bless the in-tree destructive/git tier at
> setup per the established worktree convention). **Live-run gates are authoritative** for M3 and
> M4 — static review has repeatedly passed runtime bugs on this project; every GPU-touching
> milestone ends in an actual `VIXEN.exe` run with validation layers explicitly enabled. Build
> Windows-native via the repo `.bat` entry points through `cmd.exe /c` (WSL env vars do not reach
> a Windows `.exe`); `vixen-wsl` (Mesa Dozen ICD) is the WSL fallback / offscreen-test harness, but
> per the standing project policy (Lazy-Procedural Inc0/Inc1 ENVIRONMENT NOTE) **all builds + GPU
> tests run Windows-side** — this WSL install has no GPU-backed Vulkan ICD. Never overlap two
> builds of one target. Watch long builds with a foreground polling loop, not a blind wait.

**Goal:** Ship "P4" — the ability for an SDF recipe instruction to read a **dynamic per-instance
parameter** at eval time instead of a baked constant, via new opcodes `ReadParam` (scalar) and
`ReadParamFloat3` (vec3). This is the keystone
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §5 is gated on: **parameter value changes
must NOT trigger a shader recompile** — only structural bytecode edits do. Without this, every
per-instance value tweak on a virtual (zero-bake) body is a recompile hitch, and the tiered-JIT
epic's "compile once per shape, vary by data" economics don't exist.

**This is a two-repo change.** `ReadParam`/`ReadParamFloat3` are **already fully specified and
implemented on the Yeroket/Unity side** — this is a port of a proven design, not new invention:
- `Yeroket-Fantasy/Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs:143,171` —
  `SDFOpCode.ReadParam = 96` / `ReadParamFloat3 = 111` already reserved in the canonical enum
  (`data0.x = slot index into parameters array (resolved at runtime)`).
- `Yeroket-Fantasy/Packages/com.utility.sdf/Runtime/Burst/SDFImplicitParams.cs` — a shipped
  reference implementation: a `NativeArray<float>` parameter array with 14 well-known "implicit"
  slots (Time/DeltaTime/SinTime/CosTime, main-light dir/color/intensity, camera pos), populated
  once per frame via `SDFImplicitParams.Sync(...)` before evaluation.
- `Yeroket-Fantasy/.../SourceGenerator~/SDFNodeSourceGenerator.cs:1789-1835` — the Burst
  dispatcher's generated eval case (`ctx.Stack[ctx.Sp++] = ctx.Parameters[(int)ctx.Inst.Data0.x];`
  for `ReadParam`; indexed triple-read for `ReadParamFloat3`), for both the scalar and SIMD4
  dispatch loops.

**What does NOT exist anywhere yet, and is this plan's actual scope:**
- VIXEN's generated `SdfOpCodes.g.h` mirror (`libraries/SVO/include/Recipe/generated/`) — stops
  at `PushParam=95`/`RestorePos=97`, has no `ReadParam`/`ReadParamFloat3` entries.
- VIXEN's C++ CPU evaluator case (`evalRecipe`, `SdfRecipeEval.h`) — no `ReadParam` case, and the
  function signature takes no parameter-array argument at all.
- VIXEN's GLSL field-function emitter case (`EmitProceduralFieldFunctionGlsl`,
  `SdfRecipeCodegenGlsl.h`) — same gap; no parameter-array threading into the emitted
  `sdfRecipe_<id>(vec3 p)` signature or `evalRecipeField(uint recipeId, vec3 p)` dispatcher.
- `RecipeRegistry::Register`'s hard rejection of `paramMask != 0` (`RecipeRegistry.h:114`) and the
  format contract's `paramMask` enforcement — must be relaxed for exactly these two opcodes.
- Any bridge from VIXEN's existing per-instance GPU parameter storage into the recipe VM — see
  Reuses below, this is mostly wiring, not new storage.
- A C++/HLSL `EmitCppEmitter`-side case in the Yeroket source generator — confirmed absent
  (`ReadParam` only appears in the Burst `Dispatch`/`Dispatch4` generator, not the C++ emitter
  path). VIXEN's C++/GLSL cases are hand-written mirrors, matching the project's existing
  convention ("Canonical values in C# are the source of truth; VIXEN mirrors" — by hand, same as
  every other opcode's C++/GLSL case today).

**Explicitly NOT this increment** (deferred to the JIT epic or later, per
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §5): recipe-content-hash pipeline
caching; async tier-1 promotion; GPU-LRU eviction; shape/literal normalization / "fragment
families"; SPIR-V specialization constants for baked-per-shape values (a real optimization, but
the instance-buffer path alone is sufficient and simpler for v1 — spec-constants can follow once
the JIT epic needs to burn in per-*family* literals); a general implicit-parameter slot table
mirroring Yeroket's `SDFImplicitParam` (Time/Light/Camera) — VIXEN's v1 parameter array is
**explicit per-instance only** (M1), fed from `SetParameter`-style content authoring, not an
engine-wide implicit-slot sync (that's a clean, separable follow-up once this lands, and the
opcode/stack mechanics are IDENTICAL either way — only the array's *population* differs).

**Architecture:** Reuse, don't invent. `Vixen::SVO::BodyInstanceGpu` (`ShellOctreeGpu.h:349-364`)
**already has a per-instance `float recipeParams[6]`** field, already uploaded every frame to
binding 10 via `BodyOctreeSceneNode::SetInstances` → SSBO ring (no recompile — confirmed the
per-tick instance upload path is entirely separate from `SpliceProceduralRecipesIntoSource`'s
recompile trigger). Today the uber-recipe path (`recipeId>=2`) simply never reads this array. The
whole of M1-M2 is: thread a `float params[]` (backed by `recipeParams[6]`) through the CPU
evaluator and the GLSL emitter/dispatcher signatures, add the two new opcode cases, and relax the
registry's `paramMask` gate for exactly `ReadParam`/`ReadParamFloat3`. No new GPU binding, no new
buffer, no octree/brick/mip-pool format change.

**Tech Stack:** C++23, GLSL compute (runtime-compiled via glslang), C# (Yeroket canonical opcode
source + Burst reference, read/ported not modified unless a genuine drift is found), GoogleTest,
CMake ninja/wsl presets + Windows `.bat` builds, Vulkan 1.3, real GPU (Windows-native) for all
render/parity gates.

**Reuses (verified 2026-07-15; re-verify at implementation time):**
`BodyInstanceGpu::recipeParams[6]` (`libraries/SVO/include/ShellOctreeGpu.h:356`, 3 of 6 floats
already spare per the "3 spare" comment) + its existing binding-10 upload path
(`BodyOctreeSceneNode::SetInstances`, `BodyOctreeSceneNode.cpp:135-151`) — the per-instance param
carrier, already wired end-to-end, just not consumed by the procedural path yet;
`shaders/SceneBindings.glsl:162-174` — the GLSL mirror of `BodyInstanceGpu` (binding 10,
`bodyInstances[]`), where `params.xyz` / the 3 spare floats need to become GLSL-visible in the
recipe-eval call site; `RecipeStack.h`'s `RecipeStackArity` table (`PushParam`/`PushFloat3`
entries at lines ~47/51 as the direct template — `ReadParam`⇒`{0,1,0,0}`,
`ReadParamFloat3`⇒`{0,3,0,0}`, since the validator checks stack DEPTH only, confirmed by reading
`RecipeRegistry.h:111-126`, never VALUES — a runtime-unknown value does not break static
validation); `evalRecipe`'s existing `PushParam` case (`SdfRecipeEval.h:466-469`) and
`EmitProceduralFieldFunctionGlsl`'s existing `PushParam` case (`SdfRecipeCodegenGlsl.h:724-728`)
as the direct 1-line-different templates (`in.data[0]` baked literal → indexed array read);
`UberShaderSplice.h`'s `evalRecipeField(uint recipeId, vec3 p)` dispatcher (line 91) and
`getRecipeBoundSphere` (line 100) — both signatures need a params argument threaded through;
`IsValidSdfOpCode` (`RecipeRegistry.h:14-52`) — where the two new opcodes get added to the valid
set; `RecipeRegistry::Register`'s `paramMask` check (`RecipeRegistry.h:114`) — the one line that
currently hard-rejects what this plan enables (needs a narrow allow-list, not a blanket removal —
see M1 Task 2 for the exact rule: `paramMask != 0` allowed ONLY when `opCode` is
`ReadParam`/`ReadParamFloat3`, still rejected for every other opcode, so a stray non-P4 param-mask
byte in an unrelated instruction remains caught).

**Design of record:**
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §5 (the two-layer requirement: node-level
params — already shipped, out of scope here — supplying values into a per-instance buffer, wired
to a VM opcode that consumes them — this plan);
[[Recipe-Container-Format-Contract-2026-06]] §6 ("P4 planned: `paramMask≠0` opcodes; `ReadParam`
instruction; per-frame override through `SetRecipeParams()`" — this plan is P4);
[[Lazy-Procedural-Delta-Baseline-Design-2026-07]] §8.2 (parameterization explicitly deferred out
of Inc1, this plan closes that gap).
**Depends on (shipped):** Lazy-Procedural-Delta-Baseline Inc0+Inc1 (M1-M6, ALL DONE 2026-07-10/11,
CLOSED per Perf-Ledger) — the uber-shader splice + `recipeId` switch + zero-bake virtual rendering
this plan extends.

---

## §0. Scope

**In scope:**
- Two new opcodes (`ReadParam`, `ReadParamFloat3`) mirrored from the already-shipped Yeroket
  canonical enum into VIXEN's generated header, with C++ CPU-eval and GLSL-emit cases (M1).
- Threading a `const float*`/`params[]` argument through `evalRecipe`, `evalRecipeField`,
  `getRecipeBoundSphere`, `EmitProceduralFieldFunctionGlsl`, and the uber-shader dispatch call
  site, sourced from `BodyInstanceGpu::recipeParams[6]` (M1-M2).
- Registry validation: narrow `paramMask` allow-list for exactly these two opcodes; stack-arity
  entries; `IsValidSdfOpCode` additions (M1).
- A parity/coverage test proving CPU `evalRecipe` and GPU `evalRecipeField` agree on
  `ReadParam`/`ReadParamFloat3` across a range of parameter-array values (not just one fixed
  value) — the numerical-parity harness precedent from Lazy-Procedural M4, extended (M2).
- Live-run proof that changing ONLY `recipeParams[]` values (same registered bytecode) does
  **not** trigger `SpliceProceduralRecipesIntoSource`/shader recompile, and that the rendered
  geometry visibly changes frame-to-frame from param values alone (M3).
- One authored demo/test recipe using `ReadParam` end-to-end (e.g. a parameterized sphere radius
  or box half-extent) exercised through the full CPU-bake vs GPU-virtual parity gate inherited
  from Lazy-Procedural M6, confirming parameterized recipes are geometry-correct on BOTH paths
  (baked recipes may also use `ReadParam` — the CPU evaluator needs it too, for authoring-time
  preview/bake-time snapshot semantics) (M3-M4).
- Doc updates: `Recipe-Container-Format-Contract-2026-06.md` §6 P4 row flips to shipped;
  `Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07.md` intro/§5 status line updated (M4).

**Out of scope:** the JIT epic itself (pipeline cache, tier-1 promotion, GPU-LRU, family
normalization — all of §7 there); an implicit/well-known parameter slot table (Yeroket's
`SDFImplicitParam` Time/Light/Camera automation) — v1 here is explicit-only, populated by content
authoring, not a per-frame engine sync; growing `recipeParams` beyond 6 floats or adding a
dedicated parameter SSBO (only needed if a real recipe needs >6 dynamic values — cross that bridge
if M3's demo needs it, do not build speculative capacity); any change to the Yeroket canonical
C# files beyond confirming they already match what VIXEN mirrors (read-only reference unless a
genuine mismatch is found, in which case flag it, don't silently diverge); SPIR-V specialization
constants; any node-graph/`ParameterDefinition`/`SetParameter` change (that system stays exactly
as-is — it's the *upstream* content-authoring layer that would feed `recipeParams[]` in a full
authoring pipeline, but wiring THAT bridge is separate follow-up work, not this plan; this plan
only makes the VM/shader side capable of consuming a param array that already reaches the GPU).

---

## Milestone Map

- **M1 — Opcode + CPU-eval + registry validation** (Tasks 1-4) · gate: pure-CPU gtest green — new
  opcodes registered, `evalRecipe` correctly reads from a passed-in params array, registry accepts
  `ReadParam`/`ReadParamFloat3` and continues rejecting `paramMask!=0` on every other opcode, stack
  arity/overflow checks unaffected, zero regression on the existing recipe/SVO suites.
  **✅ DONE 2026-07-15, Opus-validated APPROVED** — commits `353e6b8e..aaa28116` (worktree
  `feat/recipe-parameterization-inc1`, one commit per task + a follow-up test-scope fix). Opcodes
  `ReadParam=96`/`ReadParamFloat3=111` mirrored from Yeroket canonical (confirmed byte-identical
  at validation time, not stale); opcode 110 correctly identified as `CurlNoise3D` (NOT free) and
  deliberately left unmirrored. `RecipeStackArity` entries `{0,1,0,0}`/`{0,3,0,0}` confirmed to
  match eval-code push counts. `paramMask` allow-list narrowed correctly — P4 opcodes require
  nonzero mask, every other opcode's `!=0` reject pinned by regression test
  `ParamMaskOnUnrelatedOpcodeStillRejected`. `evalRecipe`'s new `std::span<const float>` params
  arg confirmed truly additive (grepped every call site). Bounds-check fail-safe (0.0f on
  out-of-range) confirmed, no assert/crash. `PushParam` confirmed byte-identical/untouched.
  Scope containment confirmed via merge-base diff — zero shader/GLSL/RenderGraph files touched.
  **Test-scope finding (self-caught, fixed same milestone):** adding the 2 opcodes to
  `IsValidSdfOpCode` tripped `RecipeGlslOpcodeCoverage.CorpusCoversEveryValidOpcode` (a drift-guard
  expecting every valid opcode to be GLSL-corpus-exercised) since the GLSL emitter case is M2, not
  M1 — fixed with a narrow, documented, temporary allowlist for opcodes 96/111 pointing at M2 Task
  5 for removal. CPU gates green: `test_recipe_registry` 16/16, `test_recipe_eval_parity` 97/97,
  `test_recipe_occupancy` 7/7 (120/120 recipe tests, 13 new). Validator independently reproduced
  the build (Windows-native) and traced the 4 GPU-render test failures the implementer flagged
  (`VUID-VkComputePipelineCreateInfo-layout-10069/-07988`) to a pre-existing shader/pipeline state
  predating this branch's fork point (main is actually ahead on that code) — confirmed
  environmental, not a regression. 11 further Gaia/VoxelInjector failures independently matched to
  pre-existing **KI-027** (open, unrelated, predates this branch).
- **M2 — GLSL emitter + shader-side plumbing** (Tasks 5-7) · gate: emitted-GLSL compiles through
  glslang (WSL, no GPU needed) for a `ReadParam`-using program; `evalRecipeField`/
  `getRecipeBoundSphere`/uber-shader call site correctly pass the per-instance params array through
  to the emitted field function; CPU-vs-GLSL numerical parity harness extended to sweep parameter
  values (not just structural opcode coverage).
  - [ ] Not started.
- **M3 — Live zero-bake render + no-recompile proof** (Tasks 8-10) · **live-run gate, validation
  layers mandatory** · a registered `ReadParam` recipe renders as a virtual (zero-bake) body whose
  geometry visibly tracks `recipeParams[]` changes frame-to-frame; instrumented/logged proof that
  `SpliceProceduralRecipesIntoSource` / shader recompile does NOT fire on a pure param-value update
  (same bytecode, same instance count); baked (CPU-bake) evaluation of the same `ReadParam` recipe
  also produces correct, non-regressed geometry (bake-time snapshot of the param array).
  - [ ] Not started.
- **M4 — Parity gate + doc closure + sweep** (Tasks 11-12) · **live-run gate** · baked-vs-virtual
  geometry parity (reusing Lazy-Procedural M6's IoU harness) on a `ReadParam` recipe at a specific
  snapshotted parameter value; full no-regression sweep across the recipe/SVO/RenderGraph suites;
  format-contract and JIT-direction docs updated to reflect P4 shipped.
  - [ ] Not started.

### Progress Log

(populated as milestones complete — one entry per milestone: commit hash, gate evidence, Opus
validator verdict; follow the Lazy-Procedural / Sparse-Mip / Tiered-ESVO plans' convention.)

- **Milestone M1 (Tasks 1-4): DONE** · commits `353e6b8e..aaa28116` · Opus validator APPROVED ·
  2026-07-15. See Milestone Map entry above for full detail. Worktree
  `.claude/worktrees/recipe-param-inc1` (branch `feat/recipe-parameterization-inc1`), not yet
  merged to main — remaining milestones (M2-M4) continue on this branch/worktree before merge.

---

## Tasks

### M1 — Opcode + CPU-eval + registry validation

**Task 1 — Mirror the two opcodes into VIXEN's generated header.**
`libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h` is marked "GENERATED by Yeroket
kernel-codegen — DO NOT EDIT. Regenerate from the canonical SDFOpCode enum." Confirm at
implementation time whether this repo has a working regen path from `Yeroket-Fantasy` into this
checkout (the kernel-codegen framework / `codegen/` tooling referenced in
[[kernel-codegen-framework-direction]]) — if so, USE IT (regenerate, don't hand-edit) so the file
stays byte-provable against the canonical source. If no working regen path reaches this exact file
in this environment, hand-edit it to add exactly:
```cpp
ReadParam              = 96,
// ... (RestorePos = 97 unchanged, existing entries 98-109 unchanged)
ReadParamFloat3        = 111,
```
at the two confirmed-free slots (96 sits between `PushParam=95`/`RestorePos=97`; 111 is 2 past the
current last entry `Float3Normalize=109` — confirm 110 is intentionally left free or also
available; check the Yeroket canonical file for what, if anything, occupies 110 before assuming a
gap). Preserve the "DO NOT EDIT" banner's intent by adding a comment noting the hand-mirror and the
exact canonical source line numbers mirrored, so a future regen doesn't silently clobber-then-lose
context. Verify byte value against `Yeroket-Fantasy/Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs:143,171`
at implementation time (this plan was written against Yeroket main `99bce801`, 2026-07-14 — confirm
unchanged before trusting the numbers here).

**Task 2 — `RecipeStackArity` + `IsValidSdfOpCode` + registry `paramMask` allow-list.**
`libraries/SVO/include/Recipe/RecipeStack.h`: add `ReadParam` to the existing `{0,1,0,0}` case
group (alongside `PushParam` etc.); add a new `ReadParamFloat3` case returning `{0,3,0,0}`
(mirroring `PushFloat3`). `libraries/SVO/include/Recipe/RecipeRegistry.h`: add both opcodes to
`IsValidSdfOpCode`'s switch (lines ~14-52); change the `paramMask` check at line 114 from an
unconditional reject to: reject `paramMask != 0` UNLESS `opCode` is `ReadParam` or
`ReadParamFloat3`, in which case require `paramMask != 0` (an explicit non-zero marker — pick and
document the exact convention, e.g. `paramMask` must equal 1 to mean "this instruction reads
`data[0]` as a param-array index," so a `ReadParam` instruction with `paramMask==0` is ALSO
rejected as malformed, not silently treated as legacy `PushParam`-like). Add a
`RegisterResult` enumerator if the existing `ParamMaskUnsupported` name no longer fits the new
semantics (may need e.g. `ParamMaskMismatch` for "paramMask set on a non-P4 opcode" vs the P4
opcodes requiring it) — check callers of `RegisterResult` before renaming anything.

**Task 3 — `evalRecipe` params-array plumbing.**
`libraries/SVO/include/Recipe/SdfRecipeEval.h`: change `evalRecipe`'s signature to accept an
optional params source (e.g. `std::span<const float>` or `const float* params, size_t paramCount`
— prefer `std::span` per C++23; default to an empty span so every existing non-parameterized
call site compiles unchanged, this is an additive signature change, verify by grepping every
`evalRecipe(` call site before touching the signature). Add the `ReadParam` case (index
`static_cast<size_t>(in.data[0])` into the span, bounds-check and clamp/zero on out-of-range per
the Yeroket Burst reference's own `baseIdx < ctx.Parameters.Length ? ... : 0f` pattern — mirror
that exact fail-safe, don't assert/crash on a bad index since content authoring will get this
wrong sometimes) and `ReadParamFloat3` (three consecutive span reads at `idx*3`/`idx*3+1`/`idx*3+2`,
same bounds-check-per-component pattern). Keep the existing `PushParam` case (line 466-469)
completely unchanged — it is NOT being renamed or merged with `ReadParam`, they are distinct
opcodes with distinct semantics (baked constant vs. runtime-indexed read).

**Task 4 — Unit coverage for M1.** New/extended gtest cases (likely `test_recipe_eval_parity` or
a new `test_recipe_readparam` target, follow the existing test-file convention in
`libraries/SVO/tests/`): registering a `ReadParam`/`ReadParamFloat3` program succeeds; registering
one with `paramMask==0` on a `ReadParam` instruction fails with the right `RegisterResult`;
registering `paramMask!=0` on an unrelated opcode (e.g. `Sphere`) still fails (regression pin for
the narrowed-not-removed check); `evalRecipe` with a params span produces the expected value at
several indices including an out-of-range one (bounds-check proof); stack-arity/overflow checks
pass for both new opcodes at the 64-slot boundary (adapt the existing overflow-boundary test
pattern if one exists for `PushFloat3`).

### M2 — GLSL emitter + shader-side plumbing

**Task 5 — `EmitProceduralFieldFunctionGlsl` params threading.**
`libraries/SVO/include/Recipe/SdfRecipeCodegenGlsl.h`: the emitted `sdfRecipe_<id>(vec3 p)`
functions need a second argument for the params array (e.g. `float sdfRecipe_<id>(vec3 p, float
params[N])` or index into a shared bound array/buffer — decide based on M6's binding-budget
reality: **prefer reading directly from the existing `bodyInstances[instanceIndex].recipeParams[]`
SSBO field at the call site** rather than threading a `float[]` value-copy through every recipe
function argument list, since GLSL array-by-value function params are awkward and the data is
already resident in an SSBO the shader can index directly — confirm this against how
`evalRecipeField`/`getRecipeBoundSphere` are actually called in `BodyInstanceRayMarch.comp` before
committing to an argument-passing vs. direct-SSBO-index design). Add the `ReadParam` case (mirror
the existing `PushParam` case at line 724-728, but emit an indexed SSBO/array read instead of a
literal — exact syntax depends on the M5 design decision above) and `ReadParamFloat3` (mirror
`PushFloat3` at ~730, three-component indexed read). Both cases must NOT introduce a new GLSL
literal for the param VALUE — that would defeat the entire point (a literal bakes at splice/compile
time; the whole plan exists so param values do NOT require recompilation). Keep `f(v)` /
float-literal-guard usage exactly as-is for every OTHER opcode — this is the one deliberate
exception, document it with a short comment at the case site so it doesn't look like an oversight
in a later drift-guard pass.

**Task 6 — `evalRecipeField`/`getRecipeBoundSphere`/splice call-site wiring.**
`libraries/SVO/include/Recipe/UberShaderSplice.h`: thread whatever M5's design decided (an extra
function parameter, or confirm the direct-SSBO-index approach needs no signature change at all —
if the emitted `sdfRecipe_<id>` functions read `bodyInstances[]` directly, they need the current
`instanceIndex`/`gl_GlobalInvocationID`-derived index in scope, confirm it's already available at
the call site in `BodyInstanceRayMarch.comp`, likely is since `recipeId`/`providerKind` are already
read per-instance there). Update the demo/call site in `BuildRenderGraph.cpp` (~1667-1679, the
`VIXEN_PROCEDURAL_UBER_DEMO` seeding block) to populate `recipeParams[]` with a real varying value
per instance (not the current `unused: field samples world p directly` placeholder) so M3 has
something live to render and vary.

**Task 7 — Extend the numerical-parity harness.** The Lazy-Procedural M4 CPU↔GLSL parity harness
(opcode-coverage corpus, `test_recipe_glsl_numerical_parity` per the Inc0/Inc1 plan) validated
structural opcode coverage at fixed baked values. Add `ReadParam`/`ReadParamFloat3` corpus programs
and, critically, **sweep several different parameter-array values through the SAME compiled/spliced
program** (not just different bytecode) — this is the actual claim under test (params vary without
recompile) and the existing harness's single-fixed-value-per-program shape doesn't cover it as-is.
Real-GPU dispatch required (glslang-compiles-only is necessary but not sufficient here — this is
where the CPU-vs-GPU numeric agreement is actually checked).

### M3 — Live zero-bake render + no-recompile proof

**Task 8 — Live render: params drive visible geometry.** Windows-native, validation layers on. Run
the (updated) `VIXEN_PROCEDURAL_UBER_DEMO` with a `ReadParam`-using recipe (e.g. sphere radius or
box half-extent read from `recipeParams[0]`), varying the instance's `recipeParams[]` across
frames (a simple time-driven or scripted sweep is enough — this does not need the full
`SDFImplicitParams`-style engine sync, that's explicitly out of scope; a demo-side per-frame
`SetInstances` call with a changing value is sufficient proof). Checklist: (a) geometry visibly
changes frame-to-frame from the param sweep alone; (b) zero VUIDs; (c) **zero-bake still holds** —
grep the run log for `BakeSdfWorld`/`BuildSdfBodyOctree`/`BakeRecipeToSdfWorld`, none for the
param-driven bodies (this must NOT regress Lazy-Procedural M5's zero-bake proof).

**Task 9 — No-recompile proof.** Instrument or log-grep `SpliceProceduralRecipesIntoSource` /
`RecompileProceduralShader` / `MarkNodeNeedsRecompile` call counts (whatever the existing recompile
trigger's observable signal is — check `UberShaderSplice.h`/`VulkanGraphApplication.cpp` for the
established logging convention used to prove M5's zero-bake claim structurally, apply the same
rigor here) across N frames of pure param-value updates with UNCHANGED bytecode and UNCHANGED
instance count. Expected: zero recompiles. This is the single most important claim in this plan —
give it a dedicated, clearly-named test/log assertion, not just an eyeballed log read, so it's a
durable regression gate (a future change to `SetInstances`'s recompile-avoidance logic — noted as
already fragile/load-bearing in `BodyOctreeSceneNode.cpp`'s own comments about a "per-tick
recompile cascade race root cause" — could silently reintroduce a recompile-per-param-change
regression without this).

**Task 10 — Baked-path correctness for `ReadParam` recipes.** The CPU evaluator (`evalRecipe`,
M1 Task 3) also needs to work correctly when a `ReadParam` recipe is BAKED (not just rendered
virtual) — bake-time semantics = evaluate with whatever param array is current at bake time (a
snapshot, not dynamic — baking inherently produces a static voxel grid). Confirm/test that
`BakeRegistryToPool`/`BakeSdfWorld`'s call path to `evalRecipe` passes SOME params array (even if
just a caller-supplied snapshot or a documented default-zeros behavior if the recipe entry carries
no snapshot value) rather than leaving the params argument unset in a way that silently reads
garbage or crashes. Decide and document the bake-time snapshot source (recipe entry gains an
optional default-params field? caller passes explicit values? — pick the simplest option that
doesn't require new `RecipeEntry` fields if avoidable, e.g. bake call sites that don't care about
`ReadParam` can pass an empty span and rely on Task 3's zero-fill fail-safe, which is well-defined
behavior, not a footgun).

### M4 — Parity gate + doc closure + sweep

**Task 11 — Baked-vs-virtual parity gate for a `ReadParam` recipe.** Reuse the Lazy-Procedural M6
IoU harness (`test_baked_vs_virtual_parity` or successor) with a new corpus entry: a `ReadParam`
recipe baked with a specific snapshotted param value vs. the same recipe rendered virtual with
`recipeParams[]` set to the identical value at render time. Expect parity within the existing IoU
floor (0.75, per KI-LPD-003's established floor — do not weaken it). This proves the two
evaluation paths (CPU bake-time snapshot, GPU per-frame dynamic read) produce the same geometry
for the same effective parameter value.

**Task 12 — Full sweep + doc closure.** Run the full recipe/SVO/RenderGraph test suites, confirm
zero regressions (validator-classify any pre-existing failures, same discipline as every prior
milestone in this program). Update `Recipe-Container-Format-Contract-2026-06.md` §6 ("P4 planned"
→ "P4 shipped `<commit>`, see [[Recipe-Parameterization-Plan-2026-07]]"); update
`Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07.md`'s intro/§5 to mark the parameterization
keystone DONE and note this plan doc; if any genuine drift from the Yeroket canonical source was
found in Task 1, file it as a known issue (do not silently carry an undocumented divergence).

---

## Risks / decision points

- **Argument-passing vs. direct-SSBO-index for the GLSL params array (Task 5-6).** The plan
  deliberately leaves this open pending a fresh read of `BuildRenderGraph.cpp`'s current splice
  call-site shape at implementation time — do not guess a signature change without checking
  whether `instanceIndex` is actually in scope where `sdfRecipe_<id>` is called today.
- **`recipeParams` 6-float ceiling.** If M3's chosen demo recipe needs more than 6 dynamic values,
  stop and re-scope (either trim the demo or treat "grow the param carrier" as an explicit,
  separately-reviewed decision — not a silent scope creep mid-milestone).
- **Yeroket canonical-source drift.** This plan was scoped against Yeroket main `99bce801`
  (2026-07-14). If the opcode values (96/111) or the `ReadParam`/`ReadParamFloat3` semantics have
  changed by implementation time, re-verify before trusting any number in this doc — Task 1 says
  this explicitly, repeating it here because a stale opcode number is a silent, hard-to-detect
  correctness bug (wrong opcode ID = wrong instruction dispatched, not a crash).
- **No engine-wide implicit-parameter sync in v1.** Yeroket's `SDFImplicitParams.Sync()`
  (Time/Light/Camera auto-populated slots) is a genuinely nice future mirror once this ships, but
  building it now would be scope creep ahead of a real VIXEN consumer — noted as an explicit
  non-goal so a future session doesn't rediscover this design from scratch; when needed, it's a
  small, separable addition (a VIXEN-side `Sync`-equivalent populating well-known slots in
  `recipeParams[]` or a dedicated implicit-slot array before the opcode/stack mechanics change at
  all).
