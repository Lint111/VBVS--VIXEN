---
title: Per-Recipe-Type Declared Gaia Query — Direction
status: future / not scheduled; UNDERTOW authority boundary reconciled 2026-07-26
created: 2026-07-15
---

# Per-Recipe-Type Declared Gaia Query — Direction

> **💡 Not scheduled.** Captured while scoping
> [[Recipe-Parameterization-Plan-2026-07]]. That prerequisite is now **✅ SHIPPED** (`ReadParam` and
> per-instance param arrays exist), but this direction still has no implementation plan.
>
> **UNDERTOW authority amendment (2026-07-26):** VIXEN must never query UNDERTOW's authoritative
> `GaiaSimWorld` directly. For UNDERTOW, “declared Gaia query” means either (a) a sim-owned generated
> projection/query binding that emits typed, viewer-safe recipe-instance batches over
> `Undertow.View`, or (b) a VIXEN-local query over the already-projected `GaiaVoxelWorld` mirror.
> It may not bypass fog-of-war, pull hidden components, or make render state authoritative. The
> original exploratory wording below is retained where useful, but this amendment wins.

## 1. The idea (user, 2026-07-15, verbatim intent)

A quality-of-life mechanism for parameterized recipes (unfolded/tier-1-promoted or not): each
**recipe type** should declare an automatic Gaia ECS query that is *always the best-known way* to
retrieve the set of sim data that recipe needs. This turns per-instance param population from
hand-wired glue into a **declared-shape → generated-query** pipeline, and enables a **dispatch
pattern**: 1000 live instances of recipe 1, 20 of recipe 6, etc. batch into one full per-recipe-type
data set gathered from the sim frame in one pass. Ideally the query should read from the same
**declared view layer** the renderer already snapshots per frame (not a second independent live-ECS
query), so recipes get frame-cached values with no double-access pattern. The follow-up insight
mid-scoping: **this unlocks easy indirect instantiation** — spawning more instances of a recipe
type becomes "the query returns more rows," not a new hand-written call site.

## 2. Why this is the right layer, not a new one (grounded, not hypothetical)

This is not inventing a new boundary — it's extending one that already exists and already carries
almost the exact data shape needed:

- **`Undertow.View`'s `Bodies` section already has a `RecipeParams` column**
  (`View-Contract-Inc5b-Typed-Accessor-Emitter-Plan-2026-07.md` — declared alongside
  `Position`/`OrbitParams`, matching `BodyInstanceGpu::recipeParams[6]`'s real shape; the plan's own
  words: "closes the MODEL-layer half of Bodies' Vec3f-scalar..."). This is a schema-generated,
  byte-identical-on-both-sides SoA slice (`Undertow.View` C# `ViewWriter` ↔ VIXEN C++ reader) — the
  "declared view layer" the user is asking to read FROM, not around.
- **The real sim→render seam is the generated `Undertow.View` contract; the authoritative UNDERTOW
  sim is still managed while Gaia migration is partial.**
  `ut_view` → `ReadBodies` reads Bodies SoA columns Position/Kind/Mass/tint →
  `BuildBodyInstances` → `ToBodyInstanceGpu` → `BodyOctreeSceneNode`. Today the carrier mapping is a
  **hand-written, linear, one-loop-over-SoA-rows** projection (anti-
  combinatorial by construction, per that memory doc) — not per-recipe-type, not queried, just one
  fixed loop that reads the same columns for every body regardless of its actual recipe/`providerKind`.
- **`gaia-semantics` skill's query primitives are the natural implementation substrate**:
  `query().all<T>()`/`.any<T>()`/`.no<T>()` (`ecs/query.h`), `.each(Func, QueryExecType)` with a
  `Parallel`/`ParallelPerf`/`ParallelEff` exec mode already available for batched per-archetype
  iteration, and the standing convention "keep one persistent `Query` object per bound view" — this
  is exactly the "declare once per recipe type, batch-execute every frame" shape being proposed,
  not a new capability Gaia lacks.

**So the mechanism this idea wants is: recipe TYPE → declared Gaia query shape → per-recipe-type
batched result set, fed through the SAME `Undertow.View` schema/marshaling machinery that already
exists for `Bodies`, rather than a bespoke per-recipe hand-rolled loop.** The View-Contract program
already proved the "declared schema removes a hand-maintained, drift-prone boundary" pattern for
the UI-facing HUD (`Renderer-Agnostic-View-Contract-Design-2026-07.md` §1.1 — "the manually-tracked
view version is subsumed by a generated schema version/hash"); this is the same shape applied to
the sim-facing recipe-instantiation boundary.

## 3. What "declare a query per recipe type" would actually mean

Sketch only — not a design, this needs its own scoping pass when picked up:

- A recipe TYPE (not instance) declares which sim-side fields it reads to populate its
  `recipeParams[]` — e.g. "recipe 6 needs `OrbitParams.eccentricity`, `Mass.value`,
  `Faction.tintColor`" — analogous to how a `[View]` schema declares fields today, but scoped to
  one recipe's param contract instead of one whole HUD view.
- That declaration compiles (codegen, mirroring the View-Contract program's own machinery) into a
  Gaia query shape (`world.query().all<OrbitParams, Mass, Faction>()` or equivalent) that is
  provably the minimal/correct fetch for that recipe — "always the best approach to retrieve the
  set of data the recipe needs" (user's framing) means generated-and-provably-matching the
  declaration, not hand-guessed and liable to drift, the same anti-drift motivation as §1.1 above.
- Batched dispatch: group live instances by recipe type, run each type's query once per frame
  (not once per instance), and build the frame's `recipeParams[]` sets in one pass per type — "1000
  sets of recipe 1, 20 sets of recipe 6" batches into that many per-type dispatch groups, each
  populated by exactly one query execution rather than N ad-hoc lookups.
- **Sourced from the frame-cached View layer, not live ECS, to avoid double-access.** If the
  `Undertow.View` snapshot already ran this frame (it does, every tick, per the integration map),
  the recipe query should read FROM that already-materialized SoA blob (or from the same Gaia world
  state the View writer itself just queried, if run in the same frame phase) rather than the
  renderer re-querying live ECS state independently — one query per frame per data need, not two.
- **Indirect instantiation, the payoff.** Once a recipe type's data need is a declared query shape
  instead of a hand-wired call site, spawning more instances of that recipe is purely a sim-side
  concern (spawn more entities matching the query's component signature) — no render-side glue
  code needs to change to support more/fewer live instances of an existing recipe type. This is the
  concrete "quality of life" the user named it for.

## 4. Open questions (for whoever scopes this properly)

- **RESOLVED for authority ownership:** the authoritative query/projection belongs UNDERTOW-side,
  against managed state today and `GaiaSimWorld` after per-family cutover. VIXEN consumes the
  generated result and may query/group only its `GaiaVoxelWorld` mirror. The two worlds share a
  library lineage, not authority or direct access.
- Per-recipe-type query declaration syntax/location: a new codegen face bolted onto the existing
  `[View]`/recipe-registration machinery, or a standalone mechanism? Given §2's finding that
  `RecipeParams` is already a `Bodies` column, the simplest version might be "no new query at
  all — just project the existing `Bodies` SoA columns into `recipeParams[]` per recipe type's
  declared field mapping," which is much smaller than "generate a bespoke ECS query per recipe."
  Worth checking whether the full generality (arbitrary component queries beyond what `Bodies`
  already carries) is actually needed before building it.
- How this interacts with [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]]'s per-recipe-
  type batched dispatch (grouping instances by pipeline for the tier-1 promoted path) — the
  "1000 sets of recipe 1, 20 sets of recipe 6" batching described here is very close to what that
  epic's §7 sketch step 4 ("shape/literal normalization → parameterized family pipelines... batched
  dispatch-by-pipeline") already wants on the GPU-dispatch side. These may be the same batching
  concern viewed from the sim-query side vs. the GPU-dispatch side — worth reconciling into one
  design rather than building two separate per-recipe-type grouping mechanisms.

See [[Recipe-Parameterization-Plan-2026-07]] (the prerequisite — `ReadParam`/`recipeParams[]` must
exist before there's anything to batch-populate), [[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]]
(the GPU-side batching this may converge with), [[Renderer-Agnostic-View-Contract-Design-2026-07]]
(the real sim→render seam this would extend), and [[View-Contract-Codegen-Design-2026-07]] (the
"declared schema removes a hand-maintained boundary" precedent this follows).
