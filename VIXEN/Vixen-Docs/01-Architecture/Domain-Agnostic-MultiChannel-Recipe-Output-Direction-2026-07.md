---
title: Domain-Agnostic Multi-Channel Recipe Output + Subset-Fused Codegen — Direction
status: DIRECTION (design complete, pre-plan) — user-directed 2026-07-18; awaits brainstorm→writing-plans→pipeline. Grounded in a 2-repo design-scout (VIXEN content-config + Yeroket kernel-framework). All architecture decisions RESOLVED (see §4).
created: 2026-07-18
program: domain-agnostic-multichannel-recipe-output
---

# Domain-Agnostic Multi-Channel Recipe Output + Subset-Fused Codegen

## 0. Origin & intent (user, 2026-07-18)

Spun out of M11.3 (Cornell procedural-native emission) once recon showed the real need is
architectural, not a one-off emission hack. **Intent, verbatim:** migrate the Unity-domain
multi-channel SDF-graph OUTPUT solution into a **domain-agnostic** solution that is integrated with
and usable natively by **the content-config setup we already use to define voxel data**; and instead
of a full different-outputs instruction tree per property, provide an option to **request an
instruction tree for a specific SUBSET of properties** — e.g. a recipe supporting
`{distance, color, roughness, metalness, brightness}`, asked via a config call for
`{roughness, brightness}`, returns **one combined method** computing exactly those, **without the
overhead of repeated calculations per property** we actually want to expose.

This is the engine's external-logic-injection / recipe-output contract maturing from single-output
(distance only) to a **content-config-driven multi-channel output** with **fused subset codegen**.

## 1. Why this matters / relationship to existing work

- Retires the mandatory light-tree side-bake (`BakeRecipeInstructionsToSdfWorldWithEmission`) — emission
  becomes a channel any provider outputs natively. SUPERSEDES the old M11.3 framing.
- Unifies two today-disjoint "channel" worlds: the recipe OUTPUT vocabulary and the voxel STORAGE
  vocabulary — see [[runtime-kernel-pipeline-direction]] (content-config canonical schema) and
  [[kernel-codegen-framework-direction]] (the codegen core). Consistent with the framework's own
  purpose: it is already a **domain-blind transpiler** (kernel-framework skill §0) — this program lifts
  a Unity-specific contract into that domain-blind core, where it belongs.
- First real consumer = emission (feeds M11's Cornell lighting). But the capability is general:
  color/roughness/metalness/AO all become first-class recipe outputs.

## 2. Current state (design-scout, 2026-07-18, file:line-grounded)

**Content-config already drives dynamic voxel SHAPES — but not the channel SET.**
- `ChannelDesc.cs` (1 uvec4 `{semanticId, elemCount, channelBaseFloats, fieldKind}`) + `OctreeConfig.cs:51`
  `[GpuArray(8)] ChannelDesc channels` @224 + `channelCount` @212. Generated → `OctreeConfig.g.h` +
  `OctreeConfig.glsl:25`. Shaders resolve a semantic→layout at RUNTIME by scanning `channels[]`
  (`StoredSdf.glsl:46`, `MipFallback.glsl:35`; C++ `ShellOctreeGpu.h:363`). A body with a different
  channel set/count is handled purely by its own `channels[]`+`channelCount` — genuinely dynamic.
  `channelCount==0` is valid (procedural/binary bodies).
- BUT the channel SET is a hardcoded C++ table: `kChannelSpecs[]` (`ShellOctreeGpu.h:767`,
  `{SEM_SDF/1/FK_DISTANCE, SEM_COLOR/3, SEM_ROUGHNESS/1, SEM_EMISSION/1}`, single source of truth for
  pool layout) + a hand-written enum `SemanticId` (`VoxelChannelFormat.h:6`, NOT codegen-emitted).
  `ChannelDesc` instances are POPULATED FROM this table (`ShellOctreeGpu.h:778-870`).

**Recipes have NO output-channel concept.** `evalRecipe` (`SdfRecipeEval.h:568`) returns one `float`;
`EmitProceduralFieldFunctionGlsl` (`SdfRecipeCodegenGlsl.h:30`) emits one `sdfRecipe_<id>(vec3)->float`.
Single-output both CPU and GLSL. Net-new producer side.

**Unity has the full output CONTRACT + a per-function CSE cache (the reusable asset).**
- Contract: `SDFOutputChannel` flags (`SDFOutputChannel.cs:11` — Distance/Normal/Color/MaterialID/AO/
  Thickness/VectorField/Custom0-3), `SDFOutputNode` per-channel slots each with its own `inputNodeIndex`
  (`SDFOutputNode.cs:70-97`), `IMultiOutputNode.GetChannelSourceIndex(channel)->nodeIndex`
  (`IMultiOutputNode.cs:16`), `SDFVariantResult` all-channels result struct, `_DNC` variant suffix
  (`GetVariantSuffix`, `SDFOutputChannel.cs:71`). Each slot connects to a separate graph chain (a region
  of the shared DAG) — the "output nodes different graph areas connect to" the user described.
- **How it compiles TODAY (the crux):** `SDFHLSLCompiler.CompileMultiOutput` (`:150`) emits a SEPARATE
  `_D`/`_C`/`_M` function per requested channel (`EmitGraphFunctionFromNode`, `:257-274`), then a wrapper
  that calls them sequentially (`EmitVariantWrappers`, `:313-379`). **A node feeding both Distance and
  Color is walked TWICE.**
- **Latent shared-walk logic EXISTS, scoped per-function:** `EmitNode` does real CSE — `HashSet<int>
  emitted` + `_nodeVars` cache (`:415-441`, `:1744`) — shared nodes within ONE channel emit once. BUT
  each channel function gets a FRESH `emitted` set (`:305`). **The fusion = hoist that cache to span the
  requested channel subset in one method.** The dedup machinery is written; only its SCOPE is per-function.

## 3. Target architecture

**Channel vocabulary = ONE union list, content-config-driven, two projections.**
A single channel declaration in the content-config schema; each entry carries role metadata:
`{ semantic, elemCount, fieldKind (nullable), roleFlags: Stored | DerivedOutput | Outputtable }`.
- `ChannelDesc` / voxel-storage `channels[]` = the union list **projected to `Stored`**.
- A recipe output-request = the union list **projected to `Outputtable`**.
- Examples: Color = Stored+Outputtable (elem 3, FieldKind mean); Emission = Stored+Outputtable (elem 1);
  Normal/AO/Thickness = DerivedOutput-only (no storage, no FieldKind — computed from gradient);
  SDF/Distance = Stored, always present.
- `SemanticId` becomes a **codegen-emitted mirror-enum** (like `SdfOpCodes.g.h`) so C++, GLSL, and the
  recipe-output declaration all reference ONE list. `kChannelSpecs` is **lifted into the schema** (it
  already has exactly `ChannelDesc`'s shape — it's just hardcoded rather than declared).

**Subset-fused output = one method, one DAG walk, requested channels only.**
Given (recipe/graph DAG + requested channel mask): gather the source-node indices per requested channel
(`IMultiOutputNode.GetChannelSourceIndex`), emit ONE method that evaluates the **union sub-DAG once**
(CSE cache spanning the whole subset), returning a result populated for exactly the requested set. Built
as a Family-B logic transpiler by **widening Unity's `EmitNode` CSE cache scope** across the subset,
adding VIXEN GLSL + VIXEN CPU backends. Per kernel-framework skill §3-B this is a 2nd/3rd multi-output
backend → **extract a shared base, don't copy the visitor a 3rd time.**

## 4. Resolved architecture decisions (user, 2026-07-18)

1. **Fusion = respect the already-shared DAG, not invent CSE.** The graph editor's output slots make the
   authored form structurally a shared DAG; the per-variant `_DNC` compile flattens it. Goal: stop
   discarding the sharing.
2. **Home = kernel-framework CORE (shared source).** Migrate out of `com.utility.sdf` into
   `com.yeroket.utility.kernel-framework` so Unity AND VIXEN consume one contract via codegen. AppFlow-style
   split: attribute/contract definitions kernel-owned; schema instances consumer-side.
3. **New program, own brainstorm+plan** (this doc); M11 stays scoped to Cornell lighting.
4. **Channel vocabulary DERIVES FROM the content-config** dynamic-voxel-shape mechanism — not a hand-merged
   enum. One content-config drives both storage and output.
5. **Fusion reuses the existing mechanism** (Unity `EmitNode` CSE cache, scope-widened) rather than a
   from-scratch mode; new backends via a shared base.
6. **Vocabulary shape = ONE UNION LIST with per-entry role flags** (§3) — resolves the scout's one open
   risk (storage-vs-derived-output vocab divergence + FieldKind/elemCount having no output analog).

## 5. Increment decomposition (scout-proposed, accepted — to be planned)

1. **Contract-migration** — lift `SDFOutputChannel`/`SDFOutputNode`/`IMultiOutputNode`/`SDFVariantResult`/
   `SDFOutputSlot` from `com.utility.sdf` into kernel-core (domain-agnostic; AppFlow-style split). Lowest
   risk, pure move; unblocks the rest.
2. **Channel-vocab-unification** — union-list-with-role-flags schema in content-config; codegen-emit
   `SemanticId` (mirror-enum); lift `kChannelSpecs` to the schema; make `ChannelDesc` population and
   recipe-output requests both read projections of the one list. (Carries decision #6, now resolved.)
3. **Fusion-codegen** — new Family-B fused-emit: subset-in → one method walking the shared DAG once (CSE
   cache spanning the subset). Widen Unity's `EmitNode` cache scope; add VIXEN GLSL + CPU backends via a
   shared base (§3-B).
4. **Emission-first-consumer** — wire a real VIXEN recipe/body to request a subset (e.g.
   `{roughness, brightness}` / `{emission}`) and consume the fused method end-to-end; parity-check fused
   multi-output vs N-separate against VIXEN's ~91-opcode harness; retire the light-tree side-bake once
   emission flows natively.

**Sequence rationale:** 1 unblocks the type moves; 2 must precede 3 (fusion needs the channel list to know
what a "subset" is); 4 validates. Cross-repo (Yeroket + VIXEN) throughout — follow the codegen boundary
(`kernel-framework` skill §7): schema-side edits only, never hand-edit `.g.*`, drift-guards on, dotnet-only
regen with the non-deterministic `SDFNodeGenerator.dll` committed only on real source change.

## 6. Biggest risks / open questions for the plan

- **Cross-repo drift + regen discipline** — this touches Yeroket kernel-core AND VIXEN. The
  `SDFNodeGenerator.dll` non-determinism + vendored-`.g.*`-verbatim rules (skill §10) are load-bearing.
- **Fusion correctness oracle** — fused multi-output MUST be byte/numerically identical to N-separate
  single-output evals. The parity harness is the gate; a fused-vs-separate mismatch is the failure mode.
  Beware the circular-oracle trap (skill §9 / the Pyramid lesson): validate outputs against an INDEPENDENT
  reference, not the fused-vs-its-own-flatten.
- **DerivedOutput channels (Normal/AO/Thickness)** — these have no storage and are computed from the
  distance field; the fused emit must handle "derive from another channel's result" within the single walk,
  not just tap stored values. Confirm the derive-dependency (Normal needs Distance) threads through the
  subset selection.
- **Does the union-list projection actually round-trip `ChannelDesc` byte-identically?** Increment 2 must
  prove the storage-side generated `OctreeConfig.g.h`/`.glsl` are unchanged (or intentionally changed) when
  `ChannelDesc` becomes a projection — a drift-guard byte-compare is the check.

## 7. Not in scope (deferred)

- M11.2 (self-lit emissive panel) — independent, HELD pending this program's emission-authoring shape.
- The procedural→GI bridge for a live (non-baked) recipe emissive body feeding ReSTIR/DDGI — the light-tree
  currently requires a baked octree+MipPool. Whether native emission reaches GI without a bake is a
  DOWNSTREAM consumer problem (a later increment or its own scoping), NOT part of this contract+codegen
  program. Flagged so increment 4 doesn't silently inherit it.
