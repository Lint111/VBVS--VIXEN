---
title: Runtime Kernel Pipeline — Kernel-Codegen ↔ ShaderManagement Unification
status: FUTURE / direction note (not scheduled)
date: 2026-06-29
tags: [architecture, kernel-codegen, shader-management, sdi, mods, future]
---

# Runtime Kernel Pipeline — Codegen ↔ ShaderManagement Unification

> **Status: FUTURE direction note.** Not a plan, not scheduled. Captures *why the
> "kernels are build-time only" ceiling is not fundamental* and what wiring would
> dissolve it. Written after the [[Recipe-Container-Format-Contract-2026-06|recipe authoring→render]]
> epic shipped. Related: [[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design|kernel-codegen (Inc4)]],
> [[ShaderManagement-Library]].

## TL;DR

VIXEN already owns **both halves** of a runtime-kernel pipeline; they are simply
not connected:

- **Kernel-codegen (Yeroket)** — the *content / opcode* layer. Canonical C# kernels
  → source-gen → vendored C++ (`SdfOpCodes.g.h`, `RecipeContainer.g.h`, kernel
  bodies) + the recipe VM that interprets opcode bytecode.
- **ShaderManagement (VIXEN)** — the *runtime shader* layer. `ShaderBundleBuilder`
  (glslang compile, **GLSL and HLSL**) → `SpirvReflector` (SPIRV-Reflect) →
  `SpirvInterfaceGenerator` (typed **SDI** headers + `LAYOUT_HASH`) →
  `SdiRegistryManager` → `SdiDiscoveryScanner` (**startup discovery + runtime
  binding of shader types the engine was never compiled against**).

The "new kernels need an engine recompile" framing is wrong: the recompile is only
required because the codegen output never reaches ShaderManagement. Wire
`codegen → ShaderBundleBuilder → SDI → recipe registry` and a **mod can ship a
kernel as a shader fragment + opcode metadata** that the engine compiles, reflects,
exposes, and dispatches **at load — no engine rebuild**.

## What exists today (verified 2026-06-29)

| Capability | Where | Notes |
|---|---|---|
| Source→SPIR-V at runtime | `ShaderManagement::ShaderCompiler` | glslang; `CompilationOptions::SourceLanguage::{GLSL,HLSL}` — HLSL via glslang's HLSL frontend |
| Full reflection | `SpirvReflector::Reflect` | SPIRV-Reflect; `structDefinitions` carry per-member `offset`/`arrayStride`/`sizeInBytes`; descriptor sets, push constants, spec constants |
| Typed interface emission | `SpirvInterfaceGenerator::Generate` | writes `<uuid>-SDI.h` with a per-struct `LAYOUT_HASH` |
| Interface registry | `SdiRegistryManager` | `SDI_Registry.h`, namespace `Shaders`, UUID + friendly alias |
| Discover + bind unknown types | `SdiDiscoveryScanner` → `UnknownTypeRegistry` | startup scan of `generated/sdi/*-SDI.h`; "register unknown types for runtime binding; notify for promote-to-compile-time" |
| Graph integration | `ShaderLibraryNode` | "replaces manual shader loading", outputs `ShaderDataBundle`; **ctor still `MVP STUB`** — the first-class shader library is unfinished (matches the backlog's "promote runtime reflection to first-class") |
| Content/opcode layer | Yeroket codegen + `RecipeRegistry` + recipe VM | opcode catalogue (`SdfOpCodes.g.h`, ~87 ops), recipe bytecode container (VRC1), runtime dispatch by the compiled VM |

## The two layers (and which is mod-scope)

| Layer | What | Compiled where | Mod-scope today |
|---|---|---|---|
| **Recipes** | compositions of *existing* opcodes (`.vrc` bytecode) | nothing — interpreted at runtime | **Yes — shipped** ([[Recipe-Container-Format-Contract-2026-06]]) |
| **Kernels / opcodes** | the catalogue itself | baked into the GPU shader (`BodyInstanceRayMarch.comp`) + C++ VM | **No today** — a new opcode is new shader code |

The unification turns the second row from "engine rebuild" into "runtime compile +
register", because ShaderManagement can already compile + reflect + bind a shader it
has never seen.

## The gap

Nobody wired codegen output → ShaderManagement. The kernel-codegen emits *vendored
source* consumed at the engine's build time; it never feeds the runtime
`ShaderBundleBuilder`. Closing that is the whole of this direction.

## Proposed shape

```
[mod/content]  kernel HLSL fragment + opcode metadata (.vrc kernel entry)
      │
      ▼
ShaderBundleBuilder.AddStage(HLSL)  →  glslang  →  SPIR-V
      │
      ▼
SpirvReflector  →  SpirvInterfaceGenerator (SDI + LAYOUT_HASH)  →  SdiRegistryManager
      │                                                              │
      ▼                                                              ▼
SdiDiscoveryScanner / UnknownTypeRegistry  ───────────────►  RecipeRegistry + opcode id
      │                                                              │
      ▼                                                              ▼
ShaderLibraryNode (owns the compiled module)  ──────►  recipe VM dispatch (no recompile)
```

Two distinct capabilities fall out, smallest first:

1. **Auto-sync vendoring (build-time DX, no architecture change).** A
   `regenerate-and-sync` step runs the codegen and writes the `.g.h` straight into
   each consumer's vendored dir + asserts byte-identical. Removes the manual
   Yeroket→VIXEN copy hop. *(Independent of the rest; do this first regardless.)*
2. **Single-source the reflected SSBO structs.** Generalize the OctreeConfig
   drift-guard (see below) into generating the C++ binding struct from the SDI
   reflection (the same single-source treatment the recipe container got). Removes
   hand-padded `std430` structs. Blocked-as-a-quick-win by: OctreeConfig is
   shared (ShellOctreeGpu + CashSystem + VoxelSceneCacher) and SDI headers are
   untracked build artifacts → needs a build-ordering decision.
   **Prerequisite found 2026-06-29:** `SpirvReflector` currently surfaces only the
   *top-level* members of a descriptor block — it does NOT recurse into a nested
   struct member (the `OctreeConfig` element of `configs[]`), so neither per-field
   drift-guarding nor struct generation is possible until the reflector recurses
   nested SSBO structs. (That is why the landed drift-guard checks element *size*
   only — see below.) Extending `SpirvReflector::ConvertStructDefinition` to recurse
   is the small enabling change.
3. **Runtime kernel compile path.** codegen kernel fragment →
   `ShaderBundleBuilder` → reflect → SDI → `RecipeRegistry` opcode registration →
   VM dispatch. This is the "register a new opcode at load" capability. Requires
   the kernel fragment to splice into (or dispatch alongside) the raymarch shader,
   plus opcode-namespace allocation for mod-contributed ops.
4. **Mod packs ship kernels.** (3) + a content-trust stance (below) + opcode
   collision/namespacing across packs.

## Mod safety stance (decided 2026-06-29)

**Mods are essentially the user's responsibility** — the same trust model as any
moddable game (Skyrim/Factorio/etc.): installing a mod is consenting to run its
code. Do **not** over-engineer a shader sandbox.

- Provide an **optional virus-scan / AV hook** on pack import (scan the pack file
  before load); surface the result, let the user proceed.
- **Disclose** plainly that kernel-bearing mods execute with engine privileges
  (GPU shader code + the C++-side registration metadata).
- Keep cheap structural guards that protect the *engine*, not police the *mod*:
  validate the recipe container (already done — `RecipeRegistry` opcode/param/stack
  validation), bound resource counts, and reject malformed SPIR-V at
  `ShaderBundleBuilder` compile (glslang already errors on bad input).
- Defer a true sandbox (separate device/process, capability limits) unless a real
  distribution scenario demands it. YAGNI until then.

## Concrete seams (where the wiring lands)

- `ShaderManagement::ShaderBundleBuilder::AddStage(..., HLSL)` → `BuildResult` →
  `ShaderDataBundle.reflectionData` — the runtime compile+reflect entry.
- `SdiDiscoveryScanner` + `UnknownTypeRegistry` — the existing "bind a type we
  weren't compiled against" mechanism a runtime opcode would reuse.
- `RenderGraph ShaderLibraryNode` (finish the `MVP STUB`) — the graph owner of
  runtime-compiled kernels.
- `RecipeRegistry` (`libraries/SVO/include/Recipe/`) + `SdfOpCodes.g.h` — where a
  mod-contributed opcode id registers.
- **OctreeConfig SDI drift-guard** (`test_octree_config_sdi_parity`, landed this
  session) — the pattern for keeping a hand-written binding struct honest against
  the shader's reflected layout; the seed of capability (2). Today it asserts the
  `configs[]` element *size* (432) only, because the reflector doesn't yet expose
  nested struct member offsets (see capability 2 prerequisite).

## Why now / why not

Strategically this is the next leg of the FORMAT→EXECUTION program (see
[[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]]): recipes proved data-driven
*composition*; this extends it to data-driven *primitives*. It is **not urgent** —
recipes already cover the mod-content story for the shipped opcode catalogue. Pick
it up when a consumer (e.g. UNDERTOW modular ships) actually needs modder-authored
primitives. Start with capability (1) any time — it is pure DX upside.
