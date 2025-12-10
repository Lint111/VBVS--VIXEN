# Active Context

**Last Updated**: December 10, 2025
**Current Branch**: `claude/phase-k-hardware-rt`
**Status**: Phase L Complete - Awaiting tester benchmark submissions

---

## Current State

### Phase L: Data Pipeline ✅ COMPLETE

Data visualization and tester coordination infrastructure:

| Component | Status |
|-----------|--------|
| aggregate_results.py | ✅ JSON → Excel aggregation |
| generate_charts.py | ✅ 9 chart types |
| TesterPackage | ✅ Automatic ZIP packaging |
| Multi-tester workflow | ✅ Pack/unpack commands |

**Tester Workflow:**
```bash
vixen_benchmark.exe --quick              # Creates ZIP automatically
vixen_benchmark.exe --tester "JohnDoe"   # Add tester name to metadata
```

**Package Contents:** JSON results, debug_images/, system_info.json

**Blocking:** Awaiting benchmark submissions from additional machines.

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
| Benchmark config | `application/benchmark/benchmark_config.json` |
| Graph factory | `libraries/Profiler/src/BenchmarkGraphFactory.cpp` |
| RT shaders | `shaders/VoxelRT*.rgen/rmiss/rchit/rint` |
| Compute shaders | `shaders/VoxelRayMarch*.comp` |
| Fragment shaders | `shaders/VoxelRayMarch*.frag` |
| Material colors | `shaders/Materials.glsl` |
| Data pipeline | `scripts/aggregate_results.py`, `scripts/generate_charts.py` |

---

## Active Todo List

### Data Quality Improvements
**Critical:**
- [ ] Fix `avg_voxels_per_ray` shader instrumentation (currently 0.0)
- [ ] Add ray throughput measurement for Fragment/HW RT pipelines

**Medium:**
- [ ] Add GPU utilization monitoring (NVML integration)
- [ ] Measure BLAS/TLAS build time in AccelerationStructureNode
- [ ] Increase warmup frames to 100 (filter frame 75 spike)

**Low:**
- [ ] Add 3 iterations per config for statistical robustness
- [ ] Add cache hit rate counters in shaders
- [ ] Add 512³ resolution tests (Compute/Fragment only)
- [ ] Create `benchmark_schema.json` for validation

### Vault Maintenance
- [ ] Review existing Vixen-Docs for outdated documentation
- [ ] Document TesterPackage workflow in Vixen-Docs/04-Development/

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
