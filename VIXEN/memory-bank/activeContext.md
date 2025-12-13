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
- [ ] Fix `avg_voxels_per_ray` shader instrumentation (currently 0.0) → Phase 2 above
- [ ] Add ray throughput measurement for Fragment/HW RT pipelines

**Medium:**
- [ ] Add GPU utilization monitoring (NVML integration)
- [ ] Measure BLAS/TLAS build time in AccelerationStructureNode

### Deferred Tasks (Post-Benchmark)
- [ ] VoxelAABBCacher extraction
- [ ] BuildHybridGraph() implementation
- [ ] VK_KHR_performance_query integration

---

## Recent Commits

| Hash | Description |
|------|-------------|
| `54c3104` | fix(benchmark): GPU metrics for FRAGMENT/HW_RT + standalone package |
| `5faefa8` | docs(session): Session summary 2025-12-13 |
| `4bf55e7` | feat(benchmark): Sprint 2 Data Collection Polish |
| `a645df0` | fix(benchmark): TBB DLL copy and clean terminal output |

---

## Historical Sessions

**Archived to:** `Vixen-Docs/05-Progress/Phase-History.md`

---

**End of Active Context**
