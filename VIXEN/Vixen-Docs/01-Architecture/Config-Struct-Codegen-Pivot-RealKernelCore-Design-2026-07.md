---
title: Config-Struct Codegen — PIVOT to the real kernel-codegen core
status: DESIGN / awaiting review · SUPERSEDES the parallel-tool approach
date: 2026-07-02
tags: [architecture, codegen, kernel-framework, D5, pivot, cross-repo]
supersedes:
  - "[[Config-Struct-Codegen-Standalone-Tool-Design-2026-07]] (parallel Vixen.Codegen tool)"
  - "[[Config-Struct-Codegen-P1-Design-2026-07]] / [[Config-Struct-Codegen-P1-Plan-2026-07]]"
related:
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]] (D5, D8)"
  - "[[runtime-kernel-pipeline-direction]]"
---

# Config-Struct Codegen — PIVOT to the real kernel-codegen core

**Why this doc:** P0 shipped a **parallel** `Vixen.Codegen` tool with **new** attributes (`[GpuStruct]` etc.) that did **not** use Yeroket's actual kernel-codegen functionality. That contradicts the steer — *"the tool must carry the kernel functionality with attributes so artifacts are consistent across all consumers."* **User decision (2026-07-02): pivot** — the config-struct codegen becomes part of the **real, extracted kernel-codegen core**, invoked as a **callable console tool in the Yeroket repo**. P0's std430/emitter *logic* is **re-homed** into that core; the parallel `Vixen.Codegen` is **removed**.

## Decisions (locked, user 2026-07-02)

| # | Decision |
|---|---|
| V1 | **One shared codegen core = Yeroket's real emitters** (`CppEmitter`, the `RecipeContainer`-style struct emission, `MappingTables`) + a **new generic POD-struct emitter** (C++ + GLSL) added to that core. No parallel system. |
| V2 | **Invocation = a callable console tool in the Yeroket repo** (`dotnet run`), extracted from the source-gen's emitter core. It loads a canonical `.cs` via a Roslyn `Compilation`, runs the emitters, writes artifacts. (Matches "local functioning pre-build tool + callable tool".) |
| V3 | **`[GpuStruct]`, `[GpuArray]`, `Float3`, `Mat4` live in the kernel framework's `Runtime/` attributes** (beside `[KernelBlackboardLayout]`) — shared, so every consumer's structs use the same vocabulary. |
| V4 | **Config canonical (OctreeConfig) lives VIXEN-side** (not in Yeroket). VIXEN's regen invokes the Yeroket tool on VIXEN's schema dir. |
| V5 | **Backend-idiomatic rendering (Option C):** C++ = scalars + `ChannelDesc` + `glm::mat4`; GLSL = `vec3`/`mat4`/`uvec4`. One std430 model; C++ emitter inserts explicit offset-driven padding. → P2 migration is a pure `#include`-swap. |
| V6 | **D8 preserved:** generated C++/GLSL are committed artifacts; **normal VIXEN builds stay .NET-free AND Yeroket-free** (they compile the committed `.g.h`/`.glsl`). Only **regen** needs `dotnet` + the Yeroket tool present (dev-time; gated, like the recipe-container regen). |
| V7 | **Yeroket + Unity migrate to the same core later** (full D5); C#/Python emitters are future backends. |

## Current-state mechanics (verified 2026-07-02)
- Yeroket source-gen is a Roslyn analyzer gated on `compilation.AssemblyName == "com.utility.sdf"`; the recipe container's canonical (`SDFInstruction`) lives in Yeroket; the `.g.h` is **manually vendored** into VIXEN (no auto-sync). Regen is dotnet-driven (`CSharpGeneratorDriver` in tests; emitters expose direct `Build*` methods returning raw backend text — e.g. `CppEmitter.BuildCppHeader`).
- So the extraction is real work: the emitter core must be usable **outside** the assembly-name-gated analyzer path — a console entry that builds a `Compilation` from an arbitrary schema dir and calls the emitters directly.

## Architecture

```
Yeroket repo (the shared kernel-codegen core + tool)
  Runtime/ …Attributes  : [VMKernel] [KernelBlackboardLayout] … + NEW [GpuStruct] [GpuArray] Float3 Mat4
  SourceGenerator~/     : CppEmitter, RecipeContainerEmitter, MappingTables  (existing)
        + NEW: GpuStructModel (std430; re-homes P0 logic) + GpuStructCppEmitter + GpuStructGlslEmitter
  CodegenTool~/ (NEW)   : console `Main` — --schema <dir> --struct <Name> --out-cpp --out-glsl [--check]
                          builds a Roslyn Compilation over the schema, runs the emitters, writes/checks.
                          │  dotnet run   (regen-time only; committed artifacts serve normal builds)
VIXEN repo
  codegen/schemas/OctreeConfig.cs  [GpuStruct]   (config, VIXEN-side; refs the kernel-framework attrs)
     → OctreeConfig.g.h (C++, SVO/include) + OctreeConfig.glsl (shaders/)  ← committed, VERBATIM
  CMake regen target: dotnet-gated AND Yeroket-path-gated (VIXEN_YEROKET_DIR); golden --check gate.
```

### Re-homing P0
P0's `StructModel` (std430) + `CppStructEmitter` (offset-driven pad) + `GlslStructEmitter` are correct and portable. The pivot moves that logic into the Yeroket core as `GpuStructModel`/`GpuStruct{Cpp,Glsl}Emitter`, reusing `CppEmitter`/`MappingTables` type helpers where they overlap. P1's Option-C extensions (Float3/Mat4/nested/array/idiomatic) are authored there.

### Cross-repo regen wiring (V6)
VIXEN's `codegen/CMakeLists.txt`: `find_program(dotnet)` AND a `VIXEN_YEROKET_DIR` (path to the Yeroket checkout, or a cached/published tool). If either is absent → `return()` (skip regen/golden; committed artifacts used as-is). When present, the golden `codegen_check` runs the Yeroket tool `--check` against the committed artifacts. This mirrors the recipe-container regen (dev-time, both repos present) and keeps normal builds hermetic.

## Phasing (each its own plan + gate)

| Phase | Repo | Deliverable |
|---|---|---|
| **A — extract core + tool (redo P0 on the real core)** | Yeroket | `[GpuStruct]`/`[GpuArray]`/`Float3`/`Mat4` in `Runtime/`; `GpuStructModel` + C++/GLSL emitters in the core (re-home P0 logic); the `CodegenTool~` console tool; a `SkeletonConfig` proof + emitter golden tests (`dotnet test`). |
| **B — OctreeConfig canonical + parity (redo P1 on the real core)** | VIXEN | `codegen/schemas/OctreeConfig.cs` + `ChannelDesc` (`[GpuStruct]`, Option-C shapes); invoke the Yeroket tool → `OctreeConfig.g.h`/`.glsl`; CMake golden gate (dotnet+Yeroket gated); parity test vs today's `Vixen::SVO::OctreeConfig` (432 layout). |
| **C — migrate + live gate** | VIXEN | `ShellOctreeGpu.h` `#include`s the generated C++ (drop-in); `BodyInstanceRayMarch.comp` `#include`s the generated GLSL; drift-guard on the migrated shader; **live lavapipe render no-regression**. |
| **Cleanup** | VIXEN | remove the parallel `VIXEN/codegen/Vixen.Codegen*` (P0) once B supersedes it; retire the P0/P1 parallel-approach docs (mark superseded). |

## Testing
Yeroket: emitter golden tests (`dotnet test`) for scalar/Float3/Mat4/nested/array + C++ offset-driven pad + idiomatic GLSL. VIXEN: golden `--check` (dev-gated) + compile-time parity static_asserts vs the current struct + the reflection drift-guard on the migrated shader + the live render gate (authoritative).

## Disposition of shipped P0
P0's parallel `Vixen.Codegen` is on `origin/main` (`70165c13`). It stays until Phase B produces the real-core equivalent, then Cleanup removes it (single commit). Its logic is re-homed in Phase A, not lost. The P1 branch (`feat/config-struct-codegen-p1`) is superseded (its Option-C design is carried into this doc).

## Open items for Phase A planning
- Exact extraction boundary: which emitter classes move into a shared library vs stay in `SourceGenerator~` (the console tool + the analyzer both reference the core).
- Console tool CLI + how it builds the `Compilation` (reference the kernel-framework attributes assembly so `[GpuStruct]` resolves).
- How VIXEN locates the tool: `VIXEN_YEROKET_DIR` cache var vs a published tool path.
- Whether Yeroket's existing `RecipeContainerEmitter` should be refactored onto the new generic `GpuStructModel` (DRY) or left as-is for now (YAGNI — defer).
