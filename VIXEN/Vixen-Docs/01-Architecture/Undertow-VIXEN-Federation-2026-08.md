---
title: Undertow VIXEN Federation
aliases: [Undertow Integration, Federation Contract]
tags: [architecture, undertow, vixen, kernel, view, appflow]
date: 2026-08-12
status: active
---

# Undertow VIXEN Federation

## Purpose

This is the current ownership and integration map for Undertow, VIXEN, and the Yeroket kernel framework.
It replaces historical migration plans as the starting point for cross-repo work. Historical designs retain
rationale, not current paths or status.

## Ownership

| Domain | Owner | Contract |
|---|---|---|
| Authoritative campaign, content, saves, and fog-filtered projection | Undertow `core/` | Pure deterministic C#; no Unity/Burst/VIXEN runtime enters the core. |
| Native host and presentation composition | Undertow `vixen/` | The super-build links `VixenApp`, boots .NET through hostfxr, and owns the C ABI session. |
| Renderer, render graph, RmlUi, AppFlow runtime, and render-local caches | VIXEN | Presentation consumes projected data only. It never becomes simulation authority. |
| Attributes, schema lowering, and multi-language emitters | Yeroket kernel framework | Generic code generation. Consumers own schemas and committed generated artifacts. |

## Executable Path

```text
Undertow core sim
  -> Undertow.View generated writers
  -> UTVC v2 container of UTVA section buffers
  -> ut_view / ut_view_manifest C ABI
  -> undertow_host (hostfxr + VixenApp)
  -> VIXEN body-octree traversal
  -> hit records -> DirectLighting -> SpatialReuse
  -> offscreen render target -> blit -> sky/RmlUi -> present
```

`vixen/CMakeLists.txt` is the supported integration route. It adds VIXEN as a subdirectory, then builds
`undertow_host`; `vixen/app/CMakeLists.txt` intentionally rejects a standalone host configure. The generic
installed-SDK route remains incomplete: VIXEN builds 15 library directories, while its install export lists
13 targets and omits `AppFlow` and `KernelDispatch`.

## View Contract

- `UTVC` is the version-2 outer container. It carries independently encoded `UTVA` section payloads.
- `UTVM` is the manifest export. It carries each section's id, name, structural hash, and row-keyed flag;
  the host intersects its generated readers with that manifest.
- `schemas.json` is the View-shape authority. Derived queries and runtime-state `views` facets generate
  the C# writer/reader faces. `core/codegen/view-schemas/UndertowHud.cs` has no remaining hand `[View]`
  roots.
- `ViewWriterAdapter` is the allowed deterministic population seam. It is not a second wire schema.
- Do not edit generated `*.g.cs` or `*.g.h` files. Change the schema/catalogue and regenerate.

**Current gate (reverified 2026-09-01):** the prior 13-vs-15 View-section drift is fixed. The
reconciled `ViewSectionEnum.g.h` contains `ObserverDemand` and `OrbitalStructureFacet` at values 13
and 14, and the generated C++ blob/typed/read-model faces are present (`c807e0cc`,
`VIXEN/application/main/include/Generated/Undertow{ObserverDemand,OrbitalStructureFacet}.*`).
Keep the C#/C++ drift gates green as the release-safety check.

## AppFlow

AppFlow is presentation/input routing, not game-state authority.

- Kernel-owned `[Flow*]` attributes declare state, actions, bindings, and optional `FlowDataTarget`s.
- `--view-noun-enum` emits typed `ViewNounId`; `FlowDataTarget` resolves to that type at generation time.
- VIXEN `AppFlowLoader` loads generated data targets and `AppFlowRuntime` routes Data actions through an
  `IViewDataProvider`. A direct-field provider exists now; a future Gaia provider is a provider swap.
- Undertow's generated `UndertowFlow` crosses the C ABI as resolved action ids and parameters. VIXEN's
  own AppFlow container remains a separate engine artifact.

## Kernel Consumption

Undertow and VIXEN both invoke the kernel CodegenTool directly.

- Undertow's `vixen/codegen/CMakeLists.txt` builds `CodegenTool.dll` once and invokes it for AppFlow,
  View registry/enum, content, and native-Gaia faces. `tools/check-content-codegen.sh` and
  `tools/check-view-derive.sh` are hard shell gates.
- VIXEN's `codegen/CMakeLists.txt` emits its own schemas and reads Undertow's View schemas only when it is
  in the Undertow superproject. A standalone VIXEN checkout consumes committed Undertow artifacts; it
  cannot regenerate them without Undertow present.

## Current Risks

1. Keep the C#/C++ View-section drift gates green; sections 13 and 14 are present in the current generated enum/faces (reverified 2026-09-01, `c807e0cc`, `7e258a64`).
2. Complete the installed-SDK export surface or keep documenting the super-build as the supported route.
3. Validate a player-facing toy with a stranger before treating renderer mechanism work as game progress.
4. Keep VIXEN render-local state and future `GaiaVoxelWorld` data presentation-only; authority remains on
   Undertow's managed path until a native family passes its explicit cutover gates.

> **Risk recheck 2026-09-01:** Risk 1 is corrected above. Risk 2 remains open: the standalone export
> list still omits `AppFlow` and `KernelDispatch` (`VIXEN/cmake/VixenInstall.cmake:46-77`), while
> Risks 3–4 remain unchanged.

## Source Pointers

- Undertow host: `vixen/CMakeLists.txt`, `vixen/app/CMakeLists.txt`, `vixen/host/Undertow.Vixen.Host/`
- View source/gates: `core/src/Undertow.Authoring/Schema/SchemaJson.cs`,
  `core/src/Undertow.View/Generated/`, `tools/check-view-derive.sh`
- VIXEN render driver: `application/main/source/graph/BuildRenderGraph.cpp`
- VIXEN AppFlow seam: `libraries/AppFlow/include/IViewDataProvider.h`, `src/AppFlowLoader.cpp`,
  `src/AppFlowRuntime.cpp`
- Kernel tool: `Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs`
