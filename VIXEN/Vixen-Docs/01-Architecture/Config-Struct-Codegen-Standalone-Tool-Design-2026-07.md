---
title: Config-Struct Codegen — Standalone Shared Tool (OctreeConfig proof)
status: DESIGN / awaiting review
date: 2026-07-02
tags: [architecture, codegen, shader-management, sdi, kernel-codegen, D5, config-structs]
related:
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]]"
  - "[[Runtime-Kernel-Pipeline-Direction-2026-06]]"
  - "[[vixen-owns-content-format-not-consumer]]"
---

# Config-Struct Codegen — Standalone Shared Tool

**Goal:** single-source the engine's GPU config structs. One **canonical C# definition +
Vixen attributes** → a **standalone codegen tool** emits byte-identical **C++** and **GLSL**
(and later C#/Python), so the engine, the shaders, and future tooling can never disagree about
a struct's layout. First proof: `Vixen::SVO::OctreeConfig`.

**Architecture (one line):** realize Inc4 **D5** (extract the codegen core into a standalone
shared tool; Yeroket becomes one consumer) pragmatically, and add a **GLSL struct emitter**, so
config structs are authored **outside Yeroket** with the kernel/Vixen attributes.

**Tech stack:** .NET (Roslyn) codegen tool (regen-time only) · C++23/GLSL emitted artifacts ·
CMake pre-build regen · glslang/SPIRV-Reflect drift-guard (already built).

## Global constraints (from Inc4, carried verbatim)

- **D8 — canonical stays C#; generated C++/GLSL are committed artifacts; VIXEN's normal build
  stays .NET-free.** Only *regeneration* runs `dotnet` (mirrors the recipe-container regen).
- **Vendored `.g.*` artifacts are VERBATIM** — regenerate, never hand-edit.
- **Live-run gate authoritative for GPU work** — the render path must be proven on lavapipe.
- **VIXEN owns engine/content concepts, not consumer-game ones.** `OctreeConfig` is
  **engine-internal**, so its canonical definition lives VIXEN-side, never in Yeroket.

## 1. Problem

`OctreeConfig` (the 432-byte std430 SSBO at binding 5 of `BodyInstanceRayMarch.comp`) exists as
**two hand-maintained copies** — the C++ struct in `ShellOctreeGpu.h` (with hand-computed padding
+ a `static_assert(offsetof…)` battery) and the GLSL struct in the `.comp`. They must stay
byte-identical by hand. The recipe pool work hit the std140→std430 restride trap here; we now have
a runtime **drift-guard** (`test_octree_config_sdi_parity`) that *catches* divergence but does not
*prevent* it. Prevention = generate both sides from one source.

**Why not "generate C++ from shader reflection" (the original ask):** the shader and C++ describe
the same bytes with **different field shapes** (`gridMin` vec3 vs `gridMinX/Y/Z`; `channels` uvec4
vs `ChannelDesc`; explicit C++ padding vs implicit SPIR-V gaps). Neither side is canonical, so
reflection-of-one can't cleanly regenerate the other. **Resolution:** a third, canonical source
generates *both* — the recipe-container treatment, generalized to a GLSL backend.

## 2. Decisions

| # | Decision | Why |
|---|---|---|
| C1 | **Realize D5: a standalone, callable, pre-build codegen tool** (its own DLL/package); config authored outside Yeroket. | The kernel codegen is proven but trapped in Yeroket; a VIXEN engine struct must not live in the content-format repo. |
| C2 | **Canonical = C# struct + Vixen attributes**, VIXEN-side (`VIXEN/codegen/`). Generated C++/GLSL committed. | D8. Reuses the proven `RecipeContainerEmitter` reflection pattern; keeps VIXEN's build .NET-free. |
| C3 | **Pluggable backend emitters** behind one interface. Ship **C++ + GLSL**; **C#/Python deferred** (tooling consistency, per user). | One source, N backends is the whole point; adding a backend must not touch the schema. |
| C4 | **Layout model computes std430 offsets once; emitters render.** Padding is *derived*, not authored. | Single layout authority = byte-identical guarantee across backends. |
| C5 | **Keep the reflection drift-guard as the independent proof** (GLSL→SPIR-V layout == C++ layout). Add a **byte-identical golden** for regen. | Two independent checks: emitters agree (golden) AND the compiled shader matches (drift-guard). |
| C6 | **First slice = `OctreeConfig` only.** Defer Yeroket→consumer migration, multi-struct rollout, C#/Python, full repo split. | YAGNI; prove the tool + GLSL emitter on one real struct before scaling. |

## 3. Architecture

```
VIXEN/codegen/  (a VIXEN-side .NET project — regen-time only)
  ├─ OctreeConfig.cs          canonical: [GpuStruct] struct OctreeConfig { … }  + ChannelDesc
  ├─ vixen.manifest           emitters: [Cpp, Glsl]   (later: +CSharp, +Python)
  └─ <references> the extracted emitter-core DLL (packaged from Yeroket SourceGenerator core)
                        │  dotnet build  (pre-build step, gated on dotnet availability)
              ┌─────────┴──────────┐
              ▼                    ▼
   OctreeConfig.g.h        OctreeConfig.glsl        ← committed, VERBATIM
   (SVO/include/…)          (shaders/…)
              │                    │
     ShellOctreeGpu.h        BodyInstanceRayMarch.comp  (#include the generated struct)
              └────────── byte-identical by construction ──────────┘
                     proven by: golden test + reflection drift-guard
```

### 3.1 Canonical schema (C# + attributes)
A `[GpuStruct(Layout = Std430)]` marker on a plain C# struct; fields carry enough for layout:
scalar/vector/matrix/array/nested-struct + explicit reserved-pad fields where the wire layout
needs them. `ChannelDesc` is a nested `[GpuStruct]`. **Field-shape choice (C4/migration):** pick
the canonical shapes to minimize churn — keep `ChannelDesc channels[8]` (named), keep the C++
scalar grid components *or* move both sides to `vec3` (decided in P0 by counting the churn on each
side; the drift-guard + render gate make either safe).

### 3.2 Emitter core (extracted)
The generic model-building + emitter dispatch from Yeroket's `SourceGenerator~` (the
`RecipeContainerEmitter` already reflects a C# struct → C++), repackaged as a **referenceable DLL**
the VIXEN codegen project consumes. **P0 proves** the cleanest extraction mechanism (analyzer
referenced by the VIXEN project vs. a plain console `Main`) on the walking skeleton before committing.

### 3.3 Emitters
- **C++** — exists (RecipeContainer path); reuse. Emits `struct` + `static_assert(sizeof/offsetof)`
  self-checks into the `.g.h`.
- **GLSL** — **new.** Emits a GLSL `struct` (+ nested structs) matching the std430 layout. This is
  the one net-new codegen component.
- **C# / Python** — deferred; the interface is shaped so they drop in.

### 3.4 Build wiring
A CMake pre-build `add_custom_command` runs `dotnet build` of `VIXEN/codegen/` → writes the two
artifacts into their vendored locations (like the `body_instance_raymarch_spv` custom target).
**Gated on `dotnet` presence** (D8: normal builds don't regen; they compile the committed
artifacts). A `--check` mode fails the build if committed artifacts are stale vs. canonical
(golden guard).

## 4. Testing

- **Golden / byte-identical regen** — regenerating with unchanged canonical produces
  byte-identical `.g.h` + `.glsl` (like the recipe container's `RecipeContainer.g.h` golden). A
  canonical change that isn't regenerated fails.
- **Reflection drift-guard** — `test_octree_config_sdi_parity` (already built, per-field) proves the
  *emitted GLSL* compiles to the *emitted C++* layout. Now guards the generated pair.
- **Render no-regression** — `BodyInstanceRayMarch.comp` including the generated GLSL struct still
  renders the default scene + the recipe CSG gate on lavapipe (authoritative).
- **Emitter unit tests** (.NET) — layout model offsets for scalars/vec/mat/array/nested/pad vs.
  known std430 expectations.

## 5. Decomposition (phases — each its own plan + gate)

| Phase | Deliverable |
|---|---|
| **P0** | Walking skeleton: extract/package the emitter core as a callable tool; a trivial 2-field `[GpuStruct]` emits C++ **and** GLSL; regen wired in CMake; golden green. Proves the mechanism + the GLSL emitter. |
| **P1** | Canonical `OctreeConfig` (+`ChannelDesc`) in `VIXEN/codegen/`; generate `OctreeConfig.g.h` + `OctreeConfig.glsl` byte-matching today's layout; golden + drift-guard green. |
| **P2** | Migrate consumers: `ShellOctreeGpu.h` includes the generated C++ struct (retire the hand-written one + its static_asserts, now generated); `BodyInstanceRayMarch.comp` includes the generated GLSL struct. Render no-regression on lavapipe. |

## 6. Deferred (explicitly out of this slice)

- **C# / Python emitters** — the tooling-consistency payoff; add once the interface is proven.
- **Multi-struct rollout** — `BodyInstanceGpu`, `ShaderCounters`, and the other hand-mirrored GPU
  structs are the obvious next customers.
- **Yeroket → consumer migration** — Yeroket keeps its in-repo source-gen for now; it migrates to
  the standalone tool later (D5 full).
- **Full standalone repo** — the tool starts packaged from the Yeroket core; a clean separate repo
  is a later move.
- The separate `CashSystem::OctreeConfig` (a different struct) — untouched.

## 7. Open questions (resolve in P0)

- Extraction mechanism: analyzer-referenced-by-VIXEN-project (reuses the exact regen path) vs. a
  plain console tool (cleaner "callable", but more refactor). P0 picks by trying the lighter one.
- Canonical field shapes (scalar grid vs vec3; named GLSL `ChannelDesc` vs uvec4) — pick by churn
  count; either is layout-safe.
- Where the emitter-core DLL is published/consumed (local path reference vs. a nupkg) for VIXEN's
  regen project.
