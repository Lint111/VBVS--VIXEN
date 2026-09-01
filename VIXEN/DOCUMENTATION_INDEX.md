# Documentation Index

**Status (2026-09):** Game-engine-library track active (engine facade, render-target
abstraction, recoverable error model) · SDF/Recipe/CSG content system in active development ·
voxel authoring Inc1 and compiler-emitted SIMD4 materialization landed · raster-proxy B1 is
DEFAULT-ON · voxel ray-tracing research feature-complete, paper drafted.

The **canonical** documentation is the Obsidian vault at [`Vixen-Docs/`](Vixen-Docs/). The
older [`documentation/`](documentation/) tree is retained for reference but partially superseded
by the vault. Finished/superseded material lives under
[`Vixen-Docs/_archive/`](Vixen-Docs/_archive/) and [`archive/`](archive/) (July 2026 cleanup).

## Start Here

1. [`../README.md`](../README.md) — repository overview, features, build
2. [`README.md`](README.md) — engine build + repository layout
3. [`CLAUDE.md`](CLAUDE.md) — assistant guidelines, coding standards, rule system
4. [`memory-bank/`](memory-bank/) — session/context state (projectbrief, systemPatterns, progress)

## Canonical Vault — `Vixen-Docs/`

| Area | Path | Contents |
|------|------|----------|
| Index | [`00-Index/`](Vixen-Docs/00-Index/) | Home, quick-lookup |
| Architecture | [`01-Architecture/`](Vixen-Docs/01-Architecture/) | Render graph, render targets, error model, SDF/Recipe codegen, game-renderer review |
| Implementation | [`02-Implementation/`](Vixen-Docs/02-Implementation/) | Compute/SVO/shader implementation notes |
| Research | [`03-Research/`](Vixen-Docs/03-Research/) | Paper draft, bibliography, algorithm analyses |
| Development | [`04-Development/`](Vixen-Docs/04-Development/) | Dev tooling, guides, troubleshooting |
| Progress | [`05-Progress/`](Vixen-Docs/05-Progress/) | Workstreams, feature notes, active status |
| Embedding | [`06-Embedding/`](Vixen-Docs/06-Embedding/) | Consuming VIXEN as a library |
| Libraries | [`Libraries/`](Vixen-Docs/Libraries/) | Per-library reference docs |

**Recommended entry point:** `01-Architecture/Architecture-Review-Game-Renderer-2026-06-12.md`
— the gap analysis driving the current game-engine-library direction.

## Legacy reference — `documentation/`

Older topic docs retained for reference (some superseded by the vault): `GraphArchitecture/`,
`ShaderManagement/`, `Shaders/`, `VulkanResources/`, and coding **Standards/**
(`cpp-programming-guidelins.md`, `Communication Guidelines.md`, `smart-pointers-guide.md`).

## Library READMEs

[`libraries/README.md`](libraries/README.md) lists all 14 libraries. Notable per-library docs:
[SVO](libraries/SVO/README.md), [RenderGraph](libraries/RenderGraph/README.md),
[ShaderManagement](libraries/ShaderManagement/README.md),
[VulkanResources](libraries/VulkanResources/README.md),
[VoxelData](libraries/VoxelData/README.md), [GaiaVoxelWorld](libraries/GaiaVoxelWorld/README.md).

## Archives

- [`Vixen-Docs/_archive/2026-07/`](Vixen-Docs/_archive/) — sessions, feature proposals, MCP-dev notes, completed sprint logs
- [`Vixen-Docs/05-Progress/archive/`](Vixen-Docs/05-Progress/archive/) — earlier progress snapshots
- [`archive/`](archive/) — repo-level dated archives (Phase-G, doc-consolidation, 2026-07 cleanup)
