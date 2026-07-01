---
title: Arbitrary Recipe Authoring → Render — Spine + T1 (Stored)
status: Design — approved 2026-06-29; first sub-project of the post-P2.4 arbitrary-recipe (FORMAT→EXECUTION) epic. Not yet planned/built.
date: 2026-06-29
updated: 2026-06-29
tags: [architecture, sdf, recipe, kernel-codegen, content-format, provider, stored, materialization, authoring, cross-repo, future]
aliases: [Recipe Authoring Pipeline, Arbitrary Recipes, Recipe Registry, Recipe Container Format, Bake-to-Stored, Spine T1]
related:
  - "[[Voxel-Content-Format-Contract-Design-2026-06]]"
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]]"
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-P2.4-Spec-2026-06]]"
  - "[[Destructible-Body-Rendering-Direction-2026-06]]"
---

# Arbitrary Recipe Authoring → Render — Spine + T1 (Stored)

> [!summary] Scope
> The **first sub-project** of the post-P2.4 "arbitrary recipe authoring" epic (the FORMAT→EXECUTION
> program). Delivers the end-to-end loop: an **arbitrary authored SDF recipe** is ingested by VIXEN,
> registered under a stable id, **baked to voxels**, and **rendered via the shipped Stored path** — the
> smallest slice that proves *authored recipe → on-screen* while reusing the existing renderer.
> Game-agnostic **VIXEN engine capability**; consumers (UNDERSET) supply recipes conforming to the
> published format. **Cross-repo** (kernel/Yeroket + VIXEN), like P2.4.

## 1. Goal & ownership

Today the only procedural recipes the live renderer knows are two hardcoded analytic shapes
(`recipeId` 0=sphere / 1=displaced-sphere, `SdfRecipes.{h,glsl}`). The P2.4 catalogue (~87 opcodes,
`SdfInstruction[]` bytecode) can be compiled-to-shader or baked-to-Stored, but there is **no authorable
format, no engine recipe registry, and the instanced renderer cannot consume an arbitrary recipe**.

This sub-project closes that for the **Stored target**: arbitrary recipes flow content → engine →
voxels → screen. VIXEN **owns and publishes the recipe format contract**; how human intent becomes
recipe bytecode (a DSL, node graph, etc.) is an **upstream** concern and out of scope here.

## 2. Decisions (locked in brainstorming 2026-06-29)

| # | Decision |
|---|---|
| Scope | Decompose the epic; build **the spine + T1 (Stored)** first. T2 (compiled-SPIR-V) and T3 (dynamic GPU VM) are separate later specs. |
| Format | VIXEN **ingests a pre-compiled serialized `SdfInstruction[]` blob**; it does **not** ship a text authoring parser. ("pre-compiled recipe passing".) |
| Reader | The container reader/parser is a **kernel-side codegen artifact** (single source of truth) — the source-gen emits a vendored C++ `.g.h`, exactly like `SdfOpCodes.g.h`. `SdfInstruction` is promoted from hand-mirror to generated. |
| Identity | A recipe is addressed by a **content-declared namespaced id resolved to a dense U32** via the pack manifest (mirrors undertow's `NamespacedId` model). Registry = `U32 → {bytecode, octreeSlot}`. |
| Render binding | Bake each recipe → its own Stored octree in a **memory-budgeted, count-unbounded** octree pool; the bridge resolves `recipeId → octreeSlot` and renders `providerKind=Stored, octreeIndex=slot` via the **shipped ESVO march, unchanged**. |
| Pool | **No count cap** — the only limit is the allocated byte budget. Lift the hardcoded `kMaxOctrees=3`. |
| Lifecycle | **Static bake at load**, **fail-loud** at the budget ceiling. On-demand/lazy + world streaming + eviction is a **follow-up** (the B path / provider-LOD). |
| Resolution | Default bake resolution + **optional per-recipe override** in the container header. |

## 3. Data flow

```
authoring (upstream, C# CompileToBurst) ──► recipe blob (serialized SdfInstruction[])
        │                                          │  ships in mod pack (+ manifest: namespaced id → U32)
        │                                          ▼
        │                            VIXEN loads manifest + blobs
        │                                          ▼
        │                     Recipe Registry:  U32 recipeId → { bytecode, octreeSlot }
        │                                          ▼
        │                     Bake (evalRecipe + SdfBake, at load) ──► Stored octree in
        ▼                                                              memory-budgeted pool
  kernel source-gen emits the C++ READER (.g.h)                        ▼
  VIXEN vendors + consumes it                          bridge: body.recipeId → octreeSlot
                                                       render: providerKind=Stored, octreeIndex=slot
                                                              → shipped ESVO instanced march (unchanged)
```

## 4. Components

### 4.1 Recipe container format (interchange) — single-sourced
A versioned, self-describing blob:
- **Header:** magic · format-version · recipe namespaced-id · default bake resolution + optional
  per-recipe override · opcode-count (+ a reserved/spare region for forward-compat).
- **Body:** the `SdfInstruction[]` stream (132-byte records today).

The format is **defined canonically in the kernel framework**; the source-gen emits the **C++ reader as a
vendored `.g.h`** (same pattern + VERBATIM-vendoring rule as `SdfOpCodes.g.h`), and `SdfInstruction` is
**promoted from today's hand-written 132-byte mirror to a generated struct**, closing the last
hand-mirror. VIXEN consumes the generated reader — **no hand-written parser, zero drift**. VIXEN
**publishes the format-contract doc** (layout · version · valid-opcode set) so upstream authors target a
stable spec.

### 4.2 Recipe registry (engine-owned)
`U32 recipeId → { SdfInstruction[] bytecode, octreeSlot }`. Populated at load from the pack manifest
(namespaced → dense U32). **Validation on ingest** (fail-loud with id + reason): format-version match,
every opcode ∈ the valid set, the existing `sp < 64` / `paramMask == 0` invariants. The current 3
archetypes (star/planet/moon) become ordinary registry entries — no special case.

### 4.3 Bake pipeline (recipe → Stored octree)
Reuse `evalRecipe` + `SdfBake` to voxelize each registered recipe into a Stored `SdfBodyOctree` **at
load (static)**. Default bake resolution, overridable per-recipe via the header. This is the existing
Materialize-to-Stored machinery applied per registry entry.

### 4.4 Octree pool — memory-budgeted, count-unbounded
Lift the hardcoded `kMaxOctrees = 3` (`ConcatenatedOctrees`, `configs[3]`, the GPU buffers, the shader's
`clamp(octreeIndex, 0, 2)`) to a **dynamic, memory-budgeted arena**: `configs[]` dynamic; nodes /
bricks / channelPool / brickGridLookup buffers sized to an allocated **byte budget**; octree index
generalized. **No count cap** — the limit is the budget. **Fail loud** at the ceiling ("recipe pool over
budget by X"). This is the one change to *shipped* Stored code and is the correct root-cause fix (the cap
was always a placeholder for the 3 archetypes).

### 4.5 Render binding
The bridge resolves `recipeId → octreeSlot` (registry) and emits each body as
`providerKind = Stored, octreeIndex = slot`. **Render path = the shipped ESVO instanced march,
unchanged.** `recipeId` is a bake-time/registry key; at render time a T1 body is just a Stored body
pointing at a pool slot.

## 5. Testing strategy (live-gate authoritative)

- **CPU eval parity** for each recipe against the `evalRecipe` oracle (existing pattern).
- **Bake round-trip** — recipe → octree → sample matches eval within tolerance.
- **Pool tests** — N > 3 octrees pack + address correctly; the budget ceiling fails loud (not silent).
- **Reader parity** — the generated C++ reader round-trips a C#-written blob byte-identically.
- **Live GPU gate (authoritative)** — author a *non-trivial* recipe clearly distinct from sphere/the 3
  archetypes (e.g. a CSG-composed shape), load it, confirm it renders correctly on lavapipe/real-GPU;
  capture a PNG and validate pixel content (no degenerate-silhouette trap — pick a shape whose outline
  proves the recipe).
- **No-regression** — the existing 3-body scene still renders with archetypes-as-registry-entries.

## 6. Scope boundaries (YAGNI — explicitly deferred)

- **On-demand / lazy bake + world streaming + eviction** → follow-up (the B path / provider-LOD; ties to
  [[Destructible-Body-Rendering-Direction-2026-06]]).
- **T2 compiled-SPIR-V** specialized pass and **T3 dynamic GPU VM** (data-driven interpreter) → separate specs.
- **Dynamic params** (`paramMask != 0`) → deferred (the P4 lever).
- **Authoring UI / text DSL** → upstream, out of engine scope.

## 7. Cross-repo split + increments

**Kernel/Yeroket:** container schema + C# writer (`CompileToBurst` → blob) + the **C++ reader emitter** +
generated `SdfInstruction`.
**VIXEN:** vendor the generated reader → registry + manifest load + validation → bake-to-pool +
memory-budgeted pool generalization → bridge resolution + live gate → format-contract doc.

Proposed increments for the plan:
- **I1 (kernel):** container schema + writer + C++ reader emitter + generated `SdfInstruction`.
- **I2 (VIXEN):** vendor reader; registry + manifest ingest + validation.
- **I3 (VIXEN):** bake-to-pool + memory-budgeted, count-unbounded octree pool (lift the 3-cap).
- **I4 (VIXEN):** bridge `recipeId → slot` resolution + live GPU gate + format-contract doc.

**Ordering:** default **kernel-reader-first** (I1 before I2 — no throwaway parser, honoring the
single-source principle). Schedule lever: if I2–I4 must proceed in parallel, a *temporary* VIXEN-side
reader stub may unblock them **with a hard retirement** when I1's generated reader lands (a lift, per the
kernel-framework no-workarounds rule).

## 8. Open items / risks

- **Container schema details** (exact header fields, endianness, alignment, the spare region) — pinned in
  I1; must stay blittable-friendly and version-guarded.
- **Manifest format** (where namespaced→U32 lives, who emits it) — coordinate with the sim/content side;
  reuse undertow's content-id conventions.
- **Budget sizing** — the default byte budget + how a scene declares it; fail-loud messaging.
- **Bake cost at load** — many recipes × static bake could lengthen load; acceptable for the first slice,
  and the motivation for the deferred on-demand/streaming path.

## 9. Related

- [[Voxel-Content-Format-Contract-Design-2026-06]] — the 3-provider model (Stored is the T1 target)
- [[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]] · [[SDF-Recipe-Kernel-Codegen-Inc4-P2.4-Spec-2026-06]] — the catalogue + bytecode this builds on
- [[Destructible-Body-Rendering-Direction-2026-06]] — where the deferred on-demand/streaming (B) leg leads
