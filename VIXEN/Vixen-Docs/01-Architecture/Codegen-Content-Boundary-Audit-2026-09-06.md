---
title: Codegen Content-Boundary Audit — Externalizing VIXEN-Consumed Codegen for Hot-Reload
status: AUDIT / DESIGN (awaiting owner ratification — STOP before implementation)
created: 2026-09-06
author: facadeaudit lane (senior-architect audit pass)
parent:
  - AppFlow-Framework-Design-2026-07.md
  - AppFlow-Kernel-Glue-Transplant-Reframe-Design-2026-07.md
  - View-Contract-Inc4-View-Action-AppFlow-Convergence-Design-2026-07.md
  - View-Contract-Inc2b-Reflection-Blob-Design-2026-07.md
  - Renderer-Agnostic-View-Contract-Design-2026-07.md
tags: [architecture, codegen, content-boundary, hot-reload, appflow, view-contract, recipe-registry, agnostic]
---

# Codegen Content-Boundary Audit

**One-sentence goal (owner):** every codegen output VIXEN consumes should cross an *agnostic content boundary* — loaded as external, swappable content — so a small codegen change does **not** recompile the renderer, and eventually a facade/UI change **hot-loads** into the running app.

**Scope of this doc:** an AUDIT + boundary DESIGN. No source changed. No implementation. The owner ratifies before any code moves. Tier-1 priority is the **facade/UI (AppFlow + View)** surface; the render tier (Recipe/SVO) is tier-2.

**Headline finding.** The agnostic content boundary the owner wants is **not a new invention — it is already built twice in this codebase, in two different maturities**, and the audit's job is to name it, unify it, and close the two specific gaps that keep it from delivering hot-reload:

1. **Render tier (tier-2) — MATURE.** The **Recipe Registry** is already a full content boundary: a versioned blob reader (`RecipeContainer.g.h`, magic `'VRC1'` + `formatVersion`), a runtime ingest/pack pipeline (`RecipeIngest` / `RecipeBootIngest` / `RecipePackLoader`), and a manifest+registry layer (`RecipeManifest` / `RecipeRegistry`). Recipe *content* already crosses as data today. Per the owner's steer, the render-tier answer is **reuse this registry** — not design a new mechanism.
2. **Facade tier (tier-1) — HALF-BUILT.** The **View Contract** already has the exact dual-output the owner wants: `ViewBlobEmitter` (`--view-blob`) emits **both** a compiled-in `constexpr` header (`Hud.blob.g.h`) **and** a runtime data file (`hud.viewblob`, `# viewblob v1` + a schema-version hash), and a generic runtime host `BlobView : IView` + `ViewBlobFile` parser consume it. But **AppFlow has no equivalent** — `AppFlowContainerView` is *only* the compiled-in `constexpr` header form; there is no `.appflow` data file, no parser, and no `BlobAppFlowView`. AppFlow's loader is already agnostic (it takes a `const AppFlowContainerView&`), so the seam is one struct away from external.

**The unification (owner steer §4):** AppFlow's noun-agnostic data seam and the Recipe Registry are the **same pattern** — an *agnostic content boundary fed from an external, versioned, keyed store*. They are **two registries of one shape**. This doc treats them as one architecture with two instances.

**Why this is SAFE (and the honest ceiling).** This is **hot-reload of the FACADE, not of code internals.** The runtime reads content *through* the boundary each tick and **deep-copies at the loader** (verified: `FlowStateMachine`/`ActionStack`/`BindingStore` all copy, retaining no pointer into the view — §7.1), so the classic hot-reload obstacles (state-layout mismatch, pointer/vtable invalidation, torn mid-flight execution, live graph-rewiring) are **structurally avoided** (§7). A **shape-hash gate** (owner ruling, §6) freezes the interface/graph shape and lets only the *interior* (param values, state, calc bodies, shader-constant interiors) swap; any shape change is detected, messaged, and forced to a rebuild — safe by construction. The ceiling is **facade-total, code-internals-never** (§7.4) — going past it re-introduces the four obstacles.

**The one genuine obstacle (both tiers):** *logic* does not cross a data boundary. Three artifact classes carry transpiled/hand C++ code — AppFlow **handlers** (app-side C++ lambdas), the transpiled **callables** (`AppFlowCallables`), and the render-tier **shader/SIMD codegen** (`UberShaderSplice`, `RecipeSimd`). A callable/calc *body* still hot-reloads (interior); only a *new* handler / *new* signature / *new* graph edge needs a rebuild. Full code-internals reload would need a *compiled-plugin (.so/dlopen)* path — which **does not exist anywhere in the tree today** — but the recommended ceiling (§7.4) does not require it. This is why **dual-output is genuinely needed** (real disadvantages exist — §8), and the handler-path scope is the crux the owner must rule on (§11).

---

## 1. Inventory of VIXEN-consumed codegen artifacts

All artifacts are produced by the **Yeroket kernel-codegen** tool (`$KF/CodegenTool~/Program.cs` CLI + `$KF/SourceGenerator~/` Roslyn generator; emitters in `$KF/SourceGenerator~/Transpiler/`, where `$KF = /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`). Codegen runs as a **checked-in drift-guard, not a build-time regenerator**: normal builds compile the committed `.g.*` and only `--check` them; regen is a manual target. 59 generated artifacts exist in the tree; the table below is the *consumed-and-coupling* subset that drives the boundary design.

Legend — **Consumption**: `hdr` = compiled-in `constexpr`/inline header (recompile-coupled); `data` = already loaded from an external blob/file at runtime; `hdr+data` = dual-output already exists. **D/L**: DATA (POD/tables/enums) vs LOGIC (transpiled/hand C++ bodies).

| Artifact | Producer (emitter, flag) | Consumption today | Blast radius (TUs) | Tier | D/L |
|---|---|---|---|---|---|
| `AppFlow.g.h` (FlowStateId/ActionId enums, kTransitions, kElementTriggers, kKeyDefaults, kReturnEdges, kDataTargets, `AppFlowContainerView`) | `AppFlowEmitter.cs` `--appflow` | **hdr** — `AppFlowContainerView{}` hard-constructed in `AppFlowRuntime::Load` | **20** | facade | DATA |
| `ViewNounId.g.h` (agnostic noun key vocabulary, 36 nouns) | `ViewNounEmitter.cs` `--view-noun-enum` | **hdr** — enum used as `uint32_t` key across the seam | (incl. by AppFlow.g.h) | facade | DATA |
| `AppFlowCallables.generated.g.hpp` (+ hand shim `.g.hpp`) | `BuildCallableCppHeader` + C#→C++ transpiler, `--callable-cpp` | **hdr** — `#include`d by every `*.typed.g.h` | (via typed accessors) | facade | **LOGIC** |
| `*.typed.g.h` (UndertowBodies/Recipes/MapClaims/… typed View accessors) | `TypedAccessorEmitter.cs` `--typed-accessor-cpp` | **hdr** — typed getters over `ViewStore`; hard-dep on callables | ~1 each | facade | accessors (call LOGIC) |
| `Hud.blob.g.h` + `*.viewblob` (View reflection blob) | **`ViewBlobEmitter.cs` `--view-blob`** | **hdr+data** — header form compiled in; `.viewblob` parsed by `ViewBlobFile`; hosted by `BlobView` (built+tested, **not yet the live path**) | header: HUD TUs | facade | DATA |
| `RecipeContainer.g.h` (`SdfInstruction` 132-B mirror, `RecipeContainerHeader`, `ReadRecipeContainer` blob reader) | `RecipeContainerEmitter.cs` (source-gen) | **hdr (layout mirror + reader) feeding a data path** — recipe *content* is an external blob parsed by the reader | **13** | render | DATA (+ tiny reader logic) |
| `RecipeSimd.g.hpp` (closed-recipe SIMD4 CPU evaluator) | `RecipeSimdEmitter.cs` `--recipe-simd-cpp` | **hdr** — header-only SIMD eval | **7** | render | **LOGIC** |
| `OctreeConfig.g.h` (`[GpuStruct]` std430 config mirror) | `GpuStructCppEmitter.cs` `--cpp-header --struct-gpu-layout` | **hdr** — POD config struct, static_asserts | **6** | render | DATA (layout) |
| `UberShaderSplice.h`, `SdfRecipeCodegenGlsl.h`, `SpecializedRecipeShaderGlsl.h` (GLSL codegen) | *hand-authored* C++ in VIXEN (drive the runtime shader emit; consume the registry) | **hdr** — runtime GLSL generators reading `RecipeRegistry` | (SVO TUs) | render | **LOGIC** (runtime codegen) |
| RenderGraph `*Config.g.h` (Lighting/Shadow/Accumulation/… `[GpuStruct]`) | `GpuStructCppEmitter.cs` `--struct` | **hdr** — POD config mirrors | per-struct | render | DATA (layout) |
| Merged SDI shader-interface headers (`generated/sdi/merged/*-SDI.g.h`, 16) | separate SDI drift gate (`ShaderManagement`) | **hdr** — semantic shader wiring | shader TUs | render | DATA (wiring) |

**Schema sources** (the authored single-source that drives each): AppFlow ← `UndertowFlow.cs` / `AppFlowReference.cs`; callables ← `AppFlowCallables.cs` / `UndertowViewCallables.cs`; Views ← `Hud.cs` / `HudSections.cs` / `EditorLayers.cs` / `UndertowHud.cs`; OctreeConfig ← `OctreeConfig.cs` `[GpuStruct]`; Recipe/VoxelDocument ← Yeroket VM framework canonical types (gated on assembly `com.utility.sdf`).

**Reading of the table.** The recompile blast radius is concentrated on the facade tier: `AppFlow.g.h` at **20 TUs** is the single largest coupling point, and it is pure DATA — the highest-value, lowest-risk externalization target, and exactly the owner's tier-1 priority. Everything marked `hdr` and DATA is a candidate for the boundary with no logic hazard. Everything marked LOGIC is where the dual-output question actually bites.

---

## 2. What "already crosses" vs. "still recompiles" — the precise coupling

The audit's central question, answered per surface.

### 2.1 AppFlow (tier-1) — the loader is agnostic; the *view* is compiled in

`AppFlowLoader::Load(const AppFlowContainerView& view, fsm, stack, bindings, input, dataTargets*)` (`libraries/AppFlow/src/AppFlowLoader.cpp`) consumes the view as **opaque spans of POD structs** — `actions()`, `transitions()`, `elementTriggers()`, `keyDefaults()`, `returnEdges()`, `dataTargets()`. It already treats the artifact as runtime data. The `test_appflow_data_seam.cpp` proof documents the seam as **NOUN-AGNOSTIC**: the same `DispatchData`/`ReadData` path services an undertow HUD noun or an editor noun through an `IViewDataProvider`, keyed by the agnostic `ViewNounId` — the mechanics model no specific noun.

**But** `AppFlowContainerView` (`libraries/AppFlow/include/generated/AppFlow.g.h`) is `Generated::AppFlowContainerView`: every accessor returns an `inline constexpr` C++ array (`kActionDecls`, `kTransitions`, …). `AppFlowRuntime::Load` hard-constructs `AppFlowContainerView{}`. So **the loader is ready for external data, but the data is welded in at compile time.** Any schema change → manual `appflow_regen` → the 20 TUs that include `AppFlow.g.h` recompile.

**Answer to the brief's critical tier-1 question:** `AppFlowContainerView` / `dataTargets()` are **COMPILED-IN generated code today** (recompile coupling remains despite the agnostic loader). *That struct is the seam to externalize.* And the template for doing so already exists one library over (§4.1).

### 2.2 View reflection blob (tier-1) — dual-output already exists, but is not the live path

`ViewBlobEmitter` emits both `Hud.blob.g.h` (compiled-in `constexpr ViewBlob`) **and** `hud.viewblob` (a `# viewblob v1` text file carrying `model`, a `version 0x…` schema hash, and field descriptors). `ViewBlobFile::Parse/Load` reads the file at runtime into the *same* `ViewBlob` struct (stable backing storage, all failures → `nullopt`/logged, never throws). `BlobView : IView` walks the blob to build RmlUi's dynamic data model, guarded by the version hash. **This is exactly the owner's dual-output content boundary — already designed, built, and tested** (View-Contract Inc-2b).

The gap: the *running app* still uses the native compiled `HudView` fast-path; `BlobView`/`ViewBlobFile::Load` are proven in tests but **not wired as the live runtime path**, and nothing file-watches the `.viewblob` to re-`Register`. So the *mechanism* is done; the *hot-reload wiring* is not connected.

### 2.3 The logic carriers (both tiers) — what genuinely can't be data

- **AppFlow handlers.** `EditorApplication` registers one C++ lambda per action (`rt_.RegisterHandler(FlowActionId::ToggleLayer, [this]{…})`, `…Undo/Redo/Save/Return`). Per the Kernel-Glue-Transplant reframe (D14/D15), the handler *behavior* is "the only hand-written per-consumer piece." It is compiled-in logic — the true blocker to *fully* data-driven facade hot-reload.
- **Callables** (`AppFlowCallables.generated.g.hpp`) — transpiled C++ from `[KernelCallable]` C#; every `*.typed.g.h` `#include`s it. Logic, compiled in.
- **Render-tier shader/SIMD codegen** (`UberShaderSplice.h`, `SdfRecipeCodegenGlsl.h`, `RecipeSimd.g.hpp`) — C++ that *generates* GLSL / evaluates SIMD. Note the important subtlety: `UberShaderSplice` runs **at runtime from the `RecipeRegistry`**, so changing *which* recipes are spliced (content) already hot-reloads and re-compiles the GLSL via glslang at runtime; only changing the *codegen logic itself* recompiles the renderer.

### 2.4 Recipe Registry (tier-2) — already a content boundary

Confirmed end-to-end in `libraries/SVO/include/Recipe/`:
- **Versioned blob reader:** `RecipeContainer.g.h` — `RecipeContainerHeader{magic, formatVersion, …}`, `ReadRecipeContainer(blob, len, view)` rejects wrong magic/version/length.
- **Runtime ingest:** `RecipeIngest::IngestBlob(recipeId, blob, len, reg)` → `ReadRecipeContainer` + `RecipeRegistry::Register`.
- **Boot pack:** `RecipeBootIngest::ParseAndBakeRecipeBlobBuffer(buf, len)` walks a packed buffer `[count][ (recipeId, len, bytes) … ]` (from `HostAbi.PackRecipeBlobs`), ingests, bakes. Fail-loud.
- **Manifest-of-files pack:** `RecipePackLoader::LoadRecipePack(manifestPath, reg, err)` over `RecipeManifest{namespacedId, recipeId, blobPath}`.
- **Registry:** `RecipeRegistry` keyed by stable `recipeId`, holding bytecode + bound metadata; `UberShaderSplice` reads it at runtime to emit GLSL.

**So recipe DATA already crosses as content; the registry IS the boundary.** What still forces a VIXEN recompile on a recipe/shader change: (a) the compiled-in `SdfInstruction` 132-B **layout mirror** + reader (change the instruction layout → recompile the 13 TUs); (b) the **GLSL splice / SIMD codegen logic** (`UberShaderSplice.h`, `RecipeSimd.g.hpp`) — but *only if the codegen logic changes*, not on a recipe-content change; (c) the `SdfOpCode` vocabulary enum. **A recipe-content or per-recipe-param change is already a registry/pack reload today, not a header edit.**

---

## 3. The unified boundary — one pattern, two registries

The owner's insight (steer §4) is the organizing principle of this doc:

> AppFlow's noun-agnostic data seam (facade tier) and the Recipe Registry (render tier) are the **same pattern** — an agnostic content boundary — just two registries.

The shared shape:

```
  AUTHORED SCHEMA (C#)                 [Yeroket kernel-codegen]
        │  emits  ─────────────────────────────┬──────────────────────────
        ▼                                       ▼
  COMPILED-IN form (constexpr .g.h)     EXTERNAL CONTENT form (versioned blob/file)
        │  (finalized builds)                   │  (iteration / hot-reload)
        └──────────────┬────────────────────────┘
                       ▼
              A VERSIONED READER  (magic + formatVersion / schema-hash guard)
                       ▼
              A REGISTRY / VIEW   (keyed by a STABLE agnostic id)
                       ▼
        GENERIC RUNTIME  (loader/host is noun-blind; knows only the id vocabulary)
```

Every instance is `(stable-id vocabulary) + (versioned blob) + (reader) + (registry) + (noun/id-blind runtime)`:

| Element | Facade instance (AppFlow + View) | Render instance (Recipe) |
|---|---|---|
| Stable-id vocabulary | `ViewNounId` (nouns), `FlowActionId`/`FlowStateId` (verbs/states) | `recipeId` (U32), `SdfOpCode` |
| Versioned blob | `.viewblob` (`# viewblob v1` + schema hash); **`.appflow` — TO ADD** | recipe container blob (`'VRC1'` + formatVersion) |
| Reader | `ViewBlobFile::Parse`; **AppFlow reader — TO ADD** | `ReadRecipeContainer` |
| Registry / view | `BlobView` + `ViewStore`; runtime AppFlow primitives | `RecipeRegistry` + `RecipeManifest` |
| Id-blind runtime | `AppFlowLoader`/`AppFlowRuntime` (noun-agnostic) | `RecipeIngest`/`UberShaderSplice` (recipeId-driven) |

The convergence is not aspirational — it is two-thirds already there. The facade tier needs the AppFlow reader/blob added (copying the View precedent); the render tier needs only its recompile-forcing *layout/codegen* pieces addressed (§5.2), because its content already flows.

---

## 4. Boundary design per artifact class (with the dual-output answer)

For each class: the stable contract, how it stays agnostic, how hot-reload concretely works, and **keep-both-paths vs collapse**.

### 4.1 AppFlow data (`AppFlow.g.h` / `AppFlowContainerView`) — HIGHEST VALUE

- **Contract:** an `.appflow` versioned blob mirroring the `.viewblob` shape — a header (`magic 'AFL1'` + `formatVersion`/schema-hash) followed by the six tables (`actions`, `transitions`, `elementTriggers`, `keyDefaults`, `returnEdges`, `dataTargets`). Enum values (`FlowActionId`, `FlowStateId`, `ViewNounId`) are **pinned + append-only** (they already are) so the blob's `uint16_t`/`uint32_t` ids are stable across a reload — this is the load-bearing invariant.
- **Agnostic how:** identical to today — the loader consumes spans of POD; `ViewNounId` keeps the data leg noun-blind. Nothing in the boundary models a specific action or noun.
- **Hot-reload mechanism:** add `AppFlowContainerView`-from-blob construction (an `AppFlowBlobFile` parser + a runtime `AppFlowContainerView` backed by parsed storage, exactly like `ViewBlobFile` backs `ViewBlob`); make `AppFlowRuntime::Load` accept an optional external view; **file-watch the `.appflow`** → on change, re-run `AppFlowLoader::Load` into fresh primitives and swap. Because `Load` already re-populates fsm/stack/bindings/input from scratch, a reload is a **re-`Load` + handler re-attach**, not a restart.
- **Dual-output: KEEP BOTH.** The `constexpr` header stays for finalized builds (zero-alloc, zero-parse, no file I/O); the blob is the iteration path. This is precisely the `--view-blob` precedent (header + datafile from one emitter, one version hash). *Add an `EmitDataFile` face to `AppFlowEmitter`* — a direct copy of `ViewBlobEmitter`'s dual-face pattern.

### 4.2 View reflection blob (`Hud.blob.g.h` / `.viewblob`) — MECHANISM DONE, WIRING MISSING

- **Contract / agnostic / dual-output:** already exists and shipped (§2.2). Keep both — this is the reference implementation of the whole pattern.
- **Hot-reload mechanism (the only gap):** wire `BlobView` + `ViewBlobFile::Load` as a selectable *live* runtime path behind a flag, and add a file-watch on `.viewblob` → re-`Parse` → re-`Register` (the version hash guard already makes a stale/mismatched blob a logged skip, never garbage). No new contract; only wiring.

### 4.3 Typed View accessors (`*.typed.g.h`) + callables (`AppFlowCallables`) — LOGIC

- **Nature:** typed C++ getters that call transpiled callables. This is a *compile-time typed convenience over `ViewStore`* — the same data the blob path reaches generically at runtime. The `BlobView`/`ViewStore` path already provides an *untyped-but-generic* runtime alternative that needs no compiled accessor.
- **Dual-output answer:** **keep the typed header for compiled builds; for hot-reload, route through the generic `BlobView`/`ViewStore` path** (which does not need the typed accessors or the callables at all). The callables that are *pure projections* (Identity*, StrengthBandToByte) are data-describable (a projection kind + params) and could later be expressed as blob metadata; the ones that aren't (the RmlUi `IdentityString` hand shim) prove some callables genuinely need C++ — a **logic carrier**, see §7/§9. **Do not** try to externalize the callables as data in tier-1; use the generic path instead.

### 4.4 Recipe registry (`RecipeContainer.g.h` + ingest/pack/registry) — REUSE AS-IS (owner steer)

- **Contract:** the existing versioned recipe blob + `RecipeManifest` + `RecipeRegistry`. **Do not design a new mechanism.** Promote the registry to *the* render-tier content boundary: recipes + their compute payloads flow through `RecipePackLoader`/`RecipeBootIngest` as loadable content, so a recipe change is a **pack/registry reload**, not a header edit + recompile.
- **Agnostic how:** the registry is already domain-blind (keyed by `recipeId`, holds SDF bytecode + bound metadata, no game nouns).
- **Hot-reload mechanism:** file-watch a recipe manifest / blob dir → `RecipePackLoader::LoadRecipePack` into a fresh registry → re-run the runtime shader splice (`SpliceProceduralRecipesIntoSource` already reads the registry and re-emits GLSL, re-compiled by glslang at runtime). This path **already exists**; hot-reload is a watch + re-load + re-splice, no header edit.
- **Shader payloads specifically:** `UberShaderSplice` bakes per-recipe params as GLSL literals and re-compiles at runtime *from the registry* — so a **recipe/param change already hot-reloads the shader** without a C++ recompile. To let a **shader change itself** cross as content (owner steer §2), the target is a **GLSL/SPIR-V blob keyed by `recipeId` carried through the registry** (a `RecipeEntry` gains an optional precompiled-shader-blob slot, or the manifest references a `.glsl`/`.spv` per recipe) so the splice can *load* a payload instead of *only* generating it. That closes the "shader change = renderer recompile" gap for the codegen-*logic* case.
- **Dual-output: KEEP BOTH.** Registry-loaded content for iteration; compiled-in (the current static registration + the `constexpr`-style layout mirror) for finalized builds. See §6 for the render-tier disadvantage assessment.

### 4.5 `[GpuStruct]` config mirrors (`OctreeConfig.g.h`, RenderGraph `*Config.g.h`) — LAYOUT, LOW PRIORITY

- **Nature:** std430 layout mirrors with `static_assert`s — they *are* the compile-time ABI between C++ and the shader. Externalizing a memory layout as runtime data buys little (the shader is compiled against the same layout) and risks silent ABI drift.
- **Dual-output answer:** **collapse to compiled-in.** These are the one class where the boundary has no meaningful payoff and a real hazard. Leave them as `constexpr` mirrors; they are not an iteration bottleneck.

---

## 5. First-slice plans (exact files)

### 5.1 TIER-1 (owner priority): hot-load a facade change with no renderer recompile

**The smallest end-to-end slice.** Prove that editing the AppFlow schema (e.g. add a key-default or an element trigger) reaches the running editor **without recompiling** — by copying the *already-proven* View blob pattern onto AppFlow.

**Boundary format:** `.appflow` text/binary blob, header `AFL1` + schema-hash version, six tables — modeled 1:1 on `.viewblob` (`# viewblob v1`).

**Exact artifacts / files (plan only — do NOT implement):**
1. Producer (Yeroket, read-only in this audit): add an `EmitDataFile` face to `$KF/SourceGenerator~/Transpiler/AppFlowEmitter.cs`, mirroring `ViewBlobEmitter.EmitHeader`/`EmitDataFile`; feed both from one `AppFlowVersionHash` (mirror `ViewVersionHash.cs`). New `--out-datafile` on the `--appflow` CLI block in `Program.cs`.
2. Consumer parser: `libraries/AppFlow/include/AppFlowBlobFile.h` + `src/AppFlowBlobFile.cpp` — the exact analogue of `libraries/RenderGraph/include/Ui/ViewBlobFile.h` (owns stable backing storage; `Parse`/`Load`; all failures → `nullopt`/logged; never throws). It fills a runtime-backed `AppFlowContainerView`.
3. Runtime entry: extend `AppFlowRuntime::Load` (`libraries/AppFlow/src/AppFlowRuntime.cpp:29`) to accept an optional `const AppFlowContainerView*` (default = the compiled-in `AppFlowContainerView{}`), so both paths share one loader.
4. **Shape-hash gate in the loader (safe by construction — §6):** the `.appflow` blob carries an `AppFlowShapeHash` stamped by the producer over the *frozen surface only* (enum sets + pinned values, table schemas, transition/trigger/Data-target **edges**, param signatures — NOT key values / patterns / defaults). On hot-load, `AppFlowBlobFile` compares the incoming shape-hash to the running one: **match → accept the swap; differ → reject, emit "this facade change alters the interface/graph → rebuild required", and fall back to a full rebuild.** This makes the very first hot-reload demo safe — an interior edit (retarget a key, tweak a default) hot-loads; a shape edit (new action/state/edge) is caught and messaged, never silently mis-applied.
5. Hot-reload trigger: a file-watch on the `.appflow` in the editor's frame tick (`application/editor/source/EditorApplication.cpp`, near `rt_.Load()` at :176) → on change, run the shape-hash gate (step 4), and *only on match* re-`Load` from the re-parsed blob into fresh primitives at the top of the tick (the verified between-tick swap point, §7.2), then **re-run the handler registration block** (`:203–263`). No retained pointers into the view (verified §7.1), so the swap is clean.
6. Commit the `.appflow` next to the schema, and add `appflow_blob_check/regen` targets in `codegen/CMakeLists.txt` mirroring `view_hud_blob_check/regen`.

**What hot-reloads in this slice:** states, transitions, element→action triggers, key chords/defaults, return edges, Data→noun targets — i.e. **the entire declared facade graph**. What does *not* (this slice): a *new action's handler behavior* (needs the handler, which is C++ — §9). A schema change that only re-wires *existing* actions to different triggers/keys/nouns hot-reloads fully with zero recompile.

**Why this is low-risk:** it is a mechanical transcription of a pattern already shipped and tested for Views (`ViewBlobEmitter`/`ViewBlobFile`/`BlobView`), onto a loader that is *already* agnostic and already re-populates from scratch on `Load`.

**A companion micro-slice (even smaller, no new emitter):** wire the *existing* `BlobView`/`ViewBlobFile::Load` as the live HUD path behind a flag + file-watch the existing `hud.viewblob` (§4.2). This hot-reloads HUD *view shape* today with **only wiring** — no producer change — and de-risks the AppFlow slice by proving the watch/re-register loop first.

### 5.2 TIER-2 (render): recipe/shader change as a registry reload

**Smallest slice:** file-watch a recipe manifest dir → `RecipePackLoader::LoadRecipePack` into a fresh `RecipeRegistry` → re-run `SpliceProceduralRecipesIntoSource` + re-compile the uber-shader at runtime. Recipe content + per-recipe params hot-reload with no C++ recompile (the path already exists; the slice is the watch + swap). **Shader-payload-as-content** (a GLSL/SPIR-V blob keyed by `recipeId` in the registry/manifest) is the follow-on that lets a *shader-logic* change cross the boundary too.

---

## 6. The safety contract — frozen shape vs fluid interior, enforced by a shape-hash gate (OWNER RULING)

Hot-reload eligibility is **not hand-policed** — it is a *checkable, derived* contract. The principle: **the interface/graph SHAPE is frozen; only the INTERIOR is fluid.** A hot-load is accepted only when it changes the interior and leaves the shape identical; any shape change is detected and forced to a rebuild.

**Eligible for hot-reload (the fluid interior):**
- param **values**, initial/default **state**;
- the **body of a calculation** — the arithmetic/logic *inside* a function or system — *provided it reads the same inputs and writes the same outputs*.

**Ineligible (the frozen shape — requires a rebuild):**
- declarations: new/removed/retyped params, fields, systems, states, actions, nouns;
- a body's **signature** (the inputs it consumes / outputs it produces);
- new control-flow edges *out of* a body: new dispatch targets, new dependencies, newly emitted rows/effects;
- anything that changes what the renderer linked against or how the graph is wired.

**Enforcement — the shape-hash gate.** Codegen stamps a **shape/version hash over the frozen surface only** — struct/layout mirrors, body signatures, the dispatch/dependency graph, and the declaration set — and **deliberately EXCLUDES** the fluid interior (body statements, param values, default state). On hot-load the loader compares the incoming artifact's shape-hash to the **running** one:
- **match → accept the swap** (body/param/state adopted live);
- **differ → REJECT the hot-load**, emit a clear message naming what changed ("this change alters the interface / adds a path out of the body / retypes a param → **rebuild required**"), and **fall back to a full rebuild**. No silent wrong-state, no best-effort live migration in this pass.

This **generalizes the `ViewVersionHash` precedent** the inventory already found: `ViewBlobEmitter` emits a header face and a datafile face carrying the *same* `ViewVersionHash`, and `BlobView::Register` already rejects a version mismatch as a logged skip. The ruling makes that the *general* mechanism and pins exactly what the hash must and must not cover: an **interior-only edit keeps the hash stable** (so it hot-loads); a **shape edit changes it** (so it is caught). The design requirement is that the hash is computed over the frozen surface's canonical serialization and *never* folds in a body statement or a literal value.

### 6.1 Per-class frozen-shape surface vs fluid interior

| Artifact class | Frozen SHAPE (in the hash) | Fluid INTERIOR (excluded) | Clean separation? |
|---|---|---|---|
| **AppFlow data** (`AppFlow.g.h`) | the enum sets (`FlowStateId`/`FlowActionId`/`ViewNounId` members + pinned values), the table *schemas*, the transition graph's **edges** (from/to/guard), each Data action's **noun target**, param **signatures** | key-chord **values**, default-state values, an element-trigger's **pattern string**, a transition's **effect-ref string**, param **default values** | **Yes, mostly.** Clean for values. **Caveat:** a *new transition edge* or a *new element→action trigger* is a new graph edge OUT of the flow → SHAPE (rebuild). Re-pointing an *existing* key/trigger to an existing action is interior. |
| **Callables** (`AppFlowCallables`) | each callable's **signature** (name, arg types, return type) — the fixed contract the typed accessors call | the callable **body** (e.g. `applyToggle`'s `mask ^ (1u<<index)` arithmetic) | **Yes — this is the textbook case.** Same signature, different arithmetic = interior. A new callable or a retyped arg = shape. |
| **Views** (`Hud.blob.g.h`/`.viewblob`) | model name, ordered field **descriptors** (name + kind), array element field set — *exactly what `ViewVersionHash` already hashes* | the field **data/values** pushed each frame (never in the blob's version anyway) | **Yes — already implemented.** `ViewVersionHash` hashes shape only ("Data/values are not hashed — only the shape"). Directly reusable. |
| **Recipe/SVO** (`RecipeContainer.g.h` + registry) | the `SdfInstruction` **layout** (132-B mirror), the `SdfOpCode` **vocabulary**, a recipe's **opcode sequence + structure** (the DAG shape) | per-recipe **param values**, bound metadata (bound sphere, relaxation), occupancy-grid **values** | **Partial.** Param/metadata values are cleanly interior (already hot-reload via the registry). But a recipe's *opcode sequence* is arguably shape (it changes the GLSL the splice emits + which functions exist) — a recipe that only tweaks *param literals* is interior; one that adds/removes an SDF op is shape. The container's `formatVersion` already guards the layout half. |
| **Shader codegen** (`UberShaderSplice.h`, `SdfRecipeCodegenGlsl.h`) | the *set of generated function signatures* (`evalRecipeField`/`getRecipeBoundSphere`/… ) + the switch's **case set** (one per registered `recipeId`) | the per-case **literals** (baked bounds/relaxation/params) | **Partial.** Adding a recipe adds a switch case = shape edit *of the generated shader* (but it re-compiles at runtime anyway); changing a baked literal is interior. See §4.4 for shader-payload-as-content. |
| **`[GpuStruct]` config** (`OctreeConfig.g.h`, `*Config.g.h`) | the **entire** struct — field set, types, std430 offsets, size (that IS the C++/shader ABI) | *nothing* — the value is runtime data already, not in the mirror | **N/A — ALL SHAPE.** These are **never hot-reloadable**: the mirror is pure interface. The shape-hash would cover 100% of them; any change is a rebuild. Correctly collapsed to compiled-in (§4.5). |

**Reading:** the hash separates shape from interior *cleanly* for callables and views (and param-values everywhere), *mostly* for AppFlow (edges are the boundary case), *partially* for recipe/shader (opcode-sequence vs param-literal), and *not at all* for `[GpuStruct]` (all shape → never hot-reloadable — an honest "this class can't play"). Crucially, the common iteration edits (tune a value, change a calc body) land on the **interior** side for every class that hot-reloads at all.

### 6.2 The shape-hash specification

- **What it hashes (frozen surface):** a canonical, declared-order serialization of — the declaration set (enum members + pinned values, field/param names + types), every body **signature** (name + input types + output types), the **dispatch/dependency graph** (edges: transitions, element/key triggers, Data→noun targets, recipe switch-case set / dependencies), and struct **layouts** (offsets/sizes for `[GpuStruct]`).
- **What it must NOT hash (fluid interior):** body statements/arithmetic, param values, default/initial state, baked literals, string values that are data (key-chord constants, effect-refs, element patterns *when re-pointing to an existing action*), and the frame data pushed through a View.
- **Where stamped:** the producer (Yeroket emitter) computes it once in C# — mirroring `ViewVersionHash.Compute` — and stamps it into **both** faces (compiled header + external blob) so header, datafile, and (if present) the C# consumer value are one source and cannot disagree.
- **Where checked:** the loader, on every hot-load, before adopting the swap. Match → accept; differ → reject + message + rebuild (§6, above).
- **Reuse vs new:** **Views** reuse `ViewVersionHash` as-is. **AppFlow** needs a new `AppFlowShapeHash` (same algorithm — FNV-1a over the canonical frozen-surface serialization — new input set covering the flow graph). **Recipe** already has `formatVersion` for the container layout; a *recipe-shape* hash (opcode-sequence structure, excluding param literals) is the new piece if opcode-sequence edits are to be gated distinctly from param edits. **Callables** need a signature-set hash (new, small). **`[GpuStruct]`** needs no hot-reload hash (never hot-reloadable); its `static_assert`s already are the compile-time shape guard.

### 6.3 Honest limitation of the interior/shape split

Interior-only reload makes the **common** iteration instant — tune a param, change a calc body, adjust default state — while **any interface change still costs a rebuild**. Rough inference of how often facade/UI iteration is interior vs shape (from the live schemas + the retire history in the prior designs):

- **Interior-heavy (hot-reloadable), likely the majority of *tuning* iteration:** adjusting a key default (`Ctrl+Z` → some other chord), re-pointing an element trigger to an existing action, changing a callable body (`applyToggle`/`StrengthBandToByte` arithmetic), tuning param defaults, changing a View field's *data*. These are the "dial it in" edits that dominate polish.
- **Shape-changing (rebuild), likely the majority of *feature* iteration:** adding a new `FlowAction`/`FlowState` (the AppFlow enums grew ToggleLayer→…→Data over the increments — each was a shape edit), adding a View field (the `ViewNounId` catalogue grew to 36 nouns — each a shape edit), adding a Data→noun target, adding a transition edge. The View-Contract/AppFlow increment history is *dominated* by shape edits because it was **building the vocabulary**; once a vocabulary stabilizes, iteration shifts toward interior tuning.

**Net:** hot-reload's payoff is largest **after** a surface's vocabulary stabilizes (tuning phase), and smaller **while** a surface is still growing its declaration set (feature phase). This is a genuine limitation, not a flaw — and it is *why the shape-hash gate matters*: it makes the "you must rebuild now" moment explicit and safe instead of a silent corruption. It also argues for sequencing hot-reload onto **stable** facade surfaces first.

---

## 7. Why this is safe: FACADE-content reload, not code-internals reload (THESIS VALIDATION — OWNER)

The owner is right to be skeptical of *total* hot-reload. Arbitrary code-internals reload re-introduces four classic obstacles: (1) live-state layout mismatch, (2) native function-pointer/vtable invalidation, (3) mid-flight/torn execution, (4) graph/dependency rewiring. **This framework does not design toward any of that.** The thesis to validate:

> Because the framework reloads **facade CONTENT that crosses a data boundary the runtime already re-reads** (recipe container blob, `AppFlowContainerView`, `ViewBlob`) — *not* code internals — the four obstacles are **structurally avoided**.

**Verdict: the thesis HOLDS against the actual architecture**, with one small indirection caveat (§7.1, `[GpuStruct]`, which is correctly excluded from reload anyway). The achievable feature is **"hot-reload of the FACADE"** (params, state, calc/system bodies, views, flows, shader interiors) — **not** "hot-reload of arbitrary code." Each obstacle, mapped to why it can't fire:

| Classic obstacle | Why it is structurally avoided here |
|---|---|
| (1) Live-state layout mismatch | The **shape-hash gate (§6) freezes layout**; a reload with a different shape is rejected, so live instances keep their shape by construction. |
| (2) Function-pointer / vtable invalidation | The renderer/kernel holds **no pointer INTO the content** — it reads THROUGH the registry/loader (verified §7.1). Nothing to invalidate because nothing points in. |
| (3) Mid-flight / torn execution | Content is re-ingested **AT the boundary**, at a natural between-tick swap point (§7.2) — never mid-body. |
| (4) Graph / dependency rewiring | A new edge OUT of a body **changes the shape → the shape-hash REJECTS → rebuild** (§6). The fluid interior cannot rewire the graph. |

### 7.1 Per-class: does the runtime access content THROUGH the boundary (no retained raw pointers)?

Verified against the code — the load-bearing property for obstacle (2):

| Facade class | Access pattern | Retained pointer into content? |
|---|---|---|
| **AppFlow** | `AppFlowLoader::Load` ingests, and every primitive **deep-copies**: `FlowStateMachine::LoadTransitions` → `transitions_.assign(table, table+count)`; `ActionStack::LoadActions` → `actions_.assign(...)`; `BindingStore::RegisterActions` → `std::vector<FlowParamSchema>(decl.params, …)`; `BindingStore::AddElementTrigger` → `const std::string pat = trig.elementPattern` (copies the `const char*` into `std::string prefix/suffix/paramName`). | **NO.** After `Load` returns, nothing holds a pointer into the `AppFlowContainerView`; the view can be a transient. This is exactly what makes a blob-backed transient view safe. *(Minor: `FlowParamSchema.name` is copied by pointer-value inside the copied vector; it is only read during Load/AddBinding validation, not retained live — worth a note, not a hazard.)* |
| **View (`BlobView`/`ViewStore`)** | `ViewStore` holds **typed slots it owns** (real `int`/`Rml::String`/`std::vector<RowStore>`); `BlobView` binds RmlUi definitions to `store_.SlotPtr(...)`, which point into `store_`, **owned by `BlobView`**, not into the blob. The blob is walked once at `Register`. | **NO** into the blob. (RmlUi holds pointers into `ViewStore`, which `BlobView` owns and outlives the model — the same contract the native `HudView` honors.) |
| **Recipe registry** | `RecipeIngest::IngestBlob` copies bytecode into `RecipeEntry.bytecode` (`entry.bytecode.assign(view.instructions, …)`); the registry owns the entries; `UberShaderSplice` reads the **registry**, not the blob. | **NO** into the blob. The renderer reads the registry each splice. |
| **`[GpuStruct]` config** | The mirror **is** a compiled type; call sites use the struct type directly (compile-time). | **N/A — this class is compiled interface, never hot-reloaded (§4.5/§6.1).** It is the one class where "read through a boundary" does not apply — correctly excluded. |

**Flag:** no facade class that is *slated for reload* retains a raw pointer into generated content — the loaders copy at the boundary. The only class holding a direct type dependency (`[GpuStruct]`) is the all-shape class already excluded from reload. **The thesis's obstacle-(2) claim is verified true for every reloadable facade class.**

### 7.2 The natural swap point per tier

| Tier | Swap point | Tears an in-flight tick/frame? |
|---|---|---|
| **AppFlow (facade)** | **Between-tick** — `AppFlowLoader::Load` fully re-populates fresh primitives; the swap happens at the top of the editor/host frame tick (near `EditorApplication.cpp:176`), before any dispatch that frame. | **No** — dispatch reads the primitives; re-Load before dispatch is atomic w.r.t. the tick. Re-attach handlers in the same step. |
| **View (`BlobView`)** | **Between-frame** — re-`Parse` the `.viewblob` + re-`Register` at frame boundary; the version-hash guard skips a mismatched blob (empty view, logged), never garbage mid-frame. | **No** — RmlUi model rebuild is a frame-boundary operation. |
| **Recipe (render)** | **Between-frame, on-swap** — `LoadRecipePack` into a *fresh* registry, then re-run the splice + runtime shader re-compile; swap the compiled pipeline at the frame boundary. | **No tearing of C++**, but the runtime shader re-compile is a visible hitch (§8.6). |

Every tier has a clean between-tick/between-frame swap point where the boundary re-read already happens; a facade reload rides that existing seam. **No mid-body / torn-execution path exists** because the content is never read from inside a running body — it is read at the loader/registry each cycle.

### 7.3 The payoff number — interior (reloadable) vs shape (rebuild)

The decision number: what fraction of typical facade/UI iteration lands on the **reloadable interior** (param/state/body/shader-interior, shape-hash stable) vs is inherently a **shape change** (rebuild)?

- **Reloadable interior — the *tuning* loop.** Param values, default/initial state, a callable/calc **body** (same signature), a key-chord value, re-pointing an existing trigger to an existing action, per-recipe param literals, baked shader constants, View field *data*. This is the dial-it-in, polish, and balance work.
- **Inherent shape change — the *feature/vocabulary* loop.** A new `FlowState`/`FlowAction`, a new View field/noun, a new Data→noun target, a new transition edge, a new callable, a retyped param, a new SDF op in a recipe, a `[GpuStruct]` field change.

**Estimate (inferred from the live schemas + the increment history, which is explicit provenance):** during **vocabulary-building** phases (what the AppFlow/View-Contract increments *were* — the enums grew ToggleLayer→Data, `ViewNounId` grew to 36 nouns, each a shape edit) iteration is **shape-dominated (~70–80% shape)**, so hot-reload helps little. During **tuning/polish** phases on a *stabilized* surface, iteration inverts to **interior-dominated (~70–90% interior)** — chord tweaks, calc-body tweaks, param/threshold tuning, shader-constant tuning, all shape-hash-stable. **Net practical read: on a stabilized facade surface, the large majority of iteration is interior and hot-reloads instantly; the payoff scales with surface maturity.** The number that matters for the owner: *once a facade surface stops growing its declaration set, most of its remaining iteration is interior* — which is precisely the long tail of a shipping product's UI/feel work. That is where facade hot-reload earns its cost.

**Inherently all-shape (never reloadable) facade artifacts:** `[GpuStruct]` config mirrors (§4.5, pure ABI); the `ViewNounId` / `FlowActionId` / `FlowStateId` **enum declaration tables themselves** (adding a member is by definition a shape edit); the merged SDI shader-interface headers (semantic wiring = graph shape). These are declaration/interface artifacts — correctly outside the reload envelope.

### 7.4 The honest scope ceiling

This framework's ceiling is **total coverage of the boundary-crossing facade** — every artifact that already crosses the data boundary the runtime re-reads — **not arbitrary-code reload.** That ceiling is the *right* one: going past it (reloading code internals, live-migrating state, rewiring the graph live) re-introduces obstacles (1)–(4) the boundary+shape-hash design exists to avoid. The framework buys the high-frequency iteration (params, state, bodies, views, flows, shader interiors) at the price of "a shape change costs a rebuild" — and makes that price **explicit and safe** via the shape-hash gate rather than paying it as silent corruption. **Facade-total, code-internals-never.**

---

## 8. Honest disadvantages of hot-reload — "is there really no disadvantage?"

The owner asked whether hot-reload has *no* meaningful disadvantage (which would justify collapsing to a single boundary path). **It has real disadvantages. Dual-output IS needed.** The honest list:

**Facade tier (data — mild disadvantages, boundary still worth it):**
1. **Runtime cost & allocation.** The `constexpr` header is zero-parse, zero-alloc, cache-tight. The blob path parses a file, allocates backing storage, and builds tables at load — negligible for AppFlow's small tables, but non-zero, and a reason to keep the compiled path for shipping builds.
2. **Enum-stability coupling.** The blob carries `uint16_t`/`uint32_t` ids; a reload against a binary compiled with a *different* enum ordering is silent corruption unless the version-hash guard catches it. The guard exists for Views; AppFlow must adopt the same hash discipline or the reload is unsafe. **This is a hard prerequisite, not a nicety.**
3. **Handlers don't reload.** The declared graph reloads; the *behavior* (handlers) is compiled in. A schema that adds an action whose handler doesn't exist yet resolves to `RejectedByState` (caught, logged) — safe, but the new action is inert until the app is rebuilt. So facade hot-reload is *partial by construction* until the logic path (§9) exists.
4. **Debuggability & provenance.** A crash/misbehavior now depends on *which blob was loaded*, not just the binary — the version hash must be logged, and "what content is live" becomes part of every bug report.
5. **Two paths to keep green.** Dual-output means the header and the blob must stay behaviorally identical (the View program pays for this with a 4-way equivalence hash test); that test burden is real.

**Render tier (bigger disadvantages):**
6. **Runtime shader recompilation.** The recipe path re-emits GLSL and re-compiles via glslang at runtime on every reload — a visible hitch, and a dependency on the shader compiler being present in the shipping build (undesirable for a finalized product).
7. **SIMD/layout logic can't be data.** `RecipeSimd.g.hpp` and the `SdfInstruction` layout mirror are compiled artifacts; a change to *those* is a recompile regardless of the registry. The registry boundary hot-reloads *content*, not the *evaluator*.
8. **ABI drift risk on `[GpuStruct]`.** Externalizing a std430 layout invites a data/shader mismatch the current `static_assert` catches at compile time (§4.5) — a strong reason to *not* put config layouts on the boundary.

**Conclusion:** dual-output is warranted on the facade and render tiers (iteration blob + finalized compiled-in). The one class to **collapse to compiled-in** is `[GpuStruct]` config layout (§4.5). There is no class where the *hot-reload path* dominates so completely that the compiled path should be dropped.

---

## 9. Obstacles, risks, and what genuinely can't be pure data

1. **Handlers / behavior = the core obstacle.** Per Kernel-Glue-Transplant D14/D15, the handler is "the only hand-written per-consumer piece." Two candidate resolutions, both DESIGNED-not-built:
   - **(a) Transplanted-logic .so (the D12 path).** The reframe already commits to the kernel transplanting *logic* (C# bodies → C++). A hot-reloadable facade means compiling the transplanted handler/dispatch logic into a **swappable shared library** and `dlopen`-ing it. **This infrastructure does not exist anywhere in the tree** (no `dlopen`/`LoadLibrary` — verified). `libraries/KernelDispatch/include/KernelDispatch/Abi.h` is a *static, in-process* domain-blind dispatcher contract (SlotRef reads/writes + `std::function` payloads) — the natural *shape* for a plugin ABI, but not a dynamic loader. Building a `.so`/dlopen plugin path is **net-new work and the single biggest risk** (ABI stability, symbol versioning, lifetime across the boundary, `noexcept` translation at the C-ABI edge per the existing C#↔C++ UB rule).
   - **(b) Keep the hot-reloadable surface declarative.** The `Data`-action → View-noun path (D16) needs *no handler code* — a `Data` verb reads/writes a declared noun through the `IViewDataProvider`. Every facade interaction expressible as "mutate a declared noun" hot-reloads as pure data with the tier-1 slice alone. Handlers that run arbitrary side-effects stay compiled. This bounds hot-reload to the declarative surface but needs **zero new infrastructure**.
2. **Version/ABI drift** — the blob's ids vs the binary's enums. Mitigated by the generated schema-version hash (single-source in C#, the View program's proven approach). Mandatory for AppFlow before any blob reload ships.
3. **Lifetime/ownership across the boundary.** The parser must own the backing storage the view's `string_view`/`span` point into (the `ViewBlobFile` deque discipline) and outlive every consumer — a real hazard the View precedent already solved; copy it exactly.
4. **Runtime shader compiler dependency** (render tier) — see §6.6.
5. **Codegen is a drift-guard, not a live regenerator** — the current flow is schema-edit → manual `_regen` → recompile. Hot-reload changes the *iteration* loop to schema-edit → regen blob → file-watch reload; the drift-guard stays for CI correctness. No conflict, but the mental model shifts.
6. **Where the architecture fights this:** nowhere structurally — the loaders are already agnostic, the enums already pinned/append-only, the View dual-output already built. The only friction is the *absence* of the AppFlow blob face and of any dynamic-library loader.

---

## 10. Sequenced roadmap (with ratify-gates)

**Ratify-gate 0 (owner, before any code):** approve the unified-boundary framing (§3), the **safety contract / shape-hash gate (§6)**, the **facade-content-not-code-internals scope ceiling (§7)**, the dual-output policy (§8), and rule on the handler path (§9.1(a) plugin vs (b) declarative-only) — see OPEN QUESTIONS.

**Tier-1 — facade/UI hot-reload (owner priority):**
- **T1.0 (de-risk):** wire existing `BlobView`/`ViewBlobFile::Load` as the live HUD path behind a flag + file-watch `hud.viewblob` (§5.1 micro-slice). No producer change. *Gate: HUD view-shape hot-reloads live with no recompile.*
- **T1.1:** add `AppFlowEmitter.EmitDataFile` + `AppFlowShapeHash` (producer, stamps the frozen surface only per §6.2); `AppFlowBlobFile` parser + **shape-hash gate** (consumer); `AppFlowRuntime::Load` accepts external view (§5.1). *Gate: blob-loaded AppFlow behaves byte-identically to the compiled header (equivalence test, mirroring `test_view_blob_equiv`); an interior-edit blob has an identical shape-hash and a shape-edit blob a different one.*
- **T1.2:** file-watch `.appflow` in the editor tick → shape-hash gate → on match re-`Load` + handler re-attach at the between-tick swap point (§7.2); **on mismatch, detect + message + rebuild fallback (§6).** *Gate: an interior rewire (existing key/trigger/noun) reaches the running editor with no recompile; a shape change is caught and messaged, never mis-applied.*
- **T1.3 (only if owner picks §9.1(a)):** design the handler/dispatch `.so`/dlopen plugin path on the `Abi.h` shape. *Gate: a NEW action's behavior hot-loads.* (Large; its own program; note this pushes past the §7.4 facade ceiling toward code reload — obstacles (1)–(4) re-enter and must be handled explicitly.)

**Tier-2 — render artifacts (registry reuse):**
- **T2.1:** file-watch a recipe manifest → `LoadRecipePack` → re-splice + runtime re-compile (§5.2). *Gate: a recipe/param change hot-reloads with no C++ recompile.*
- **T2.2:** shader-payload-as-content — GLSL/SPIR-V blob keyed by `recipeId` in the registry/manifest (§4.4). *Gate: a shader change crosses as content.*
- **T2.3:** leave `[GpuStruct]` config layouts compiled-in (§4.5) — no work, explicit decision.

Each gate is an owner ratify point; no tier-2 work starts before tier-1 T1.2 proves the pattern on the priority surface.

---

## 11. OPEN QUESTIONS (owner)

1. **Handler path (the crux).** For *full* facade hot-reload (new actions whose behavior is new), pick §9.1: **(a)** build the transplanted-logic `.so`/dlopen plugin path (net-new infra, biggest risk, realizes D12, pushes past the §7.4 ceiling), or **(b)** bound hot-reload to the declarative `Data`-noun surface + interior body/param reload (zero new infra, handlers stay compiled, stays within the §7.4 ceiling). Tier-1 T1.0–T1.2 deliver value under *either* choice; this ruling only gates T1.3. **Recommendation:** (b) — it stays inside the safe facade ceiling; a callable *body* still hot-reloads (interior, §6.1), only a *new* handler needs the rebuild.
2. **Shipping builds and hot-reload.** Should finalized builds *exclude* the blob/watch path entirely (compiled-in only, no shader compiler shipped), or keep it available behind a flag? (Drives whether dual-output is build-config or always-present.)
3. **Blob format for AppFlow** — reuse the `.viewblob` *text* shape (human-diffable, matches the existing precedent) or a binary blob (like the recipe container)? Text is recommended for the facade tier's iteration ergonomics; the version hash makes either safe.
4. **Producer edits are cross-repo.** The `EmitDataFile` faces live in the Yeroket kernel-framework (read-only in this audit). Confirm the Yeroket-side work is in scope for the implementation program (it is required for T1.1/T2.2).

---

## 12. Summary

- The agnostic content boundary the owner wants **already exists** — mature on the render tier (Recipe Registry) and half-built on the facade tier (View reflection blob). They are **one pattern, two registries** (§3).
- **Tier-1 seam to externalize:** `AppFlowContainerView` — compiled-in today; externalize it by copying the shipped `ViewBlobEmitter`/`ViewBlobFile`/`BlobView` dual-output pattern (§4.1, §5.1). The loader is already noun-agnostic; the enums are already pinned. Lowest-risk, highest-value (20-TU blast radius, pure data).
- **Tier-2:** reuse the Recipe Registry as-is; a recipe/param change already hot-reloads. Add shader-payload-as-content to cross shader-logic changes (§4.4).
- **Honest scope ceiling (§7):** this is **hot-reload of the boundary-crossing FACADE — params, state, calc/system bodies, views, flows, shader interiors — NOT arbitrary-code reload.** That ceiling is the *right* one: the runtime reads content through the boundary and deep-copies at the loader (verified — no retained pointers into generated content, §7.1), so the four classic obstacles (state-layout mismatch, pointer/vtable invalidation, torn execution, live rewiring) are structurally avoided; pushing past the ceiling re-introduces them.
- **Safety by construction (§6):** a **shape-hash gate** freezes the interface/graph shape (declarations, signatures, edges, layouts) and permits only interior swaps (bodies/params/state); a shape change is detected, messaged, and forced to rebuild — never silently mis-applied. Generalizes the shipped `ViewVersionHash`. The tier-1 first slice includes the gate + detect-and-message fallback so the first demo is safe.
- **The payoff number (§7.3):** on a **stabilized** facade surface, the large majority (~70–90%) of iteration is *interior* (tuning: chords, calc bodies, param/threshold values, shader constants) and hot-reloads instantly; while a surface is still *growing its vocabulary*, iteration is shape-dominated (rebuild). Payoff scales with surface maturity — largest in the long polish tail of a shipping product.
- **Top obstacle:** *logic doesn't cross a data boundary* — handlers, callables, shader/SIMD codegen are compiled. Within the recommended ceiling a callable/calc *body* still hot-reloads (interior); only a *new* handler/signature/edge needs a rebuild — so full code-internals reload (a **.so/dlopen plugin path, which does not exist yet**) is **not required**; the recommended path keeps the reloadable surface declarative + interior (§9.1(b)).
- **Dual-output is genuinely needed** (real disadvantages exist — runtime cost, enum-stability, runtime shader recompile, ABI drift; §8). The only class to collapse to compiled-in is `[GpuStruct]` config layout (all-shape, never hot-reloadable).

**STOP — awaiting owner ratification of §3 (unification), §6 (shape-hash safety contract), §7 (facade-content scope ceiling), §8 (dual-output), and the §11 handler-path ruling before any implementation.**
