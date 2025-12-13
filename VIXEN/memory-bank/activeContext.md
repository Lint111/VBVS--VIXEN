# Active Context

**Last Updated**: December 13, 2025
**Current Branch**: `main`
**Status**: IDebugBuffer Refactor Phase 1 Complete

---

## Current State

### IDebugBuffer Refactor (#58) - Phase 1 Complete ✅

Consolidated debug buffer infrastructure for polymorphic buffer types:

| File | Purpose |
|------|---------|
| `IDebugBuffer.h` | GPU buffer interface |
| `IExportable.h` | Pure serialization |
| `RayTraceBuffer.h/.cpp` | Per-ray traversal |
| `ShaderCountersBuffer.h/.cpp` | Atomic counters |
| `DebugCaptureResource.h` | Polymorphic wrapper |

**Key Changes:**
- `IDebugCapture::GetBuffer()` returns `IDebugBuffer*` (polymorphic)
- Factory methods: `DebugCaptureResource::CreateRayTrace()`, `CreateCounters()`
- Removed: `DebugCaptureBuffer.h`, `IDebugExportable.h`

**Next (Phase 2):** Enable shader counters, wire to BenchmarkRunner for real `avgVoxelsPerRay`

---

## Quick Reference

### Build Commands
```bash
cmake --build build --config Debug --parallel 16
```

### Run Benchmarks
```bash
./binaries/vixen_benchmark.exe --config ./application/benchmark/benchmark_config.json --render -i 100 -w 10
```

### Test Commands
```bash
./build/libraries/SVO/tests/Debug/test_*.exe --gtest_brief=1
./build/libraries/Profiler/tests/Debug/test_profiler.exe --gtest_brief=1
```

---

## Key Files

| Purpose | Location |
|---------|----------|
| Debug buffer interface | `libraries/RenderGraph/include/Debug/IDebugBuffer.h` |
| Ray trace buffer | `libraries/RenderGraph/include/Debug/RayTraceBuffer.h` |
| Shader counters | `libraries/RenderGraph/include/Debug/ShaderCountersBuffer.h` |
| Benchmark config | `application/benchmark/benchmark_config.json` |
| Graph factory | `libraries/Profiler/src/BenchmarkGraphFactory.cpp` |
| Compute shaders | `shaders/VoxelRayMarch*.comp` |

---

## Active Todo List

### IDebugBuffer Refactor (#58)
- [x] Phase 1: Create polymorphic IDebugBuffer interface
- [x] Phase 1: Create RayTraceBuffer and ShaderCountersBuffer
- [x] Phase 1: Refactor DebugCaptureResource to use factory pattern
- [ ] Phase 2: Enable ENABLE_SHADER_COUNTERS in shaders
- [ ] Phase 2: Wire ShaderCountersBuffer to BenchmarkRunner
- [ ] Phase 2: Replace hardcoded avgVoxelsPerRay with real measurement

### Data Quality Improvements
**Critical:**
- [ ] Fix `avg_voxels_per_ray` shader instrumentation (currently 0.0)
- [ ] Add ray throughput measurement for Fragment/HW RT pipelines

**Medium:**
- [ ] Add GPU utilization monitoring (NVML integration)
- [ ] Measure BLAS/TLAS build time in AccelerationStructureNode
- [x] ~~Increase warmup frames to 100 (filter frame 75 spike)~~ (Sprint 2 #40)

**Low:**
- [ ] Add 3 iterations per config for statistical robustness
- [ ] Add cache hit rate counters in shaders
- [ ] Add 512³ resolution tests (Compute/Fragment only)
- [ ] Create `benchmark_schema.json` for validation

### Vault Maintenance
- [ ] Review existing Vixen-Docs for outdated documentation
- [x] ~~Document TesterPackage workflow~~ (see HacknPlan-Integration.md)

### Phase M - Analysis & Paper
- [ ] Statistical analysis of results
- [ ] Performance comparison charts
- [ ] Write findings section
- [ ] Validate/refute hypothesis

### Deferred Tasks (Post-Benchmark)

**Architecture: VoxelAABBCacher (High Priority)**
- [ ] Create VoxelAABBCacher - Specialized cacher for AABB extraction
  - VoxelAABBConverterNode → VoxelAABBCacher
  - Remove `precomputedAABBData` hack from AccelStructCreateInfo

**Other Deferred:**
- [ ] BuildHybridGraph() - Combine compute + fragment approaches
- [ ] GLSL shader counter queries
- [ ] VK_KHR_performance_query (hardware bandwidth)
- [ ] Nsight Graphics validation
- [ ] Unified Parameter/Resource Type System

---

## Recent Commits

| Hash | Description |
|------|-------------|
| `8c2a358` | feat(Profiler): Add automatic ZIP packaging for benchmark results |
| `e43232c` | feat(data-pipeline): Implement Phase L data visualization pipeline |
| `8cabefc` | refactor(CashSystem): Extract VoxelAABBCacher from AccelerationStructureCacher |
| `60e0ce7` | fix(RT): Fix HW RT sparse rendering and consolidate CashSystem types |
| `782fd8d` | fix(Benchmark): Add vkDeviceWaitIdle before cleanup on ESC exit |

---

## Historical Sessions

**Archived to:** `Vixen-Docs/05-Progress/Phase-History.md`

Includes: Phase K HW RT fixes, Sessions 1-5 details, InputNode QoL, Frame Capture, Type Consolidation, Logging Refactor.

---

**End of Active Context**
