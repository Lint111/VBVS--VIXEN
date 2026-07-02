---
title: Config-Struct Codegen — Phase B Design (real OctreeConfig on the Yeroket core)
status: DESIGN / awaiting review
date: 2026-07-02
tags: [architecture, codegen, octreeconfig, std430, D5, D8, yeroket-kernel-core]
related:
  - "[[Config-Struct-Codegen-Pivot-RealKernelCore-Design-2026-07]]"
  - "[[Config-Struct-Codegen-PhaseA-Plan-2026-07]]"
  - "[[Config-Struct-Codegen-P1-Design-2026-07]]"
---

# Config-Struct Codegen — Phase B Design

**Goal:** generate the real `OctreeConfig` (432 B) as **byte-identical C++ and GLSL** from one canonical C# `[GpuStruct]`, rendered **backend-idiomatically**, matching today's hand-written `Vixen::SVO::OctreeConfig` exactly — so Phase C migration is a pure `#include`-swap with zero access-site churn.

**Builds on:** Phase A (MERGED to Yeroket main `f5d10c26`) shipped the Yeroket kernel-codegen core for **scalar** `[GpuStruct]`s: the `[GpuStruct]`/`[GpuArray]`/`Float3`/`Mat4` attributes (`Yeroket.Util.KernelFramework`), the std430 scalar `GpuStructModel`, the C++/GLSL emitters (`SourceGenerator~/Transpiler/GpuStruct*.cs`, ns `Yeroket.KernelFramework.Codegen`), and the `CodegenTool~` console CLI (generate/`--check`). The `Float3`/`Mat4` markers already exist as stubs; Phase B makes the **model + emitters + loader** interpret them and the non-scalar shapes `OctreeConfig` needs.

**Re-homes P1:** the identical non-scalar design was already approved as **P1** ([[Config-Struct-Codegen-P1-Design-2026-07]], user chose *Option C — backend-idiomatic*, 2026-07-02) and **already implemented** against the now-retired parallel `Vixen.Codegen` tool on branch `feat/config-struct-codegen-p1` (commits `bd7bf37b` layout / `52f3932b` C++ emitter / `591fcaa9` GLSL emitter / `fe8cdb89` loader nested-resolution + P1a unit tests). Phase B **ports that proven P1a code onto the Yeroket core**, then defines the canonical `OctreeConfig` and proves parity — exactly as Phase A re-homed P0.

## Decision recap — Option C (backend-idiomatic)

One canonical struct; each emitter renders idiomatically but byte-compatibly:

| Canonical (logical) | C++ rendering | GLSL rendering | std430 |
|---|---|---|---|
| `uint`/`int`/`float` | `uint32_t`/`int32_t`/`float` | `uint`/`int`/`float` | align 4, size 4 |
| `Float3 name` | `float nameX, nameY, nameZ;` | `vec3 name;` | align 16, size 12 |
| `Mat4 name` | `glm::mat4 name;` | `mat4 name;` | align 16, size 64 |
| nested `[GpuStruct]` of 4 scalars as `[GpuArray(N)] T name` | `T name[N];` (emit nested struct too) | `uvec4/ivec4/vec4 name[N];` (4-scalar ⇒ vecN collapse) | elem align 16, stride 16 |
| `[GpuArray(N)] uint name` (explicit pad / scalar array) | `uint32_t name[N];` | `uint name[N];` | elem align 4, stride 4 (roundUp by std430 array rules) |

**Consequence:** the generated C++ is drop-in for today's `Vixen::SVO::OctreeConfig` (scalar grid components + `ChannelDesc`); the generated GLSL matches the current shader struct (`vec3 gridMin`, `uvec4 channels[8]`).

## Architecture

### B1 (Yeroket) — non-scalar layout in the kernel core

Extend three files under `Packages/com.yeroket.utility.kernel-framework/`, porting the P1a logic:

- **`SourceGenerator~/Transpiler/GpuStructModel.cs`** — extend the layout beyond scalars. Add a `FieldKind` discriminator: `Scalar` (existing), `Float3` (align 16, size 12), `Mat4` (align 16, size 64), `NestedStruct` (recursively modeled: align = max member align, size = roundUp(lastEnd, align)), `Array{elem, count}` (element stride = roundUp(elemSize, elemAlign); total = stride × count). Offsets follow std430: `offset = roundUp(cursor, align); cursor = offset + size`; struct size = roundUp(cursor, structAlign). Output stays a flat `(logicalField, offset, size, kind)` list — the single source of every offset.
- **`SourceGenerator~/Transpiler/GpuStructCppEmitter.cs`** — offset-driven, explicit-pad. Walk the model; for a field at offset `O`, if `cursor < O` emit `uint8_t _padN[O - cursor];` then the field; `cursor = O + size`. Render per the table (`Float3`→3 suffixed scalars, `Mat4`→`glm::mat4` with a `#include <glm/glm.hpp>`, nested→the nested struct definition, array→`T name[N]`). Emit the `sizeof` + per-field `offsetof` `static_assert` battery (compile-time self-check). Keep LF-normalization + fail-loud default arm.
- **`SourceGenerator~/Transpiler/GpuStructGlslEmitter.cs`** — idiomatic std430. Render `Float3`→`vec3`, `Mat4`→`mat4`, a 4-scalar nested struct→`uvec4`/`ivec4`/`vec4` (else emit the nested GLSL struct), array→`T name[N]`. Rely on std430 auto-alignment; correctness verified downstream (Phase C drift-guard) and by the model.
- **`CodegenTool~/Program.cs`** — the loader resolves **nested `[GpuStruct]` field types** and **`[GpuArray(N)]`** on fields into the model. (The `--struct <Name>` selector already landed in Phase A A2; only the nested/array resolution is new.)

**Interfaces produced (consumed by B2):** the `CodegenTool` CLI, unchanged in shape (`--schema <dir> --struct <Name> --out-cpp <p> --out-glsl <p> [--check]`), now accepts schemas containing `Float3`/`Mat4`/nested-`[GpuStruct]`/`[GpuArray]`.

**Isolation & landing:** implement in an isolated Yeroket worktree; on validation, **merge to Yeroket main** (like Phase A) so B2 invokes the tool from a clean main.

### B2 (VIXEN) — canonical OctreeConfig + generate + parity

- **`codegen/schemas/OctreeConfig.cs` + `codegen/schemas/ChannelDesc.cs`** — the canonical schema authored VIXEN-side (D5/D8) using the shared Yeroket attributes (`Yeroket.Util.KernelFramework`). `ChannelDesc` = `[GpuStruct] { uint semanticId, elemCount, channelBaseFloats, fieldKind; }` (⇒ 1 uvec4). `OctreeConfig` mirrors the current 432-B layout (full field list below).
- **Generated artifacts (committed golden, consumer-adjacent so Phase C is a pure include-swap):** `libraries/SVO/include/Generated/OctreeConfig.g.h` (namespace `Vixen::Gpu`) and `shaders/Generated/OctreeConfig.glsl`. Produced by invoking the merged Yeroket `CodegenTool` against `codegen/schemas`.
- **CMake dotnet-gated golden gate** — a target that invokes the Yeroket `CodegenTool --check` against the committed artifacts; **active only when `dotnet` + a Yeroket checkout are available** (cache var `YEROKET_ROOT`, or auto-probe `~/Github/Yeroket-Fantasy`); **skipped otherwise** so normal VIXEN builds stay .NET-free (D8). Mirrors P0's dotnet-gated gate, retargeted at the Yeroket tool.
- **C++ parity gtest** — `libraries/SVO/tests/test_octreeconfig_codegen_parity.cpp`: `#include` both the generated `OctreeConfig.g.h` and `ShellOctreeGpu.h`, then `static_assert`/`EXPECT_EQ` `sizeof(Vixen::Gpu::OctreeConfig) == sizeof(Vixen::SVO::OctreeConfig) == 432` and per-field `offsetof` parity for every identically-named field (`localToWorld@64`, `worldToLocal@128`, `nodeArrayBase@192`, `brickArrayBase@196`, `formatId@200`, `brickStrideFloats@216`, `channels@224`, …). This proves the canonical regenerates today's exact layout.
- **Compile-smokes:** g++ `-fsyntax-only` on `OctreeConfig.g.h`; glslc on `OctreeConfig.glsl` when a Vulkan SDK is present (else logged-skip, per Phase A).

**Landing:** B2 code goes on the VIXEN `feat/config-codegen` branch (with the pivot docs); the whole VIXEN side merges to VIXEN main after Phase C + Cleanup.

## Canonical `OctreeConfig` (target — exact field list)

Mirrors the current `Vixen::SVO::OctreeConfig` (see `libraries/SVO/include/ShellOctreeGpu.h`), which the generated struct must reproduce offset-for-offset:

```
[GpuStruct] OctreeConfig:
  int   esvoMaxScale, userMaxLevels, brickDepthLevels, brickSize       // 0..15
  int   minESVOScale, brickESVOScale, bricksPerAxis, _padding1          // 16..31
  Float3 gridMin;  (+ std430 vec3→16 pad)                               // 32..47
  Float3 gridMax;  (+ pad)                                              // 48..63
  Mat4  localToWorld                                                    // 64..127
  Mat4  worldToLocal                                                    // 128..191
  int   nodeArrayBase, brickArrayBase                                   // 192..199
  uint  formatId, bricksPerAxisSdf, poolBrickBase, channelCount         // 200..215
  uint  brickStrideFloats, _padChannels                                 // 216..223
  [GpuArray(8)]  ChannelDesc channels                                   // 224..351 (16 B each)
  [GpuArray(20)] uint _tailPad                                          // 352..431
  // sizeof == 432
```

`kMaxChannels = 8` and the `ChannelDesc` shape are pinned by `VoxelChannelFormat.h`; the canonical must match. (`CashSystem::OctreeConfig` is a *separate* struct — untouched.)

## Testing & gates

- **B1 gate (controller-run):** `~/.dotnet/dotnet test CodegenTool~/Tests` green, including ported P1a synthetic-struct tests — `CompoundLayoutTests` (offsets/sizes for `Float3`/`Mat4`/nested/array), `CppCompoundEmitTests` (explicit-pad insertion + renderings + static_assert battery), `GlslCompoundEmitTests` (idiomatic renderings), `LoaderNestedTests` (nested `[GpuStruct]`+`[GpuArray]` resolution). NUnit, matching the Phase A test project.
- **B2 gate (controller-run):** golden `--check` clean + tamper bites; the C++ parity gtest green (built via the VIXEN test suite); g++ smoke pass; glslc pass-or-logged-skip.
- **Live-run gate:** not required in Phase B (no shader/runtime change yet) — deferred to Phase C.

## Port sources (P1 branch — read-only reference)

Branch `feat/config-struct-codegen-p1`: `codegen/Vixen.Codegen/Layout/StructModel.cs` (+105), `Emit/CppStructEmitter.cs` (+123), `Emit/GlslStructEmitter.cs` (+109), `CompilationLoader.cs` (+2, nested resolution), and tests `CompoundLayoutTests.cs`/`CppCompoundEmitTests.cs`/`GlslCompoundEmitTests.cs`/`LoaderNestedTests.cs`. Adapt namespaces to `Yeroket.KernelFramework.Codegen`; fold loader resolution into `CodegenTool~/Program.cs`; NUnit-ize tests.

## Global constraints

- `Microsoft.CodeAnalysis.CSharp` **4.3.0**; **NUnit**; net8.0 runnable / netstandard2.0 analyzer.
- Emitter output **LF-normalized** (`.Replace("\r\n","\n")`).
- `~/.dotnet/dotnet` only. Commit trailers on every commit:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` / `Claude-Session: https://claude.ai/code/session_01FyfX5aZWhF1kakkUE98u4c`.
- Do NOT hand-edit committed `.g.*` artifacts. Do NOT push/merge to origin (controller finishes; user pushes). Normal VIXEN builds stay .NET-free — the golden gate is dotnet-gated.
- The analyzer DLL `RoslynAnalyzers/SDFNodeGenerator.dll` rebuilds non-deterministically; commit it only when B1 changes analyzer **source** (a size delta proves real content), else `git checkout --` it.

## Milestones

- **B1 — non-scalar layout on the Yeroket core** (Yeroket). Port P1a: `GpuStructModel` non-scalar layout + C++/GLSL emitters + `CodegenTool` nested/array loader resolution + ported NUnit unit tests. Gate: `dotnet test` green over synthetic structs. Merge to Yeroket main after validation.
- **B2 — canonical OctreeConfig + generate + parity** (VIXEN). Author `OctreeConfig.cs`/`ChannelDesc.cs`; invoke the merged tool → commit `OctreeConfig.g.h`/`.glsl`; dotnet-gated CMake golden gate; C++ parity gtest vs `Vixen::SVO::OctreeConfig`; compile-smokes. Gate: golden `--check` clean + parity gtest green + smokes.

## Out of scope / deferred

- **Phase C:** migrate `libraries/SVO/include/ShellOctreeGpu.h` (`#include` the generated `OctreeConfig.g.h`, drop the hand-written struct) + `shaders/BodyInstanceRayMarch.comp` (`#include` the generated `.glsl`); live render no-regression gate; move the SPIR-V drift-guard (`test_octree_config_sdi_parity`) onto the migrated shader. Gated by the shared struct: `OctreeConfig` ∈ `ShellOctreeGpu` + `CashSystem` (separate) + `VoxelSceneCacher`.
- **Cleanup:** delete the parallel `Vixen.Codegen` (P0) from VIXEN.
- Model-emitted GLSL name↔offset map for full drift coverage of `gridMin`/`gridMax` (bracketed by matched neighbors + size assert for now).
- P0 M1-validator carryovers: `!f.IsImplicitlyDeclared` in the field walk; `CompilationLoader`/loader schema-diagnostics surfacing (fold into B1 if cheap).

## Open questions

None outstanding — the P1 design resolved them: `[GpuArray(N)]` over `FixedArray<T>` (chosen; attribute already shipped), Option C (chosen), and C++-parity (not full shader-parity) for this phase (shader migration + SPIR-V drift-guard are Phase C).
