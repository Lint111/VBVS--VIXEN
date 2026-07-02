# Libraries

VIXEN static libraries — reusable units of logic. Each is a separate CMake static-library
target with modern target-based dependencies, built in dependency order.

## Foundation

- **logger** — Logging infrastructure with the `ILoggable` interface and `LOG_*` / `NODE_LOG_*` macros
- **EventBus** — Decoupled message passing and worker-thread integration (invalidation cascade)
- **Core** — Shared core utilities and primitives used across the engine
- **ResourceManagement** — `RM<T>` wrapper with lifecycle/state tracking
- **CashSystem** — Persistent resource cache with async save/load and lazy deserialization

## Vulkan & Rendering

- **VulkanResources** — Low-level Vulkan wrappers (device, swapchain, descriptors, pipelines, textures)
- **ShaderManagement** — Shader compilation and management with build-time tooling, SPIR-V reflection, SDI generation, and hot reload
- **RenderGraph** — Node-based data-flow render graph: compile-time typed slots, resource variants, execution ordering (30+ node types)
- **Profiler** — GPU/CPU timing, bandwidth monitoring, and benchmark instrumentation

## Voxel & Procedural Content

- **VoxelData** — Zero-copy, reference-based brick architecture for voxel storage
- **SVO** — Sparse Voxel Octree (Laine–Karras foundation) with traversal + Recipe/SDF integration
- **VoxelComponents** — Reusable voxel component types
- **GaiaVoxelWorld** — ECS-based voxel world backend (built on Gaia ECS) for sparse entity storage
- **GaiaArchetypes** — Shared ECS archetype definitions for the Gaia-based voxel world

## Organization

Libraries changing in one component do not force recompilation of others (target-based
dependencies + incremental compilation). See each library's own `README.md` where present,
and the [Libraries reference docs](../Vixen-Docs/Libraries/) in the vault.
