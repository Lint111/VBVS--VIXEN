# Recipe Pipeline Cache — Increment 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager pipeline to
> implement this plan milestone-by-milestone (fresh implementer + Opus validator per milestone,
> worktree-isolated, progress persisted in this doc; pre-bless the in-tree destructive/git tier at
> setup per the established worktree convention). Windows-native build per this project's standing
> policy (`vixen-build-policy` skill) — this increment is CPU-only infra with no shader/GPU-render
> claims, so a WSL/CPU build+test is sufficient throughout; no live-run gate is required (there is
> nothing to render — see §0 Scope).

**Goal:** Ship **Increment 1** of
[[Runtime-Tiered-Recipe-Pipeline-JIT-Direction-2026-07]] §7: a **recipe-content-hash pipeline
cache** — infrastructure that collapses exact-duplicate recipe bytecode to one cache entry, with
zero change to how recipes actually render (every recipe still renders through the tier-0
`switch(recipeId)` uber-shader, per [[Lazy-Procedural-Delta-Baseline-Inc0-Inc1-Plan-2026-07]] M5).
This is the FIRST of the epic's 4 sketch increments (§7); it is explicitly NOT tier-1 promotion,
NOT GPU-LRU eviction, and NOT family normalization — those are increments 2-4, sequenced after
this one lands. **Pure infra, measurable**: the deliverable is the cache + hash function + a test
proving identical bytecode → one cache entry, distinct bytecode → distinct entries. Nothing yet
routes a body to a per-recipe pipeline.

**Depends on (shipped):** [[Recipe-Parameterization-Plan-2026-07]] (P4, `ReadParam`/
`ReadParamFloat3`, all 4 milestones DONE + Opus-validated APPROVED, merged to main `fb3a577c`
2026-07-15) — NOT a hard technical dependency for Increment 1 itself (per the epic's own §7
ordering, only increment 4 "shape/literal normalization" needs parameterization), but sequenced
after it per the epic doc's stated order, and this plan's hash-key design should account for
`ReadParam`-bearing recipes correctly (see Task 2) since they now exist in the registry.
[[Lazy-Procedural-Delta-Baseline-Inc0-Inc1-Plan-2026-07]] M5 (uber-shader splice, shipped,
CLOSED) — the tier-0 switch this increment leaves untouched.

**Grounded, not aspirational — corrects the direction doc's own §4 claim.** §4 of the JIT
direction doc says "an existing pipeline-cache layer... A recipe→pipeline cache keyed by a
recipe-content hash is the natural home for tier-1 pipelines... gives exact-duplicate
fragment-family sharing for free." Verified 2026-07-15 (research agent survey): **the
infrastructure is real and reusable, but "for free" overstates it slightly:**
- `CashSystem::TypedCacher<D,CI>` (`libraries/CashSystem/include/TypedCacher.h:34`) is a real,
  generic, thread-safe (double-checked-locked + in-flight de-dup, `:79-123`) cache keyed by a
  caller-supplied `virtual uint64_t ComputeKey(const CI&) const` — genuinely the right base to
  derive from, not aspirational.
- **BUT** the existing `PipelineCacher::ComputeKey` (`libraries/CashSystem/src/pipeline_cacher.cpp:169`)
  does NOT hash any shader/bytecode content today — it hashes string *keys* the caller already
  computed (`vertexShaderKey`/`fragmentShaderKey`/layout/state) via `std::hash<std::string>`.
  "Exact-dup sharing for free" requires THIS increment to feed it (or a sibling cacher) a real
  content hash — the cache mechanism is free; the content-hashing is this increment's actual work.
- **The exact pattern already exists and is proven live**, just not for recipes yet:
  `ComputePipelineNode` (`libraries/RenderGraph/src/Nodes/ComputePipelineNode.cpp:261-282`) ALREADY
  keys its `ComputePipelineCacher` cache entry on `programName + ":" +
  ShaderManagement::ComputeSHA256HexFromUint32Vec(spirv)` — hash-the-actual-compiled-code, not the
  descriptor-interface uuid (its own comment explains why: two different shaders can share a
  descriptor layout and would incorrectly collide on interface-uuid alone). This is the template to
  mirror, not invent from scratch.

**Architecture:** A new, small `TypedCacher<D,CI>` derivative (or an extension of an existing one —
decide in Task 1) keyed by a hash of `RecipeEntry::bytecode`, computed via one of three
already-in-tree hashers (no new hash function): `Vixen::Hash::ComputeHash64`
(`libraries/Core/include/VixenHash.h:30`, FNV-1a), `CashSystem::CacheKeyHasher`
(`libraries/CashSystem/include/CacheKeyHasher.h:34`, a builder purpose-made for accumulating
trivially-copyable values + raw bytes into one key — the best fit for hashing a
`vector<SdfInstruction>`), or `ShaderManagement::ComputeSHA256HexFromUint32Vec`-style (mirrors
`ComputePipelineNode`'s own pattern exactly, but that one is SPIR-V-word-shaped, not
`SdfInstruction`-shaped — prefer `CacheKeyHasher` unless Task 1 finds a reason to hash post-emit
GLSL/SPIR-V instead of pre-emit bytecode, see Task 2's open question). No new GPU binding, no
change to `SpliceProceduralRecipesIntoSource`, no change to what actually renders.

**Tech Stack:** C++23, GoogleTest, CMake ninja/wsl presets. Pure CPU/infra — no GPU, no shader
compile, no render gate needed for this increment (confirm this holds once Task 1 is scoped; if
the design ends up needing to hash post-glslang SPIR-V rather than pre-emit bytecode, a GPU/compile
step may become necessary — flag and re-scope if so, don't silently assume).

---

## §0. Scope

**In scope:**
- A recipe-bytecode content-hash function (Task 1) — decide bytecode-level (pre-emit
  `SdfInstruction[]`) vs. emitted-GLSL/SPIR-V-level hashing, with a stated reason (Task 1).
- A new cache keyed by that hash, built on `CashSystem::TypedCacher<D,CI>` (Task 2) — resolve
  what `D`/`CI` should be for a recipe cache entry (this is NOT yet a `VkPipeline` — see the
  Architecture note above, nothing routes to a per-recipe pipeline in this increment; `D` may be a
  lightweight identity/metadata record, not `PipelineWrapper`, until Increment 2 gives it a real
  pipeline to hold — decide explicitly, don't default to copying `PipelineCacher`'s shape if it
  doesn't fit).
- Correct hash-key semantics for `ReadParam`/`ReadParamFloat3`-bearing recipes (Task 2) — two
  recipes with IDENTICAL bytecode INCLUDING identical `ReadParam` slot indices should collapse to
  one cache entry (the whole point — a "family" sharing one specialized pipeline eventually feeds
  different PARAM VALUES per instance, not different bytecode); confirm the hash is over the
  bytecode structure/opcodes, not over any runtime `recipeParams[]` VALUE (which isn't part of
  `RecipeEntry` at all — verify this is naturally already true given `RecipeEntry` only stores
  `bytecode`, but state it as a deliberate correctness property, not an accident).
- A unit test suite (Task 3) proving: identical bytecode → identical hash → one cache entry;
  bytecode differing in even one instruction/opcode/literal → distinct hash → distinct entries; a
  recipe registered, then a SECOND structurally-identical recipe registered under a different
  `recipeId` → both hash to the same cache key (the "exact-dup family sharing" proof).
- Wiring the cache to actually get POPULATED somewhere real (Task 4) — e.g. on
  `RecipeRegistry::Register`, or lazily on first query — decide and justify; this increment does
  NOT need the cache to be QUERIED by anything downstream yet (that's Increment 2's job), but it
  needs a real call site proving the cache is genuinely exercised, not just unit-tested in
  isolation.

**Out of scope** (explicitly later increments per the JIT direction doc §7, do not build ahead of
them): async tier-1 promotion-on-hot-usage (Increment 2); GPU-LRU eviction of cold pipelines
(Increment 3, the Sparse-Mip M4 re-open); shape/literal normalization into parameterized family
pipelines + batched dispatch-by-pipeline (Increment 4); ANY actual per-recipe `VkPipeline`
creation, compilation, or routing — this increment's cache does not yet hold or create pipelines
in the GPU sense (see the `D`/`CI` resolution note above); any change to
`SpliceProceduralRecipesIntoSource`/the tier-0 switch's behavior; any change to
`BodyInstanceRayMarch.comp` or any shader file; the per-recipe-type declared Gaia query direction
([[Recipe-Declared-Gaia-Query-Direction-2026-07]]) or the dirty-upload direction
([[Instance-SSBO-Dirty-Upload-Direction-2026-07]]) — both unrelated follow-ons, not this epic.

---

## Milestone Map

- **M1 — Hash function + cache-key design** (Tasks 1-2) · gate: pure-CPU gtest green — a
  content-hash function over `RecipeEntry::bytecode` exists, is deterministic, and the design
  question of what `D`/`CI` the cache should hold is resolved and documented (not deferred as an
  open question into implementation).
  - [ ] Not started.
- **M2 — Cache implementation + population wiring + tests** (Tasks 3-4) · gate: pure-CPU gtest
  green — the `TypedCacher` derivative exists, is populated at a real call site, and the
  exact-dup-collapse property is proven by test (identical bytecode → 1 entry; distinct bytecode →
  distinct entries; zero regression on existing RecipeRegistry/CashSystem suites).
  - [ ] Not started.

### Progress Log

(populated as milestones complete — one entry per milestone: commit hash, gate evidence, Opus
validator verdict; follow the Recipe-Parameterization / Lazy-Procedural plans' convention.)

---

## Tasks

### M1 — Hash function + cache-key design

**Task 1 — Decide and implement the content-hash function.**
Resolve: hash the PRE-EMIT `std::vector<Recipe::SdfInstruction>` bytecode directly (simpler,
available immediately at `RecipeRegistry::Register` time, no glslang/GPU dependency), or hash the
POST-EMIT GLSL/SPIR-V (mirrors `ComputePipelineNode`'s exact pattern, but requires running the
emitter — `EmitProceduralFieldFunctionGlsl` — and possibly glslang, for every registered recipe,
which is more work and pulls in a compile step this increment's own Tech Stack note says should
stay CPU-only). **Recommendation to verify, not assume:** pre-emit bytecode hashing is almost
certainly correct for THIS increment's purpose (exact-duplicate detection at the recipe-authoring
level, before any pipeline exists) — the codegen from bytecode→GLSL is presumed deterministic
(same bytecode always emits identical GLSL, per the emitter's own design), so hashing the smaller,
cheaper, earlier-available bytecode should be equivalent for cache-key purposes and far simpler.
Confirm this equivalence holds (spot-check: does anything about instruction ORDER-preserving
emission make this non-obviously true or false?) before committing to it — if the emitter has any
non-deterministic or context-dependent behavior (e.g. depends on `recipeId` itself, not just
bytecode), that would break the equivalence and this needs to be flagged, not silently assumed.
Implement via `CashSystem::CacheKeyHasher` (`CacheKeyHasher.h:34`) — its `AddBytes`
(`:64`)/`Add` (`:43`) accumulator API is a direct fit for a `vector<SdfInstruction>` (each
`SdfInstruction` is a fixed 132-byte POD per the format contract — feed the whole vector as one
`AddBytes` call, or per-instruction if the `Add<T>` overload doesn't handle raw pointer+length
directly, check the header). Do NOT invent a new hash function — this codebase has three already
(see plan doc's Architecture section); use `CacheKeyHasher` unless Task 1's own investigation finds
a concrete reason it doesn't fit.

**Task 2 — Resolve cache-entry shape (`D`/`CI`) and `ReadParam` semantics.**
Decide what the cache VALUE (`D` in `TypedCacher<D,CI>`) actually is for this increment. Since no
per-recipe `VkPipeline` exists yet (Increment 2's job), `D` is likely a lightweight record — e.g.
`{contentHash, firstRecipeId, memberRecipeIds[]}` (which recipes share this hash) or similar —
NOT a `PipelineWrapper`-shaped thing. Decide and document explicitly; do not force-fit
`PipelineCacher`'s existing `D=PipelineWrapper` shape if it doesn't represent what this increment
actually has to cache. Confirm/state as a correctness property (not just implicitly true): the
hash is computed purely from `RecipeEntry::bytecode` — including `ReadParam`/`ReadParamFloat3`
instructions' `data[0]` slot-index literals (these ARE part of the bytecode, correctly
hash-distinguishing "reads param slot 0" from "reads param slot 1") — and NEVER from any runtime
`recipeParams[]` value (which lives in `BodyInstanceGpu`, a completely separate per-INSTANCE
structure `RecipeEntry` has no visibility into). This is the property that makes "many instances,
one shared parameterized pipeline" possible in a LATER increment — get it right and documented now
even though nothing consumes it yet.

### M2 — Cache implementation + population wiring + tests

**Task 3 — Implement the `TypedCacher` derivative.**
New class (likely `libraries/CashSystem/include/RecipeContentCacher.h` + a `.cpp`, or
`libraries/SVO/include/Recipe/...` if it makes more sense to live recipe-side — decide based on
whether `CashSystem` should know about `Recipe::SdfInstruction` at all, or whether the recipe
system should own a thin cacher that only depends on `CashSystem::TypedCacher`'s template
machinery; check `CashSystem`'s existing dependency direction before deciding which way this
should point). Implements `ComputeKey`/`Create` per Task 1/2's resolved design. Register it with
`MainCacher` following `ComputePipelineNode`'s registration pattern
(`RegisterCacher<...>`/`GetCacher<...>`, `ComputePipelineNode.cpp:239-253` as the template) if that
convention fits, or justify a standalone cacher if not.

**Task 4 — Wire real population + write the test suite.**
Pick a real call site to populate the cache — most natural is inside or alongside
`RecipeRegistry::Register` (every registered recipe gets hashed and cache-queried at registration
time), but confirm this doesn't create an unwanted coupling between `RecipeRegistry` (SVO-library)
and `CashSystem` (a different library) — check existing include/link relationships between these
libraries before wiring a new one; if `RecipeRegistry` genuinely shouldn't depend on `CashSystem`,
population may need to happen at a higher-level call site instead (e.g. wherever
`RegisterProceduralRecipe` is called from application/RenderGraph code) — decide and document.
Tests (new gtest file, follow existing `test_recipe_*`/`test_cash_system_*` naming convention —
check `libraries/CashSystem/tests/` if it exists, or `libraries/SVO/tests/` for the recipe-side
convention): (a) two recipes with byte-identical bytecode registered under different `recipeId`s →
same cache key/one entry; (b) two recipes differing in exactly one opcode, one literal float, or
one `ReadParam` slot index → distinct cache keys; (c) `ReadParam` slot-index differences are
correctly distinguished (recipe reading `params[0]` vs. `params[1]`, otherwise identical bytecode,
must NOT collapse to one entry — this would be a real correctness bug, silently sharing a "pipeline
family" between two recipes that need different param semantics); (d) zero regression on the
existing `RecipeRegistry`/`CashSystem` test suites.

---

## Risks / decision points

- **Pre-emit-bytecode vs. post-emit-GLSL hashing (Task 1).** The plan recommends pre-emit but
  requires verifying the codegen is genuinely deterministic and context-free before committing —
  don't skip this check, a wrong assumption here would silently under- or over-collapse recipes
  that should or shouldn't share a family in later increments.
- **`RecipeRegistry` ↔ `CashSystem` library coupling (Task 4).** This may not be as simple as
  "just call the cacher from `Register`" if the libraries aren't already linked together — check
  before assuming, and be willing to wire population from a higher layer instead.
- **This increment is deliberately inert.** No production behavior changes — every recipe still
  renders through the tier-0 switch exactly as before. Resist the temptation to also start
  Increment 2's routing logic "since you're in here" — that's explicitly the next plan doc, not
  this one, per the epic's own §7 sequencing.
