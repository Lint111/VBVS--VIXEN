# VIXEN (engine)

The VIXEN engine: a production-quality **Vulkan render-graph engine** being extended into a
reusable, moddable **game render engine** for the **Undertow** game, with an **SDF/Recipe/CSG**
procedural-content codegen pipeline.

> For the project overview, feature list, and architecture diagram see the
> **[repository README](../README.md)**. This file is the engine-developer quick reference.

## Build

```bash
# Configure — Ninja auto-selected if present; Vulkan SDK auto-provisioned if not found
cmake -B build

# Build (parallel)
cmake --build build --config Debug --parallel 16     # or Release
```

- **Platforms:** Windows 10/11 (MSVC) and Linux/WSL (GCC/Clang).
- **Vulkan SDK:** auto-provisioned via `cmake/ProvisionVulkan.cmake` (Linux/WSL variant:
  `ProvisionWslVulkan.cmake`). Manual `C:/VulkanSDK/1.4.321.1` is also detected.
- **Trimmed build:** `-DVULKAN_TRIMMED_BUILD=ON` fetches headers only (no runtime libs).
- **Tests:** `ctest` from the build dir, or run the per-library `test_*` binaries directly.

See [`Vixen-Docs/04-Development/`](Vixen-Docs/) and `CMakePresets.json` for more.

## Repository layout

| Path | Purpose |
|------|---------|
| `libraries/` | 14 static libraries — see [`libraries/README.md`](libraries/README.md) |
| `application/` | Executable entry points (`main`, `benchmark`) |
| `shaders/` | GLSL/HLSL shader sources + runtime-loaded SPIR-V assets |
| `generated/` | SDI headers (`sdi/*.h`, tracked) — compiled `.spv` build copies are ignored |
| `BuiltAssets/` | Curated built shader/asset staging |
| `dependencies/` | Third-party fetch/setup (FetchContent) |
| `cmake/` | Provisioning, install/export, asset-staging helpers |
| `tests/` | Root-level test harness (libraries carry their own `tests/`) |
| `Vixen-Docs/` | **Canonical** Obsidian documentation vault |
| `memory-bank/` | Session/context state |
| `documentation/` | Older reference docs (partially superseded by `Vixen-Docs/`) |
| `archive/` | Finished/superseded material (dated folders) |

## Where to read next

- **Architecture:** [`Vixen-Docs/01-Architecture/`](Vixen-Docs/01-Architecture/) — start with the
  game-renderer review (`Architecture-Review-Game-Renderer-2026-06-12.md`)
- **Libraries:** [`Vixen-Docs/Libraries/`](Vixen-Docs/Libraries/)
- **Doc catalog:** [`DOCUMENTATION_INDEX.md`](DOCUMENTATION_INDEX.md)
- **Coding standards:** [`documentation/Standards/`](documentation/Standards/)

## Status

Engine core (render graph, shader management, caching, profiling) is stable. In progress: the
game-engine-library boundary (facade, render-target abstraction, recoverable error model) and the
SDF/Recipe/CSG content system (registry, octree pool, live render gates). The originating voxel
ray-tracing research is feature-complete with its paper drafted.
