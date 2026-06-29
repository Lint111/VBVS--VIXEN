---
title: SDF Recipe Format via Shared Kernel-Codegen Framework (Inc4 Materialization foundation)
status: Design DRAFT 2026-06-26 — brainstormed, pending user review. Supersedes the "Inc4 Materialization" sketch in the Content-Format Contract (§8.4).
date: 2026-06-26
tags: [architecture, voxel, sdf, content-format, recipe, kernel-framework, codegen, materialization, cross-project]
aliases: [Recipe Format, SDF Instruction Recipe, Kernel Codegen Framework, Inc4 Materialization]
related:
  - "[[Voxel-Content-Format-Contract-Design-2026-06]]"
  - "[[Voxel-MultiChannel-Stored-Inc-2026-06-Design]]"
  - "[[vixen-owns-content-format-not-consumer]]"
---

# SDF Recipe Format via a Shared Kernel-Codegen Framework

> **TL;DR.** Recipe-authored content (the Procedural provider's input, and Materialization's
> bake input) gets a *proper injectable format* — the **`SDFInstruction[]` opcode stream** from
> the user's Yeroket "kernel framework". Rather than VIXEN copying it, we **extract the kernel
> framework's generic core into a standalone shared package** and make both Unity and VIXEN
> *consumers* of it. The core is a **single-source, multi-backend code generator**: one canonical
> opcode/kernel definition; each consumer's **manifest** declares which backends to emit
> (Unity → `{C#, Burst, HLSL}`, VIXEN → `{C++, HLSL}`). Generic opcodes live in the core;
> consumers declare their own without cross-contamination. VIXEN ingests **HLSL natively** (glslang
> already underpins runtime compilation — one source-language flag + binding mapping), so it reuses
> the existing HLSL emitter and builds only **one** new backend (C++). Delivered as a walking
> skeleton first, then the catalog, then Materialization.

---

## 1. Origin & goal

The immediate goal was **Increment 4 — Materialization** (Procedural→Stored bake on edit), and within
it, *"a proper format for recipe-based authored content to be injected, like we have for baked
content."* Baked/Stored content already has an injectable format (the Inc3 schema-derived
multi-channel SoA channel pool + `OctreeConfig` descriptor). Recipe content did **not** — it was a
hardcoded 2-entry enum (`RECIPE_SPHERE`, `RECIPE_DISPLACED_SPHERE`) + a fixed 3-float `RecipeParams`,
hand-mirrored across `SdfRecipes.h` ↔ `SdfRecipes.glsl`.

During brainstorming the goal widened (user direction): the **Yeroket kernel framework already solves
"author an SDF once, evaluate it identically on CPU and GPU"** — it is exactly the kind of generic
utility that should be *reused*, not reinvented. The work is therefore (a) make that framework a
generic, multi-platform foundation with **Unity isolated as its own consumer**, and (b) make **VIXEN a
second consumer** — which then delivers the recipe format and Materialization on top.

**Ownership invariant (unchanged):** VIXEN owns *its* content format; the format represents engine
concepts, never consumer-game ones (see [[vixen-owns-content-format-not-consumer]]). Here that is
honored by the **layered-opcode** rule below: engine-generic opcodes in the shared core; game-specific
opcodes (plant/turtle/MC) stay in their consumer and never enter VIXEN's vocabulary.

## 2. Decisions (with rationale)

| # | Decision | Rationale |
|---|---|---|
| D1 | **Scope = full edit loop** (format → inject → procedural render → bake → edit→re-materialize). | User chose the complete Materialization chain, not a partial format. |
| D2 | **Representation = the kernel framework's `SDFInstruction` opcode stream** (not a new VM). | A battle-tested, blittable, serializable runtime program already exists; reuse beats reinvention. |
| D3 | **Reuse model = single-source codegen, multiple backends** (Option 3). | One canonical opcode/kernel definition; each platform's evaluator is *generated*, bridging the C#/C++ boundary by generation (not linking) and eliminating hand-mirror drift. |
| D4 | **Per-consumer emitter manifest + layered opcodes.** | Unity declares `{C#,Burst,HLSL}`, VIXEN `{C++,HLSL}`. Generic opcodes in core; consumers add their own with no cross-contamination. Enables "migrate a Unity toolset to VIXEN ≈ one include + one config". |
| D5 | **Core lives in a standalone shared package/repo; Unity + VIXEN are equal consumers.** | Cleanest "generic utility framework"; clear ownership; Yeroket migrates to consume it. |
| D6 | **The format itself (`SDFInstruction`/`SDFOpCode`) is a generated artifact.** | Byte-compat across C#/C++/HLSL becomes automatic; the 144-value hand-mirror drift risk disappears. |
| D7 | **VIXEN ingests HLSL natively** (reuse Unity's HLSL emitter); only **C++** is a new backend. | VIXEN's runtime shader path is glslang, which has an HLSL frontend — a source-language flag + binding mapping, far cheaper than a GLSL emitter. |
| D8 | **Canonical authoring stays C#; generated C++/HLSL are committed artifacts.** | VIXEN's normal build stays .NET-free; only *regeneration* needs `dotnet` (mirrors how Yeroket commits its generated `.compute`/DLL). |
| D9 | **GPU procedural render = Compile realization (generated HLSL); GPU bytecode VM deferred.** | With cheap HLSL, generating a specialized eval shader per recipe is the simplest fast path. The edit loop re-bakes via the CPU interpreter, so nothing needs a GPU VM yet. |

## 3. Architecture

### 3.1 One source, many backends, per-consumer manifest

A kernel/opcode's math is authored **once** (C# `[VMKernel]`/`[KernelCallable]`-style body). The
generator transpiles that one body into each requested backend. A **consumer manifest** declares the
emitter set + output dirs + which opcode layers to include:

```
# core (shared)            opcodes: Sphere, Box, Union, SmoothUnion, Subtract, Transform, math…
# unity-consumer.manifest  emitters: [CSharpBurst, HLSL]   opcodes: core + {plant, turtle, MC…}
# vixen-consumer.manifest   emitters: [Cpp, HLSL]           opcodes: core + {…VIXEN-only later}
```

### 3.2 Layered opcodes (no cross-contamination)

`SDFOpCode` is **partitioned by layer**: a stable core range (engine-generic geometry/CSG/math) plus
reserved per-consumer ranges. VIXEN's blobs use `core + VIXEN` opcodes; Unity's use `core + Unity`.
Generic content is portable; game-specific opcodes never leak into another consumer's vocabulary.

### 3.3 The format is generated → byte-compat is free

`SDFInstruction` (132-byte blittable: `OpCode:byte`, `InputMask`, `ParamMask`, pad, `float4 Data0..7`)
and `SDFOpCode` are emitted from one definition into C# / C++ / HLSL. The interchange artifact is the
**compiled `SDFInstruction[]` blob** (the GPU SSBO repacks to std430 on upload, as the channel pool
already does).

### 3.4 Three realizations of one recipe (generalizes the Provider model)

| Realization | How | Provider | Status this program |
|---|---|---|---|
| **Bake** | CPU-interpret recipe over a grid → Inc3 Stored SoA pool | Materialized→Stored | **built** (P3) |
| **Compile** | recipe → generated HLSL eval → glslang→SPIR-V → specialized compute | Procedural (compiled) | **built P2** (primary GPU path) |
| **Interpret (GPU VM)** | inject `SDFInstruction[]` as data; HLSL stack-VM, no recompile | Procedural (dynamic) | **deferred** (P4) |

Shared downstream shading is unchanged: all realizations feed one iso-surface + gradient-normal +
lighting path; only field *evaluation* differs.

### 3.5 VIXEN HLSL ingestion

`ShaderManagement/ShaderCompiler.cpp` already compiles source→SPIR-V via **glslang** at runtime with a
cache (`ShaderCompilationCacher`/`ShaderModuleCacher`); it is GLSL-only by one line
(`setEnvInput(glslang::EShSourceGlsl, …)`, `ShaderCompiler.cpp:117`). Adding an HLSL path = select
`EShSourceHlsl` + HLSL→Vulkan resource binding mapping (cleanest when the emitted HLSL carries
`[[vk::binding(set,binding)]]` attributes). Recipe rendering becomes a **new HLSL compute path**
alongside the existing GLSL stored/binary raymarch — the working GLSL path is untouched.

## 4. Current-state findings (verified) — sizing the work

- **Format is real & portable.** `SDFInstruction`/`SDFOpCode` confirmed at
  `Yeroket-Fantasy/Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs` (blittable, 132 B,
  ~144 opcodes incl. game-specific plant/turtle/MC — these are the ones the core must *exclude*).
- **CPU evaluator exists** (`com.utility.sdf/Runtime/Burst/SDFCompiledEvaluator.cs`) — a runtime
  stack-machine; its semantics are the reference for VIXEN's C++ interpreter.
- **⚠ Opcode math is NOT yet single-source through the generic transpiler.** Primitives are plain C#
  statics (`SDFPrimitives.Sphere`, used by `SDFPrimitiveNode.cs:209`); the node separately emits
  `SDFInstruction` (`:258`); HLSL comes from a *separate* generated file
  (`com.utility.sdf/Runtime/GPU/Generated/SDFGraphEval.hlsl`) — **no `[KernelCallable]`/`[VMKernel]` on
  the primitives.** So **P1 must first route opcode math through the transpiler** before backends can be
  added. Exact current SDF→HLSL generation path to be confirmed in P0.
- **Generator is pure Roslyn/.NET, testable without Unity** (`SourceGenerator~/Tests`, `dotnet test`) —
  the C++/HLSL emitter work has a clean golden-file + compile-and-run-conformance loop, no Unity in the
  cycle. This is the key de-risk for D3.
- **VIXEN shader compiler = glslang at runtime** (confirmed, §3.5) — D7 is feasible.

## 5. Decomposition

This is a multi-subsystem **program across repos**, not one increment. Each phase gets its own
spec→plan→implement cycle; this doc is the umbrella + the P0 spec.

| Phase | Deliverable | Repo |
|---|---|---|
| **P0** | **Walking skeleton** — `Sphere`+`Union` end-to-end (§6). Proves the whole pipeline. | shared-core + VIXEN |
| **P1** | Generator productionization: route opcode math through the transpiler; **C++ emitter**; per-consumer manifest; opcode layering; conformance-vector export. | shared-core (.NET) |
| **P2** | Core opcode catalog (geometry/CSG/math) generated to C++/HLSL; VIXEN **C++ bake interpreter** + **Compile** render path; HLSL ingestion hardened (binding mapping). | shared-core + VIXEN |
| **P3** | **VIXEN Materialization + edit loop** (original Inc4): bake recipe→Stored pool; procedural render; edit→re-materialize via the `AttributeRegistry` observer hook. | VIXEN |
| **P4** | *Deferred:* GPU bytecode VM (dynamic recipes); VIXEN-declared opcodes (authored per-voxel material/channels); deep Unity dispatch/scheduling isolation; "1 include + 1 config" migration ergonomics; perf. | both |

## 6. P0 — Walking skeleton (the first spec)

**Purpose:** prove the entire pipeline on the minimum content before porting the catalog or building
Materialization. Measures the real cost of the §4 ⚠ finding.

**Content:** two opcodes — `Sphere` (leaf) and `Union` (binary op). Enough to exercise stack
push/pop + a combinator.

**Steps (each its own gate):**
1. **Define once.** Express `Sphere` + `Union` math as transpiler-consumable C# (`[VMKernel]`/
   `[KernelCallable]`). Confirms how much rework the §4 finding implies on two opcodes.
2. **Emit format + math.** Extend the generator with a minimal **C++ emitter** (and confirm the
   existing **HLSL emitter** covers these); generate the `SDFInstruction`/`SDFOpCode` mirrors + the two
   evaluators in C++ and HLSL. Golden-file emit test (`dotnet test`, no Unity).
3. **Export golden vectors.** The C# reference evaluates a 2-instruction recipe (sphere ∪ sphere) at N
   sample points → expected distances, committed as the conformance fixture.
4. **VIXEN CPU eval.** Compile the generated C++; evaluate the same recipe; **assert parity** vs the
   golden vectors (gtest).
5. **VIXEN GPU render.** Add the `EShSourceHlsl` path to `ShaderCompiler`; compile the generated HLSL
   eval into a compute shader; render the recipe (sphere ∪ sphere) offscreen on lavapipe.
6. **Live gate (authoritative).** Controller reads the PNG: one smooth blended blob; CPU↔GPU agree.

**Done = a single thread through manifest → C++ + HLSL emit → VIXEN C++ eval + HLSL render →
conformance + live gate.** Everything after is "add opcodes" (P2) and "build Materialization" (P3),
both incremental.

## 7. Testing & parity strategy

- **Conformance vectors** exported from the C# reference are the cross-language source of truth (C# ↔
  VIXEN-C++ ↔ VIXEN-HLSL all pinned to them). Generated → drift is structurally prevented; vectors
  catch semantic regressions.
- **Golden-file emit tests** for the generator (input kernel → expected C++/HLSL), pure `dotnet test`.
- **Live offscreen gate** (lavapipe) is authoritative for any render result, per the project rule.
- **No-regression:** existing GLSL stored/binary raymarch + Inc3 multi-channel render + binary path
  untouched and re-verified.

## 8. Risks

- **P1 is bigger than "add two visitors"** — it must first make opcode math single-source (§4 ⚠).
  *Mitigation:* P0 measures it on two opcodes before commitment.
- **glslang HLSL frontend gaps** vs DXC for exotic features. *Mitigation:* generated SDF eval is plain
  math + StructuredBuffer + numthreads (low risk); emit Vulkan-binding-attributed HLSL.
- **Two shader languages in VIXEN** (GLSL legacy + HLSL recipes). *Mitigation:* recipe render is a
  separate compute path; no mixing in a translation unit.
- **Cross-repo coordination + core extraction churn** to existing Yeroket consumers. *Mitigation:*
  extraction is additive; Yeroket migrates to consume; deep dispatch isolation deferred to P4.
- **Per-voxel material** is out of the distance-only format. *Mitigation:* recipe authors geometry this
  program; `color`/`roughness` stay synthesized (as today) until VIXEN-declared material opcodes (P4).

## 9. Rejected alternatives

- **VIXEN copies the format + hand-writes interpreters** — drift on 144 opcodes; abandons the shared
  foundation; rejected for single-source codegen (D3/D6).
- **Native C/C++ core + bindings** (Unity P/Invokes a native core) — large inversion that subordinates
  the mature C# framework and risks every Yeroket consumer; rejected for codegen (D3).
- **Build a GLSL emitter for VIXEN** — redundant with reusing the HLSL emitter + glslang HLSL ingest
  (D7).
- **Strict byte-compat via hand-mirrored enum** (earlier choice) — superseded: generating the format
  (D6) gives byte-compat *without* hand-mirroring.

## 10. Open items (for review / later phases)

- Name + location of the standalone shared-core repo/package; how VIXEN references it for regeneration.
- Opcode-layer numbering scheme (core range + reserved consumer ranges) — pin in P1.
- Confirm the exact current SDF→HLSL generation path (P0 step 1).
- Whether the GPU bytecode VM (P4) is ever needed, or Compile + Bake fully cover the edit loop.
