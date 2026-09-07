---
title: TaskConsumer Contract Audit — Generalizing the Recipe Registry into a Declared GPU/CPU Task-Boundary
status: AUDIT / DESIGN (awaiting owner ratification — STOP before implementation)
created: 2026-09-07
author: taskconsumeraudit lane (senior-architect audit pass)
parent:
  - Codegen-Content-Boundary-Audit-2026-09-06.md
  - Kernel-Physics-Dispatch-Contract-Spec-2026-07.md
  - Multicore-Dispatch-Unification-Direction-2026-09.md
  - Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07.md
  - Recipe-Declared-Gaia-Query-Direction-2026-07.md
  - Renderer-Agnostic-View-Contract-Design-2026-07.md
  - ../Engineering/SDI-Tool-Library-Consolidation.md
tags: [architecture, task-consumer, recipe-registry, content-boundary, kernel-dispatch, gpu, cpu, shape-hash, hot-reload, domain-agnostic, sdi, shader-interface]
---

# TaskConsumer Contract Audit

**One-sentence goal (owner):** the recipe registry that today feeds only the stepping job should be generalized into a **TaskConsumer** — a GPU-or-CPU work boundary that *declares* the shape of tasks it consumes, its typed attribute set, the data boundary/domain it accepts, and its missing-data policy — so that GPU/CPU work *logic* migrates OUT of the VIXEN framework and is *fetched through the boundary* as kernel system-dispatch, with the recipe registry becoming exactly **one** instance of the contract.

**Scope of this doc:** an AUDIT + a contract DESIGN. No source changed. No implementation. The owner ratifies before any code moves.

**Headline finding.** A TaskConsumer is not a new invention layered onto empty ground — it is the **naming and unification of four contracts this codebase has already designed or half-built**, plus one genuinely new declared surface (per-attribute missing-data policy + domain-selection as first-class). The audit's job is to show that:

1. The **content boundary + shape-hash gate + dual-output + facade-total hot-reload ceiling** of the ratified [Codegen Content-Boundary Audit](Codegen-Content-Boundary-Audit-2026-09-06.md) apply to a TaskConsumer *unchanged* — a TaskConsumer's *declared contract* is the shape-hashed frozen surface; its *compute payload* crosses the boundary as content; its *interior tuning* hot-reloads. The recipe registry (already the mature render-tier content boundary in that audit) is the embryonic TaskConsumer.
2. The **domain-blind dispatch ABI** already exists twice: [`KernelDispatch::{Abi,Dispatcher,TaskExecutor,VirtualTask}`](Multicore-Dispatch-Unification-Direction-2026-09.md) is the ratified sole Tier-A executor (owner-approved 2026-09-01; scene baking just routed through it at wave `244d99a3`), and the [Kernel Physics Dispatch Contract Spec](Kernel-Physics-Dispatch-Contract-Spec-2026-07.md) already specifies the *declared* slot/field-semantic/domain-scope/scale-policy/consumer-injection ABI a physics TaskConsumer needs. A TaskConsumer is the **consumer-facing declaration** that lowers onto that ABI.
3. The **typed multi-channel attribute set** the contract needs is already designed: the [Domain-Agnostic Multi-Channel Recipe Output](Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07.md) direction defines a **union channel list with per-entry role flags** (`Stored | DerivedOutput | Outputtable`, each with `{semantic, elemCount, fieldKind}`) — that IS the TaskConsumer typed-attribute vocabulary, and its attribute-source distinction (stored/precomputed vs derived/recipe-computed) is the contract's attribute-source axis.
4. The **attribute-source + domain-selection** language is prefigured by the [Recipe-Declared Gaia Query](Recipe-Declared-Gaia-Query-Direction-2026-07.md) direction: a consumer TYPE declares the sim-side data it needs → generated query → batched per-type dispatch. That is the CPU/sim-domain analogue of the same "declared shape → fetched-through-a-boundary batch" the owner wants for GPU work.

**The genuinely new things this contract adds** over the sum of those: (a) a **per-attribute missing-data policy** and a **declared domain-selection** (normal / inverted-sparse / self-contained-object / recipe-as-bedrock) as *first-class parts of the consumer's declaration*, plus **step-behavior modification** (a consumer that changes how a ray *steps*, not just what it *samples*) as a declared consumer capability; and (b) **an indirect SDI dependency** — the consumer *declares* the shader-data-interface (bindings/layout) it accepts, so a shader-SHAPE change becomes a boundary/content event rather than a wide VIXEN recompile (owner requirement — §5, the shader-layer instance of the same recompile-coupling the facade tier had). Today all of these are hardcoded/compiled-in: a missing normal defaults to some literal in a `.comp`; a domain is whatever buffer the node happened to bind; and VIXEN `#include`s each merged `*-SDI.g.h` directly, so regenerating one on a shader-shape change recompiles its dependents. The contract lifts them to declared, shape-hashed, checkable data crossing the boundary.

**The migration is incremental, not big-bang** (this is the owner's explicit constraint and the ratified Multicore-Dispatch stance): a VIXEN node *provides* a TaskConsumer with a bounded shape+domain; the consumer's compute is *fetched through the boundary* and *lowered onto a `KernelDispatch::Stage`*; the recipe/default-step consumer migrates first because the registry already half-is one and the GPU backend is the one piece `KernelDispatch` has *declared but not yet implemented* (`Backend::GpuCompute` is a stub, D1 is CpuTbb-only — §Abi). The honest gate is that **no GPU task actually executes through `KernelDispatch` yet**; the first migratable consumer is the one whose GPU work can be expressed as a stage without waiting on the GPU backend, or whose CPU eval (SIMD recipe) can run through the existing CpuTbb path.

---

## 1. Inventory — the current GPU/CPU work-dispatch surface, grouped by consumer kind

### 1.1 The compute-shader surface, grouped by the owner's consumer KINDS

The tree holds 33 `.comp` files under `VIXEN/shaders/` (2 are `_backup` copies, 6 are FrameGraph sync test fixtures, 2 are unwired scaffolds). The real render-consumer set is the ~23 remaining. Grouped by the owner's four consumer kinds **A/B/C/D** plus render-support **E**:

**A — default-step / ray-march sampling job (samples distance/density/normals along a ray):**
- `VoxelRayMarch.comp`, `VoxelRayMarch_Compressed.comp` — the step job (uncompressed / DXT-compressed ESVO march). **This is the recipe registry's consumer today.**
- `BodyInstanceRayMarch.comp` — instanced multi-octree primary-visibility march (the production one).
- `ProxyIntervalPrepass.comp` — ray-interval AABB prepass (A-adjacent).

**B — transparent-step / through-material (conductivity, interior data): ZERO shaders exist.** This kind is a declared expressiveness target of the contract, not existing work.

**C — volumetric step-modifier (lensing/teleport/perspective): ZERO shaders exist.** Declared target, not existing work.

**D — physics / sparse-domain (mass/friction/temperature over empty/object domain): ZERO compute shaders exist.** The Kernel-Physics-Dispatch-Contract-Spec is the *design* for this kind; no shader implements it yet. Declared target.

**E — render support / accumulation / lighting / culling (does not fit A–D):**
- GI/lighting: `DirectLighting`, `SpatialReuseShade`, `SpatialReuseGather`, `ShadowVisibilityWave`, `ShadowRayTrace`, `ProbeGather`, `ProbeApply` (DDGI), `PhotonDeposit`/`PhotonCellFold`/`PhotonCellClear`.
- Accumulation/shade: `HitAccumulate`, `HitAccumClear`, `HitAccumCellShade`.
- Post/expose: `ExposureMeter`, `ExposureTonemap`.
- Cull/HiZ: `InstanceOcclusionCull`, `HiZDownsample`.
- Prep/derive: `RecipeInstanceBucketing` (buckets instances by recipeId — the batched-dispatch prep the Gaia-query direction wants), `ShellDerive` (GPU shell classification; a sparse-domain *sweep*, but scene-prep not physics), `SkySphereAccumulate` (unwired).

**Load-bearing finding:** only kinds **A** and **E** are populated today. **B, C, D are consumer kinds the contract must be able to *express*, not migrate** — the contract's expressiveness is validated against them, but the migration (§4) starts with the one kind that exists and is already half-a-consumer (A, the recipe/step job). This keeps the audit honest: designing the physics/step-modifier parts of the contract is designing for declared future consumers, grounded in the Physics-Dispatch-Spec and the owner's examples, not describing extant code.

### 1.2 How VIXEN dispatches GPU work today

The dispatch chain is `ShaderLibraryNode → ComputePipelineNode → ComputeDispatchNode | MultiDispatchNode`:
- **`ComputePipelineNode`** (`libraries/RenderGraph/{include,src}/Nodes/ComputePipelineNode.*`) builds the `VkComputePipeline` from a `ShaderDataBundle`, **auto-deriving the pipeline layout (descriptor-set layout + push-constant ranges) from shader reflection** (`ComputePipelineNode.cpp:212`). This reflection-derived layout is exactly the SDI coupling point (§5).
- **`ComputeDispatchNode`** (`.../Nodes/ComputeDispatchNode.*`) binds the pipeline+descriptors+push-constants and issues one `vkCmdDispatch` (`ComputeDispatchNode.cpp:490`). One node = one dispatch of one shader.
- **`MultiDispatchNode`** (`.../Nodes/MultiDispatchNode.*` + `Data/DispatchPass.h`) records many dispatches in one command buffer; each `DispatchPass` carries its own `VkPipeline`/`VkPipelineLayout`/`descriptorSets`/push-constants/`workGroupCount` (`DispatchPass.h:41-49`), dispatched at `MultiDispatchNode.cpp:594`.

**The logic is *in the node*.** The pipeline, descriptor wiring, push-constants, and dispatch counts are owned by the RenderGraph node — not fetched through a boundary. This is precisely what the migration must invert (§4): the node becomes a *provider* that lowers a consumer onto a `KernelDispatch::Stage`, while staying the owner of Vulkan lifetime.

### 1.3 How missing data is handled today (hardcoded in GLSL, one shared policy)

Because only kind A exists, there is exactly one missing-data policy, centralized in `shaders/StoredSdf.glsl` (near-field) and `shaders/MipFallback.glsl` (far-field), keyed on sentinels:
- **channel/brick absent → `1e9` distance sentinel** (`StoredSdf.glsl:63,75,85`); trilinear samplers take an explicit `missing` value (`:279,:298`).
- **degenerate/missing normal gradient → world-up `vec3(0,1,0)`** (`StoredSdf.glsl:451-454`, `_normalizeSdfGradient`; march seeds `hitNormal = vec3(0,1,0)` at `VoxelRayMarch.comp:142`).
- **coverage ≤ 0 → empty space, no hit** (`MipFallback.glsl:90`); absent color → flat grey (`:98`).
- **absent color/roughness → explicit `missingColor`/`missingRoughness` args** (`StoredSdf.glsl:356-363`).

This is the exact behavior the contract's per-attribute missing-data policy (§2.3) lifts to declared data: the owner's "missing normal → up" is `StoredSdf.glsl:451-454` today (a hardcoded literal, no cross-language single source, no CPU/GPU-parity guarantee). Making it a declared `Default(vec3(0,1,0))` or `DeriveFrom(distance-gradient)` policy is the generalization.

### 1.4 The recipe registry as the existing embryonic TaskConsumer

Confirmed as the mature render-tier content boundary (from the ratified boundary audit and this pass): `evalRecipe(...)` returns a single `float` (`SdfRecipeEval.h:41`) — distance only, single-output — matching the Multi-Channel direction's "recipes have NO output-channel concept" finding. The registry (`RecipeRegistry.h`), versioned container (`RecipeContainer.g.h`), pack loader (`RecipePackLoader.h`), and boot ingest (`RecipeBootIngest.h`) are the `(id-vocabulary + versioned blob + reader + registry + id-blind runtime)` content-boundary shape. Read as a TaskConsumer, it is the **default-step consumer with everything hardcoded that the contract makes declared** (§3.1 table).

---

## 2. The TaskConsumer contract

A **TaskConsumer** is a declared, domain-blind description of a body of GPU-or-CPU work that crosses the content boundary. It is authored in the same C# schema surface that already produces `AppFlow.g.h`, `Hud.blob.g.h`, and the recipe container (the ratified content-boundary producer), and it emits **both** a compiled-in `constexpr` header face and an external versioned blob face (dual-output — §Subsumption). It lowers at runtime onto a `KernelDispatch::Stage` (or a chain of them) bound to a `DispatcherProfile` that chooses the backend.

The declaration has **seven** parts. Each is a *declared*, shape-hashed thing — not hardcoded in a shader or a node.

### 2.1 Declared task SHAPE

The shape is *what a single unit of this consumer's work is dispatched over* and *how many*. It reuses the Physics-Dispatch-Spec `VxDispatchShape` vocabulary (`Linear / Sheet / Volume / RegionList / BrickList / BodyList / ParticleList / Indirect`) — which is exactly the set of domains VIXEN's compute shaders already dispatch over (a ray-march is a `Sheet` of screen pixels; a physics solve is a `RegionList` or `ParticleList`; instance bucketing is a `BodyList`). The shape maps directly onto the `KernelDispatch::Stage.itemCount` + the shape's index→work meaning:

```
TaskConsumerShape {
  DispatchShape   shape;         // Linear/Sheet/Volume/RegionList/BrickList/BodyList/ParticleList/Indirect
  DispatchDomain  domainSelect;  // §2.4 — WHICH subset of the tree the items range over
  uint            groupSizeXYZ;  // GPU workgroup, or CPU wave granularity
  bool            fromIndirectCount; // shape's item count is GPU-produced (Indirect)
}
```

The shape is the **frozen-surface** part: changing a consumer from `Sheet` to `RegionList`, or changing its domain-selection, is a shape edit → shape-hash changes → rebuild (§Subsumption). Changing the *number* of items (more instances, bigger screen) is interior — it rides the runtime count.

### 2.2 Typed ATTRIBUTE set

An attribute is a named, typed field the consumer reads or produces along/within its shape. This reuses the **union channel list with role flags** from the Multi-Channel Recipe Output direction verbatim — each attribute is:

```
Attribute {
  Semantic  semantic;    // codegen-emitted mirror-enum (SdfOpCodes.g.h style): Distance, Density,
                         //   Normal, Color, Roughness, Metalness, Emission, Conductivity,
                         //   Mass, Friction, Temperature, Pressure, Velocity, Stress, ...
  uint      elemCount;   // 1 (scalar), 3 (vec3 normal/color), ...
  FieldKind fieldKind;   // nullable — mean/min/max reduction for stored channels; null = derived
  RoleFlags role;        // Stored | DerivedOutput | Outputtable  (a bitset; e.g. Color = Stored|Outputtable)
  AttributeSource source; // §2.3
  MissingDataPolicy missing; // §2.3 — per-attribute
}
```

**This vocabulary already exists in code and can be reused, not invented** (MEASURED, this pass): `VoxelChannelFormat.h` declares `enum SemanticId { SEM_SDF, SEM_COLOR, SEM_ROUGHNESS, SEM_NORMAL, SEM_METALLIC, SEM_EMISSION, SEM_DENSITY }` and `enum FieldKind { FK_NONE, FK_DISTANCE, FK_DENSITY }` (the latter documented as "how a scalar field is integrated by the renderer — **declared by the data, not inferred**", which is exactly the contract's stance); the serialized descriptor `VoxelDocChannel { semanticId, elemCount, channelBaseFloats, fieldKind }` (`generated/VoxelDocument.g.h`) is the on-disk form of the `Attribute` struct above. The multi-output intent is also already declared: `RecipeSimd.g.hpp:1340` has `enum class RecipeOutputSlot { Distance=1, Gradient=2, Emission=4, Material=8 }` with the note that "emission and typed material channels can add bits without minting a second recipe identity **once the engine-side slot ABI is complete**" — i.e. multi-channel-from-one-recipe is designed but its ABI is unbuilt (today `evalRecipe → single float`, `SdfRecipeEval.h:41`).

This is the load-bearing reuse: the Physics-Dispatch-Spec's `VxFieldSemantic` (Occupancy/SignedDistance/Material/Velocity/Pressure/Temperature/Stress/Strain/…) and this `SemanticId` vocabulary are **the same enum, extended** — the physics attributes and the render attributes are one `Semantic` vocabulary, exactly as the Multi-Channel doc unified recipe-OUTPUT channels with voxel-STORAGE channels into one union list (decision #4/#6 of that direction). A TaskConsumer's attribute set is a *subset request* over that union list — and the Multi-Channel **subset-fused codegen** (one method, one DAG walk, requested channels only) is precisely how a consumer that requests `{distance, normal, conductivity}` gets a single fused evaluator rather than three walks.

### 2.3 Attribute SOURCE + per-attribute MISSING-DATA policy

Each attribute declares **where its value comes from** and **what happens when it is absent** — the genuinely-new declared surface.

**Attribute source** (two axes the codebase already distinguishes but never declared):
- `Precomputed` / `Stored` — read from a voxel storage channel (the `channels[]` layout the shaders scan at runtime today — the Multi-Channel `Stored` role).
- `RecipeDerived` — computed by evaluating the recipe/consumer's own DAG (the `evalRecipe`/`DerivedOutput` path — e.g. a normal derived from the distance gradient).
- `SimQueried` — populated from a declared sim-side query (the Recipe-Declared-Gaia-Query path: recipe type → declared `Undertow.View` field mapping → batched per-type fetch). This is the CPU/sim attribute source.
- `Constant` — a declared literal (interior — hot-reloadable).

**Per-attribute missing-data policy** models the owner's four examples directly:

| Policy | Meaning | Owner example it models |
|---|---|---|
| `Default(value)` | substitute a declared literal | "missing normal → global up"; "default 0 vs 1" for absent density |
| `DeriveFrom(otherAttr)` | compute from a present sibling | normal absent → derive from distance-field gradient (the Multi-Channel DerivedOutput dependency: Normal needs Distance) |
| `InheritNeighbor(mode)` | take the last-stepped-through / spatially-nearest value | "interior data islands that were never filled → inherit the last-stepped-through material's properties" (transparent-step) |
| `Densify(factor)` | request a denser sampling/shell for this attribute's domain | "should the shell job get a MORE DENSE shell?" (transparent-step) |
| `Reject` | the task is invalid without this attribute — fail-closed, no substitution | a physics solve that requires mass; the Physics-Spec "reject rather than downgrade silently" rule |

The policy is **per-attribute and declared** so it is shape-hashed (the *choice* of policy is frozen surface; a `Default`'s literal value is interior and hot-reloads). Today a missing-data substitution is a hardcoded GLSL fallback (§1.3: `1e9` distance sentinel, world-up default normal at `StoredSdf.glsl:451-454`, coverage≤0 = empty) with no cross-language single source; and where provenance IS tracked it is per-helper and ad-hoc (`RecipeBoundsSource { Authored, Derived, EngineDefault }` at `RecipeBounds.h:204` is the one place attribute-source is a first-class enum; occupancy uses `dim==0 → degrade`, params fail-safe to `0.0f`). The contract lifts these ad-hoc, per-helper policies into ONE declared per-attribute policy the CPU and GPU backends both honor identically (the CPU/GPU-parity requirement the Physics-Spec and Multicore-Dispatch both mandate for determinism). The `Precomputed`/`RecipeDerived`/`Constant` source axis is the generalization of the already-existing `RecipeBoundsSource` provenance enum.

### 2.4 DOMAIN-SELECTION

The domain is *which subset of the sparse tree the consumer's shape ranges over* — the owner's fourth example (physics is sparse-domain, and the selection is itself declared). This is new as a first-class declared axis; today a node just binds whatever buffer it was wired to.

```
DispatchDomain (declared, frozen-surface) :=
  | Normal            // the occupied cells of the sparse tree (the default-step consumer's domain)
  | InvertedSparse    // the EMPTY sections of the sparse tree — fluid/particle sim fills the void
  | SelfContainedObject(bodyId)  // one object's own sparse domain — stresses/breaking of a body
  | RecipeAsBedrock(recipeId)    // a domain layered ON TOP of a virtual recipe evaluated as bedrock
```

This reuses the Physics-Spec `VxRegionScope { regionSetId, bodySetId, mipRange, mayExpandScope }` as the *runtime binding* of the declared domain — the declaration names *which kind* of region set (normal/inverted/object/bedrock); the runtime resolves it to a concrete `regionSetId`. `mayExpandScope` (pressure/heat diffusing to neighbors) is the declared answer to "a consumer that touches beyond its own domain."

**This axis is the one part of the contract with essentially NO existing expression** (MEASURED, this pass): the tree exposes occupied cells only; there is no normal/inverted/object/bedrock concept in code — the nearest seed is the per-block `SdfCompositeRole { Field, Interval, SurfaceMod, Emissive }` (`RecipeComposition.h:10`), which is a CSG role, not a domain selector, and the word "bedrock" elsewhere is an unrelated tier/scale concept (`TierMath.h`). So domain-selection is genuinely new, and `InvertedSparse` is the structurally hardest piece — it requires the tree to expose its *complement* (the void) as an addressable region set, which does not exist today (§Risks).

### 2.5 STEP-BEHAVIOR modification

The volumetric-step-modifier kind (owner example 3) is a consumer that changes **how a ray steps** when it interacts with the consumer's domain, not just what it samples. This is a declared consumer *capability* with a declared hook:

```
StepModifier (optional; present only for the volumetric-step-modifier kind) {
  StepHook  hook;   // the transpiled body invoked at a step that enters this domain
  //   returns a modified ray-step: origin/direction transform, dt scale, or a teleport
  ModifierClass class; // PerspectiveShift | Teleport | GravitationalLensing | DensityScale
}
```

The `StepHook` is a **logic carrier** — a transpiled body (the same class as AppFlow handlers / recipe SIMD eval that the boundary audit found genuinely cannot cross as pure data). Its *signature* (what it reads: ray state + domain attributes; what it writes: modified ray step) is frozen surface (shape-hashed); its *body arithmetic* (the actual lensing math) is interior and hot-reloads exactly like a callable body (§Subsumption §6.1 of the boundary audit — "same signature, different arithmetic = interior"). This is the crucial expressiveness test: the contract must let a consumer declare *"I participate in the step loop"* as a typed hook, distinct from *"I supply samples."* Two consumers on the same ray compose by their declared domains and step order.

### 2.6 GPU-vs-CPU as a consumer property

Backend is **not** baked into the consumer — it is a `DispatcherProfile` choice, exactly as `KernelDispatch::DispatcherProfile.backends` maps a stage owner-id → `Backend` (§Abi). A TaskConsumer declares its *supported* backends (a `VxDispatchDomain`-style bitset: `CpuInline | CpuTbb | CpuSimd | GpuCompute | GpuAsyncCompute | Transfer`) and its determinism/parity requirement; the runtime profile picks one per LOD/context. This means:
- the default-step consumer declares `{GpuCompute}` (it's the ray-march);
- the SIMD recipe eval declares `{CpuSimd, CpuTbb}` (the header-only `RecipeSimd` path);
- a physics consumer declares `{CpuTbb, GpuCompute, GpuAsyncCompute}` and lets the profile route small irregular jobs to CPU/TBB and dense diffusion to GPU (the Physics-Spec default-routing table).

CPU/GPU **parity** is a declared determinism flag (the Multicore-Dispatch worker-count-invariance gate + the Physics-Spec deterministic-reduction contract): a consumer marked parity-required must produce the same observable result on any backend, which is what lets a missing-data policy or a step-hook be trusted to behave identically whichever backend runs.

### 2.7 How a VIXEN node PROVIDES a consumer with bounded data

A VIXEN RenderGraph node is the *provider*: it declares the **limitations of shape + data boundary** it can satisfy and binds the consumer's declared attributes/domain to concrete resources. This is the two-way bridge the Multicore-Dispatch direction already ratified (§6.1: "a `KernelStageNode` provider binds RenderGraph slots to a stage descriptor; RenderGraph remains responsible for resource lifetime, residency, barriers, frame placement"). Concretely:
- the node resolves the consumer's declared `Attribute` set to real slots/channels (the `channels[]` scan for `Stored` attrs; the recipe registry entry for `RecipeDerived`; the `Undertow.View` SoA slice for `SimQueried`);
- it resolves the declared `DispatchDomain` to a concrete region set (Tier-B residency realization);
- it lowers the consumer to `KernelDispatch::Stage`(s) and submits under a `DispatcherProfile`;
- it stays the owner of Vulkan lifetime — the consumer contract is *data-only* (the Physics-Spec `VxChainDesc` is "data-only, reusable across frames; runtime bindings produce a per-run handle").

The node's *declared limitation* (e.g. "I only provide `Sheet` shape over `Normal` domain, GPU backend") is itself a checkable contract: a consumer whose declared shape/domain the node can't satisfy is rejected at bind time (the Physics-Spec validation pipeline: header → layout → capability → field → scope → hazard → residency → queue → budget → determinism).

### 2.8 Declared shader-data-interface (the indirect SDI dependency)

A GPU TaskConsumer's eighth declared part is the **shader-data-interface it accepts** — the bindings/descriptor-layout/push-constant shape its compute payload expects — declared as *part of the consumer contract* rather than `#include`d as a compiled-in `*-SDI.g.h` header VIXEN links against. This is the shader-layer instance of the recompile-coupling the facade tier already solved: today a shader-SHAPE change regenerates the merged SDI header and recompiles VIXEN through it (§5). Under the contract, the consumer declares an **SDI contract** (binding table + layout descriptors + push-constant schema + specialization-constant dimension), which becomes part of the consumer's shape-hashed frozen surface; the *concrete* SDI (the reflected binding table of a specific compiled shader) is *fetched through the boundary* and checked against the declared contract at bind time. The precise contract-vs-interior line, blast-radius, and migration are §5; the key contract fact here is that **the SDI is a declared attribute of the consumer, not a header dependency** — a consumer says "I accept an SDI matching *this* declared binding/layout shape," and any shader whose reflected SDI conforms is accepted without a VIXEN recompile.

---

## 3. Subsumption — the recipe registry is one TaskConsumer, and the boundary design carries over unchanged

### 3.1 The recipe registry as the embryonic default-step TaskConsumer

The recipe registry is already `(stable-id vocabulary) + (versioned blob) + (reader) + (registry) + (id-blind runtime)` — the content-boundary shape the prior audit named. Read as a TaskConsumer, it is the **default-step consumer** with everything hardcoded that the contract makes declared:

| TaskConsumer part | Recipe registry today | Generalization |
|---|---|---|
| Declared shape | implicit `Sheet` (the ray-march dispatches over screen) | declared `DispatchShape` |
| Typed attributes | one output: `evalRecipe → float` (distance only) | union channel list, subset-requested (Multi-Channel direction) |
| Attribute source | recipe bytecode (`RecipeDerived`) + `channels[]` (`Stored`) | declared `AttributeSource` per attr |
| Missing-data policy | hardcoded GLSL fallback | declared per-attribute policy |
| Domain-selection | implicit `Normal` (occupied cells) | declared `DispatchDomain` |
| Step modification | none (pure sampler) | optional declared `StepModifier` |
| GPU/CPU | GLSL splice (GPU) + `RecipeSimd` (CPU), chosen by hand | declared supported-backends + `DispatcherProfile` |
| Boundary | `RecipeContainer.g.h` reader + `RecipeRegistry` + pack loader | unchanged — this IS the boundary |

So the registry is not replaced; it is **re-described**. The `recipeId` key becomes a TaskConsumer instance id; the registry becomes *one* registry of consumer instances (the render/default-step ones); physics/other consumers register in the same content-boundary shape.

**Reuse-vs-invent ledger (MEASURED, this pass).** The contract is mostly *naming and unifying* existing vocabulary, not inventing it:

| Contract part | Reuse (already in code) | Invent (no existing expression) |
|---|---|---|
| Typed attribute set | `SemanticId`/`FieldKind` (`VoxelChannelFormat.h`); serialized `VoxelDocChannel` (`VoxelDocument.g.h`) | — |
| Multi-attribute output | `RecipeOutputSlot {Distance,Gradient,Emission,Material}` (`RecipeSimd.g.hpp:1340`, ABI incomplete) | the fused subset codegen (Multi-Channel program) |
| Attribute source | `RecipeBoundsSource {Authored,Derived,EngineDefault}` (`RecipeBounds.h:204`); "0=default" sentinels | `SimQueried` source (Gaia-query direction, unbuilt) |
| Missing-data policy | ad-hoc per-helper degrade rules (occupancy dim=0, bounds ok=false, param→0) | ONE *declared, per-attribute* policy |
| Read/write + sparse | `KernelDispatch` `SlotRef`/`StateAccessKey`; SIMD `AccessMode`/`IterationMapping` (`RecipeSimd.g.hpp:29`) | — |
| Dispatch shape / field-semantic / scale | Physics-Dispatch-Spec `VxDispatchShape`/`VxFieldSemantic`/`VxScalePolicy` (designed) | — |
| Domain-selection | only `SdfCompositeRole` (CSG role) as a weak seed | normal/inverted/object/bedrock — **essentially all new** |
| Step-modifier | logic-carrier precedent (recipe eval / AppFlow handlers) | the declared step-hook + composition |
| GPU/CPU as profile | `KernelDispatch::DispatcherProfile.backends` (built; GPU backend declared-not-impl) | — |
| Declared SDI | fan-out already `<Meta,MEMBERS>`-generic; `LAYOUT_HASH`/`MEMBERS[]` exist | the runtime fetch+contract-check replacing the `#include` |

The only *from-scratch* invention is **domain-selection** (and the tree-complement capability it needs) and the **unified per-attribute missing-data policy**; everything else is unification of vocabulary the codebase already carries in scattered, single-purpose forms.

### 3.2 The boundary + shape-hash + hot-reload design applies unchanged

Every finding of the ratified Codegen-Content-Boundary audit generalizes to TaskConsumers with **no new mechanism**:

- **Content boundary:** a consumer's *declared contract* (shape, attribute set, sources, domain, policies, step-hook signatures) is emitted as a versioned blob and a compiled-in header (dual-output). A consumer's *compute payload* — the recipe bytecode / the transpiled step-hook / the GLSL splice — crosses as content, exactly as recipe bytecode already crosses `RecipePackLoader`.
- **Shape-hash gate:** the frozen surface (§2.1 shape, §2.2 attribute *declarations* + types, §2.3 source + policy *choice*, §2.4 domain-selection, §2.5 step-hook *signatures*, supported-backends) is shape-hashed; the interior (attribute *values*, `Default` literals, step-hook *body arithmetic*, per-instance params, dispatch *counts*) is excluded and hot-reloads. This is the boundary audit's §6 gate applied to a consumer: a consumer that adds an attribute / changes a domain-selection / adds a step-hook is a shape edit → detected → rebuild; a consumer that retunes a lensing constant or a default-normal value hot-loads.
- **Hot-reload ceiling (facade-total, code-internals-never):** a step-hook *body* hot-reloads (interior); a *new* step-hook or a *new* attribute needs a rebuild. The runtime reads the consumer through the registry/loader and copies at the boundary (no retained pointer into content) — so the four classic obstacles are structurally avoided, identically to the boundary audit's §7 thesis. The recipe path already proves this (`UberShaderSplice` re-emits GLSL from the registry at runtime).
- **Dual-output is still needed** for the same reasons (runtime cost, enum-stability, runtime shader recompile, ABI drift); `[GpuStruct]` config layouts stay compiled-in (all-shape).

**The one addition to the shape-hash surface** that TaskConsumers introduce: the **missing-data policy choice** and the **domain-selection** must be *in* the hash (they change what the consumer computes and over what), while the policy's *literal* and the domain's *runtime region-set id* must be *out* of it (interior). This is a clean extension of the boundary audit's frozen/fluid split, not a new gate.

---

## 4. Migration path — VIXEN GPU logic → kernel dispatch through the boundary, incrementally

The owner's constraint is explicit and matches the ratified Multicore-Dispatch stance: **not a big-bang.** The path is a sequence of ratify-gated slices, each turning one more piece of VIXEN-resident GPU/CPU logic into a consumer fetched through the boundary and lowered onto `KernelDispatch`.

**Honest starting-line facts (MEASURED, not assumed):**
- `KernelDispatch::Backend::GpuCompute` is *declared but not implemented* — D1 is CpuTbb-only (`Abi.h:42-49`, "D1 implements only CpuTbb; the rest are declared"). No GPU task executes through `KernelDispatch` today.
- `KernelDispatch` has exactly one production consumer path (RenderGraph `SlotTask` cutover M1 + scene-bake T-031 at wave `244d99a3`), all CPU. It is *omitted from the installed SDK export* until a real consumer + ABI gate exist (Multicore-Dispatch decision #8).
- The recipe content already hot-reloads as a registry/pack reload; the GLSL splice re-compiles at runtime from the registry.

**Therefore the first migratable consumer is chosen by what does NOT need the unbuilt GPU backend:**

**FIRST CONSUMER — the SIMD default-step recipe evaluator as a CPU TaskConsumer.**
- **Why it, not the GPU ray-march:** the recipe registry already half-is the default-step consumer, AND its CPU evaluator (`RecipeSimd.g.hpp` / `SdfRecipeEval`) runs through the *existing, implemented* CpuTbb/CpuSimd path — so this slice needs **no GPU backend work** and rides `KernelDispatch`'s proven Tier-A executor.
- **The slice:** declare the default-step consumer's shape (`Linear` over a recipe-instance/region list), its one attribute today (`distance`, `RecipeDerived`), its domain (`Normal`), backends `{CpuSimd, CpuTbb}`; lower the SIMD eval onto a `KernelDispatch::Stage` whose `perElement` invokes the fused evaluator; bind it through a provider node; emit the dual-output contract face + shape-hash.
- **The gate:** worker-count 1/2/N parity (the Multicore determinism gate) + byte-identical output vs the current `RecipeSimd` path + shape-hash stable across an interior recipe-param edit and different across an attribute-set edit. This proves the *contract* end-to-end on CPU without blocking on the GPU backend.

**Then, in order (each its own ratify-gate):**
1. **Attribute-set generalization** — land the Multi-Channel union channel list + subset-fused codegen so the default-step consumer can request `{distance, normal, color}` as one fused eval (this is the Multi-Channel program's own increment sequence; the TaskConsumer contract is its consumer-facing declaration).
2. **Missing-data policy as declared data** — replace one hardcoded GLSL/CPU fallback (e.g. missing-normal → up) with the declared per-attribute policy honored identically on CPU; gate on parity.
3. **GPU backend for `KernelDispatch::Stage`** — the genuinely large slice: implement `Backend::GpuCompute` so the GLSL ray-march lowers onto a stage. This is the point the GPU dispatch logic actually leaves VIXEN's `ComputeDispatchNode`/`MultiDispatchNode` and is fetched through the boundary. Gate: the default-step GPU consumer produces the same frame as the current ray-march node.
4. **Domain-selection + physics consumer** — implement `InvertedSparse`/`SelfContainedObject` region-set exposure and land the first physics TaskConsumer on the Physics-Dispatch-Spec ABI (its own P0-contract-only → P2 CPU/TBB → P3 single-queue-GPU sequence). Gate per the Physics-Spec validation pipeline.
5. **Step-modifier consumer** — the volumetric modifier kind; last because it needs the step-hook logic-carrier path and composition ordering. Gate: a lensing consumer visibly bends a ray with its body hot-reloadable.

No slice past #2 starts before the first CPU consumer proves the contract; no GPU slice (#3) starts before the CPU parity gate holds.

---

## 5. Hardening VIXEN against direct SDI dependency (owner requirement)

**Goal, one line:** a shader-SHAPE change should be a *boundary/content event* — hot-reloadable where it is interior, a clean declared-contract bump where it is shape — **not a wide VIXEN recompile.** This is the shader-layer instance of the exact recompile-coupling the facade tier solved; the TaskConsumer boundary must carry it too.

### 5.1 What an SDI is, and the coupling today (MEASURED)

An **SDI (Shader Data Interface)** is a generated header (`VIXEN/generated/sdi/merged/*-SDI.g.h` — **15 files**, one per interface-distinct compute shader, not one per `.comp`) produced by reflecting a shader's compiled SPIR-V via `ShaderManagement`'s `SpirvReflector`/`SpirvInterfaceGenerator`. It is a **pure descriptor/interface mirror**, not a std430 data-struct the app fills. Each header (`namespace ShaderInterface::<Program>`) declares:
- `enum class Access { ReadWrite, ReadOnly, WriteOnly }` per binding (from SPIR-V decorations);
- per-resource layout metadata with a `LAYOUT_HASH` + `pc_N { OFFSET, SIZE, BINDING }` tables (0-byte structs — the layout is *described*, not instantiated);
- `namespace Set0 { struct BindingN { SET; BINDING; VkDescriptorType DESCRIPTOR_TYPE; COUNT; Access ACCESS; using DataType; } }` — one per descriptor slot;
- `namespace Push { SIZE; struct <field> { INDEX; OFFSET; SIZE; } }` — the push-constant field table;
- a flat `inline constexpr MemberInfo MEMBERS[]` table + `struct Metadata { PROGRAM_NAME; NUM_MEMBERS; NUM_FEATURES; }`.

**There are NO specialization constants.** The variant axis is instead boolean **interface-affecting `#define`s** (a `FEATURES[]`/`featureCount` tag per member; e.g. `BodyInstanceRayMarch` has a 6-define feature axis), driven by `VIXEN/shaders/sdi-variants.json` (15 program entries, each with a `variants` array of define-sets). Generation is a **CTest drift-gate**, not build-time regen: `sdi_tool merge-variants sdi-variants.json --check` writes nothing and exits code 6 on drift; regen is the same command without `--check`, run manually / forced by the failing gate.

**The coupling (MEASURED).** VIXEN `#include`s the merged `*-SDI.g.h` headers **directly**, and the fan-in is **concentrated in ONE production TU: `application/main/source/graph/BuildRenderGraph.cpp`** — a **9,918-line** file that includes **13 of the 15** merged headers (plus 2 legacy `-SDI.h`), aliases each namespace, and consumes `Sdi::Metadata` + `Sdi::MEMBERS` (as template args) and `Sdi::Push::<field>::INDEX` / `Sdi::Bind::*` directly. So a shader-SHAPE change to *any* of those 13 shaders regenerates its SDI header and **rebuilds the entire ~10k-line `BuildRenderGraph.cpp`** — the single most expensive TU in the app. The blast radius is *concentrated* (one TU, not a wide graph) but *severe* (that one TU is enormous and is the choke point for every shader's contract).

**The crucial architectural finding that makes the fix tractable:** the dispatch/binding fan-out layer is **already SDI-generic**. `SdiStageSynthesis.h::SynthesizeComputeStage<Meta, Members>`, `SdiStageWiring.h::WireStageFromSdi<Meta, Members>`, and `SdiHazardCensus.h::CensusStageFromSdi<Meta, Members>` are **templated on `<typename Meta, const auto& Members>` and include NO `-SDI.g.h`** — the concrete SDI type binds only at the call site inside `BuildRenderGraph.cpp`. The descriptor gatherer / `DescriptorSetNode` / `ComputePipelineNode` (which reflection-derives the layout at `ComputePipelineNode.cpp:212`) carry no per-shader SDI include. **So the indirection is nearly built already:** VIXEN's binding machinery already works off an abstract `(Metadata, MEMBERS[])` pair; the only thing anchoring it to a compiled header is that `BuildRenderGraph.cpp` supplies that pair by `#include`-ing a `constexpr` SDI. Replacing that compile-time supply with a *runtime-fetched `MEMBERS[]` checked against a declared contract* is the whole move — the consuming templates need not change.

The prior boundary audit classified "merged SDI shader-interface headers = semantic shader wiring = graph shape" as **all-shape, inherently not hot-reloadable** — correct *for a compiled-in header*. The owner requirement goes further: make the dependency **indirect** so VIXEN depends on a *declared SDI contract* (part of a TaskConsumer), not on the concrete compiled header — which the already-generic fan-out layer makes a small, well-targeted change.

### 5.2 The precise contract-vs-interior line for SDIs (and it is already half-drawn)

The ratified shape-hash gate gives the tool, and — importantly — **the SDI machinery already draws this line at the manifest level.** `sdi-variants.json` only promotes *interface-affecting* defines (`layout(set=...)`/push-changing) to variant axes; it explicitly excludes value-defines and interface-invariant toggles (`VIXEN_SHADOW_DBG_PX/_PY`, `VIXEN_SHADOW_NO_MIP_ANYHIT`, `VIXEN_COMPOSED_TRAVERSAL`, `VIXEN_BRICKMAP_TRAVERSAL` — "add no new bindings/interface to any `.comp` entrypoint"). So an interior shader-body edit already produces a byte-identical SDI → drift-check passes → the `.g.h` is not rewritten → `BuildRenderGraph.cpp` does not recompile. **Interior shader edits already cost zero VIXEN recompile today.** The gap the owner wants closed is entirely on the *shape* side: a genuine interface change funnels into the one giant TU.

The clean split, drawn by *what the binding side must agree with the shader on*:

| SDI aspect | Contract (frozen — in the shape-hash → a change is a declared-contract bump → rebuild) | Interior (fluid — same contract, different shader → NO VIXEN recompile) |
|---|---|---|
| Descriptor **bindings** (`Set0::BindingN`: set, binding, descriptor type, count, access) | **Contract** — the binding table is what the pipeline layout + descriptor writes + hazard census agree on | — |
| Push-constant **field table** (`Push::<field>` INDEX/OFFSET/SIZE) + resource `LAYOUT_HASH` | **Contract** — this IS the C++↔shader ABI (same as `[GpuStruct]`, all-shape); `BuildRenderGraph.cpp` reads `Push::<f>::INDEX` directly | — |
| **Feature-define dimension** (which interface-affecting `#define`s exist → the `FEATURES[]`/variant axis) | **Contract** — the *set* of interface variants is shape (a new feature-gated binding is a new contract) | The *active* feature set chosen per dispatch is interior (`Members(activeFeatures)` already filters at runtime) |
| Shader **body / GLSL logic** (same bindings + same layout + interior-only defines) | — | **Interior** — a new shader body against the same declared SDI must NOT recompile VIXEN (already true via the drift-gate) |
| Compiled **SPIR-V blob** for a given shader | — | **Interior** — the payload crosses as content (like recipe bytecode / the GLSL splice re-compiled at runtime) |
| Which **shader** a consumer runs (swap one conforming shader for another) | — | **Interior** — the consumer accepts any shader whose reflected `MEMBERS[]` conforms to its declared contract |

Note there are **no specialization constants** to place on this line — the variant dimension is the feature-define axis above, which is already handled at runtime by `Members(activeFeatures)`. The rule: **the SDI's binding table + push/layout + feature-define dimension is the declared contract (shape-hashed); the SDI's shader body + SPIR-V + active-feature choice is interior content.** A binding/push/feature-axis change is a contract bump (shape-hash changes → gate forces a rebuild, safe by construction); a body-only change is interior (already free today). The remaining problem the contract solves is that *even a legitimate contract bump* currently rebuilds the whole 10k-line TU — the indirection localizes that to a declared-contract-hash comparison instead.

### 5.3 The SDI as a declared TaskConsumer attribute (design)

A GPU TaskConsumer declares an **SDI contract** as part of its frozen surface (§2.8). Its shape mirrors exactly the `(Metadata, MEMBERS[])` pair the fan-out templates already consume — so the declaration is the *runtime-fetched* form of what `BuildRenderGraph.cpp` today supplies by `#include`:

```
SdiContract (part of the consumer's shape-hash) {
  BindingDesc[]  bindings;   // Set0::BindingN mirror: set, binding, descriptorType, count, access
  PushField[]    push;       // Push::<field> mirror: index, offset, size  (+ resource LAYOUT_HASHes)
  FeatureAxis[]  features;   // the interface-affecting #define dimension (the variant axis) — NOT spec constants
  uint64         sdiContractHash; // FNV-1a over the above canonical serialization (excludes body/SPIR-V/active-feature choice)
}
```

- At author time, the consumer's declared `SdiContract` is emitted in **both** faces (compiled header + external blob) with the `sdiContractHash` stamped into both (the dual-output + single-source-hash discipline the View program proved). This hash *is* the merged-SDI's existing `LAYOUT_HASH`/`Metadata` content, canonicalized — the machinery to compute it already exists in `sdi_tool`.
- At bind time, the provider node fetches the **concrete SDI** (`MEMBERS[]` + push/binding tables of the compiled shader, crossing the boundary as content) and **checks it against the declared `SdiContract`**: conforms → hand the fetched `(Metadata, MEMBERS)` to the *unchanged* generic fan-out templates (`WireStageFromSdi`/`CensusStageFromSdi`/`SynthesizeComputeStage`) → bind + dispatch, no recompile; differs → the shader's interface no longer matches the consumer's declared contract → contract bump, reject-and-message-rebuild (the ratified gate's detect-and-message fallback).
- VIXEN therefore **stops `#include`-ing `*-SDI.g.h` directly in `BuildRenderGraph.cpp`**; it depends only on the consumer's declared `SdiContract` (a small, stable, shape-hashed thing) and feeds the concrete `MEMBERS[]` to the already-generic templates at runtime. The concrete per-shader SDI becomes runtime-checked content, not a compile-time include — and the 10k-line TU stops rebuilding on every shader-shape edit.

Because the fan-out is *already* `<Meta, Members>`-templated (§5.1), this does not require rewriting the binding/dispatch layer — only changing how `BuildRenderGraph.cpp` obtains the `(Metadata, MEMBERS)` pair (from a runtime fetch + contract check instead of an `#include`).

### 5.4 Migration — VIXEN stops depending directly on SDIs

This composes with the "GPU logic migrates VIXEN→kernel through the boundary" arc — **the SDI is part of what crosses.** Incremental, ratify-gated:

1. **Declare-and-check (no include removal yet).** For the first GPU consumer (the default-step ray-march, after §4 slice #3 builds the GPU backend), author its `SdiContract` and add the *runtime conformance check* of the concrete SDI against the declared contract — while still `#include`-ing the header. Proves the check is correct with zero risk. *Gate: the concrete SDI conforms to the declared contract for the current shader; a deliberately-mutated binding is caught.*
2. **Cut one SDI include out of `BuildRenderGraph.cpp`.** Replace one shader's direct `#include "*-SDI.g.h"` + call-site `Metadata`/`MEMBERS` binding with a runtime fetch checked against the declared `SdiContract`, then hand the fetched pair to the unchanged generic templates. *Gate: a shader-BODY change (already free) still costs nothing; a shader-SHAPE change to that one shader no longer rebuilds `BuildRenderGraph.cpp` unless its declared contract actually changed — and when it does change, it is caught by the contract hash and messaged as a rebuild.* This is the concrete win the owner named, proven on one shader.
3. **Roll across the 13 includes in the one TU.** Migrate the remaining SDI includes in `BuildRenderGraph.cpp` to the runtime-fetch+contract-check form, shader-by-shader, until the giant TU depends on **zero** concrete SDI headers — only on the small declared `SdiContract` set. Each shader is its own gate. Because the fan-out templates are already generic, this is mechanical per shader.
4. **Feature-variant axis as content.** Carry the `sdi-variants.json` interface-affecting-`#define` dimension as the *declared* `FeatureAxis[]` in the contract, with the *active* feature set chosen per dispatch crossing as content (the runtime `Members(activeFeatures)` filter already exists) — so adding a shader body under an existing feature axis, or choosing a different active feature set, does not recompile the TU (§5.5 risk 2).

Note the ordering dependency: SDI-indirection for a GPU consumer is downstream of that consumer having a GPU backend to lower onto (§4 slice #3). The CPU-first consumer (§4 first consumer) has no SDI (no shader), so SDI-hardening genuinely begins with the first GPU consumer.

### 5.5 SDI-specific risks

1. **SDI ABI stability across the boundary.** The concrete SDI now crosses as content and is checked at runtime against a declared contract — the check must be *exact* on binding indices, descriptor types, and std430 offsets/sizes, or a silently-nonconforming shader binds wrong memory (the same enum/ABI-drift hazard the boundary audit flagged for `[GpuStruct]`, now at the descriptor level). The `sdiContractHash` must hash the canonical layout, never the body; a mismatch is fail-closed.
2. **The feature-variant dimension** (not spec constants — there are none). `sdi-variants.json` drives per-variant SDI generation off interface-affecting boolean `#define`s. A variant that adds/removes/re-slots a *binding* (even feature-gated) is a contract bump; a variant that only toggles an interior branch is already excluded from the SDI (the manifest does this today). The contract must declare the *feature axis* (which interface-affecting defines exist) as frozen while letting the *active feature set per dispatch* cross as content (`Members(activeFeatures)` already filters at runtime) — otherwise the localization is lost. **Open question: is the set of interface variants itself (which feature combinations are pre-baked) contract or content?** Leaning: the *feature axis* (which defines exist) is contract, the *active set per dispatch* is content, and the *set of pre-baked variant SPIR-V* is a content manifest (like the recipe pack).
3. **What genuinely must stay a compiled interface.** The std430 UBO/push-constant *layout* is a true compile-time ABI (identical to `[GpuStruct]`, which the boundary audit collapsed to compiled-in as all-shape). The contract does not make the *layout* hot-reloadable — it makes VIXEN depend on the *declared layout contract* rather than the concrete header, so a layout change is a clean declared-contract bump (rebuild) instead of a silent header-regen fanning out. The win is *localizing and gating* the recompile, not eliminating it for genuine ABI changes.
4. **Reflection is the source of the concrete SDI.** The concrete SDI is derived by reflecting compiled SPIR-V (`SpirvReflector`). The conformance check depends on that reflection being deterministic and complete; a reflection gap (e.g. an unreferenced binding optimized out) could make a conforming shader appear non-conforming. This is a real correctness dependency on `ShaderManagement`, not just a declaration.

---

## 6. Risks / obstacles

1. **Typed-attribute expressiveness vs. the fused-codegen oracle.** The attribute set only pays off if a subset request fuses into one walk (Multi-Channel §6) — and the fusion MUST be byte/numerically identical to N-separate evals, validated against an *independent* reference, not fused-vs-its-own-flatten (the circular-oracle trap that direction flags). If fusion is wrong, every multi-attribute consumer is silently wrong.
2. **Missing-data policy as declared vs. hardcoded.** The hazard is CPU/GPU divergence: a `Default`/`DeriveFrom`/`InheritNeighbor` policy must produce bit-identical substitution on every backend or the parity/determinism gate breaks. `InheritNeighbor(last-stepped-through)` is the hardest — it is *stateful along the ray*, so it is really a mini step-modifier, not a pure substitution; the contract must decide whether it is a policy or a step-hook (leaning: step-hook, because it reads ray history). **Open question.**
3. **Domain-selection generality — InvertedSparse is a real capability gap.** The sparse tree exposes its *occupied* cells; addressing its *complement* (the void, for fluid/particle sim) as a region set does not exist today. This is net-new tree capability, not a declaration. `RecipeAsBedrock` similarly needs a virtual recipe evaluated as a domain floor. These are the structurally hardest parts and gate the physics consumer.
4. **The GPU/CPU split is aspirational until `Backend::GpuCompute` exists.** The entire "GPU logic migrates out of VIXEN into kernel dispatch" goal is blocked on an unbuilt backend. Until then, TaskConsumers are a *CPU* generalization + a *declaration* that GPU consumers will lower onto once the backend lands. This must be stated plainly to the owner: the contract can be ratified and CPU-proven now; the GPU migration is downstream of the GPU backend milestone (Physics-Spec P3 / Multicore-Dispatch M-later).
5. **What genuinely can't cross as data (must stay compiled).** Step-hook bodies, recipe SIMD/GLSL codegen, and the `SdfInstruction` layout mirror are logic/ABI carriers (the boundary audit's §9 finding). A consumer's *body arithmetic* hot-reloads (interior); a *new* step-hook / *new* opcode / *retyped* attribute needs a rebuild. Full code-internals reload (a `.so`/dlopen path) does not exist and is not required within the facade-total ceiling.
6. **Determinism.** Every authoritative consumer must be worker-count invariant (Multicore gate) and, for gameplay-relevant physics, deterministic under the Physics-Spec seed/reduction/replay contract. A step-modifier that reads ray history and a physics solve that expands scope are the determinism-fragile cases.
7. **Where the current VIXEN structure fights this.** GPU work is dispatched by RenderGraph nodes (`ComputeDispatchNode`/`MultiDispatchNode`) that own pipeline+descriptors+push-constants+dispatch inline — the logic is *in the node*, not fetched through a boundary. Lowering a node's dispatch onto a `KernelDispatch::Stage` while keeping RenderGraph the owner of Vulkan lifetime is exactly the two-way-bridge adapter the Multicore-Dispatch direction sized as its own program; the TaskConsumer contract depends on that bridge existing for GPU consumers.
8. **SDI recompile choke point (owner requirement — §5).** All shader-interface coupling funnels into one 9,918-line TU (`BuildRenderGraph.cpp`, 13 of 15 SDI includes), so any shader-SHAPE change rebuilds it whole. Mitigated by the SDI-as-declared-contract design (§5), which is tractable because the fan-out layer is already `<Metadata, MEMBERS[]>`-generic — but the migration is downstream of a GPU consumer existing (the CPU-first consumer has no shader), and the conformance-check correctness depends on deterministic SPIR-V reflection (§5.5).

---

## 7. OPEN QUESTIONS (owner)

1. **Is `InheritNeighbor(last-stepped-through)` a missing-data policy or a step-modifier?** It reads ray history (stateful along the ray), so it does not fit the pure per-attribute substitution model. Recommendation: model it as a step-hook (§2.5), and keep missing-data policies stateless (`Default`/`DeriveFrom`/`Densify`/`Reject`). Ruling needed because it changes whether the transparent-step kind needs the step-modifier machinery.
2. **Does the attribute `Semantic` vocabulary unify render channels and physics field-semantics into ONE enum** (my §2.2 assumption, following the Multi-Channel unification), or are they two vocabularies with a mapping? One enum is cleaner and matches the Multi-Channel decision #4/#6; confirm it extends to physics (Mass/Friction/Stress) without collision.
3. **Domain-selection: is `InvertedSparse` (void addressing) in scope for the contract now, or declared-but-unimplemented** (like `Backend::GpuCompute`) until the tree-complement capability is built? It is the structurally hardest piece and gates the fluid/particle physics consumer.
4. **First consumer confirmation.** I recommend the **CPU SIMD default-step recipe eval** as the first migratable consumer (rides the implemented CpuTbb path, proves the contract without the unbuilt GPU backend). The alternative — waiting for `Backend::GpuCompute` and migrating the GPU ray-march first — is truer to "GPU logic migrates out" but blocks the whole arc on a large backend milestone. Confirm the CPU-first sequencing.
5. **Where does a TaskConsumer declaration live** — bolted onto the recipe-registration schema (smallest, per the Gaia-query doc's "maybe no new mechanism" note), a new codegen face, or the kernel-core (domain-agnostic, AppFlow-style split as the Multi-Channel direction chose for its contract types)? Kernel-core is the consistent answer if physics/Rust/Python consumers must target the same ABI (Physics-Spec §8 P7), but it is cross-repo work.
6. **CPU/GPU parity strictness for step-modifiers.** A gravitational-lensing step-hook may be acceptable as GPU-only (no CPU parity) since it is render-only, whereas a physics consumer must be parity-strict. Confirm that `parity-required` is a per-consumer declared flag, not a global invariant.
7. **SDI: is the set of pre-baked feature variants contract or content?** The feature *axis* (which interface-affecting defines exist) is clearly contract; the *active set per dispatch* is clearly content (already runtime-filtered). Ambiguous: the *set of which variant combinations are pre-baked into SPIR-V*. Recommendation: treat it as a content manifest (like the recipe pack), so adding a pre-baked variant is a content change, not a recompile. Confirm.
8. **SDI: which conformance strictness?** When the fetched concrete SDI differs from the declared contract only in a *superset* way (the shader exposes a binding the consumer doesn't use), is that a conformance pass (consumer uses a subset) or a contract bump (reject)? Subset-tolerance is more flexible but risks silent under-binding. Recommendation: exact-match on the declared subset, ignore-with-log on extra shader bindings. Confirm.

---

## 8. Summary

- A **TaskConsumer** is the *naming and unification* of four already-designed/half-built contracts — the ratified content boundary (Recipe registry + shape-hash + dual-output + hot-reload), the domain-blind `KernelDispatch` ABI, the Physics-Dispatch declared slot/field/domain/scale ABI, and the Multi-Channel typed-attribute union-list — plus **one genuinely new declared surface**: per-attribute missing-data policy, first-class domain-selection, and step-behavior modification.
- The contract has **eight declared parts**: task shape, typed attribute set, attribute source, per-attribute missing-data policy, domain-selection, step-behavior modification, GPU-vs-CPU-as-a-profile-choice, and the declared shader-data-interface (SDI). A VIXEN node *provides* a consumer with a bounded shape+domain and owns Vulkan lifetime; the consumer's compute is *fetched through the boundary* and *lowered onto a `KernelDispatch::Stage`*.
- The **recipe registry is exactly one TaskConsumer** — the default-step consumer — with everything the contract declares currently hardcoded. It is re-described, not replaced; the content-boundary/shape-hash/hot-reload design applies **unchanged**, with the single clean extension that the missing-data-policy *choice* and domain-selection join the shape-hash while their literals/region-ids stay interior.
- **Migration is incremental and CPU-first.** The first migratable consumer is the **SIMD default-step recipe evaluator on the implemented CpuTbb path** — it proves the whole contract end-to-end without waiting on the unbuilt `Backend::GpuCompute`. GPU migration (the ray-march leaving `ComputeDispatchNode`) is gated behind implementing the GPU backend.
- **SDI hardening (owner requirement, §5):** VIXEN today `#include`s the 15 merged `*-SDI.g.h` headers, funneled into **one 9,918-line TU (`BuildRenderGraph.cpp`, 13 of 15)** — so any shader-SHAPE change rebuilds that whole TU (concentrated but severe). The fix is small because the fan-out layer is *already* `<Metadata, MEMBERS[]>`-generic (`WireStageFromSdi`/`CensusStageFromSdi`/`SynthesizeComputeStage` include no SDI): declare the SDI (bindings + push/layout + feature-define axis) as part of the consumer contract, shape-hash it, fetch the concrete `MEMBERS[]` as runtime content, check it against the declared contract, and hand it to the unchanged templates. Then a shader-BODY change stays free (already true via the drift-gate), and a shader-SHAPE change is a localized declared-contract-hash bump instead of a 10k-line recompile. The genuine ABI layout stays compiled-in (like `[GpuStruct]`); the win is *localizing and gating* the recompile, not eliminating it for real interface changes.
- **Top risk:** the GPU/CPU migration goal is blocked on `KernelDispatch::Backend::GpuCompute`, which is declared-but-unimplemented today; the contract can be ratified and CPU-proven now, but "GPU logic migrates out of VIXEN" (and the GPU-consumer SDI-indirection that rides it) is downstream of that backend milestone. Second risk: `InvertedSparse` void-domain addressing is net-new tree capability, not a declaration.

**STOP — awaiting owner ratification of the eight-part contract (§2, incl. §2.8 declared SDI), the subsumption + shape-hash extension (§3), the CPU-first migration path + first consumer (§4), the SDI-hardening design (§5), and the §7 open questions before any implementation.**
