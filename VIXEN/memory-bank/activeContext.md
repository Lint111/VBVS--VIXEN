# Active Context

**Last Updated**: December 14, 2025
**Current Branch**: `main`
**Status**: Critical Bug Fixes Complete - Ready for Task #4 (Benchmark Matrix)

---

## Current State

### Sprint 2 Progress - All Tasks Complete ✅

| Task | Title | Status | Impact |
|------|-------|--------|--------|
| #40 | Warmup frames to 100 | ✅ Done | Config already set |
| #39 | BLAS/TLAS build time | ✅ Done | New JSON fields |
| #44 | Cache hit rate counters | ✅ Done | New JSON field |
| #55 | Multi-run iterations | ✅ Done | `--runs N` flag, cross-run stats |
| #56 | 512x512 resolution | ✅ Done | Added to resolutions array |
| #77 | FrameCapture crash fix | ✅ Done | ESC cleanup crash resolved |
| #78 | MainCacher crash fix | ✅ Done | Use-after-free resolved |
| #4 | Run benchmark matrix | ⏳ Ready | No longer blocked |

### Session 4 Changes (Critical Stability Fixes)

**Task #77 - FrameCapture ESC Crash Fix:**
- **Root Cause:** FrameCapture cleanup attempted after VkDevice destruction, causing access violation
- **Solution:**
  - Added `RegisterExternalCleanup()` API to RenderGraph for dependency-aware cleanup
  - Changed `frameCapture_` from `unique_ptr` to `shared_ptr` for shared ownership
  - Cleanup lambda captures shared_ptr, executes BEFORE DeviceNode cleanup
- **Files Modified:**
  - `libraries/RenderGraph/include/Core/RenderGraph.h:289` - New API
  - `libraries/RenderGraph/src/Core/RenderGraph.cpp:414` - Implementation
  - `libraries/Profiler/include/Profiler/BenchmarkRunner.h:337` - shared_ptr change
  - `libraries/Profiler/src/BenchmarkRunner.cpp:924-932` - Registration
  - `libraries/Profiler/src/FrameCapture.cpp:223` - Simplified cleanup

**Task #78 - MainCacher MessageBus Crash Fix:**
- **Root Cause:** MainCacher (global singleton) destructor tried to unsubscribe from MessageBus (local unique_ptr) after MessageBus already destroyed
- **Solution:**
  - Added `MainCacher::Shutdown()` method for explicit cleanup
  - BenchmarkRunner calls `Shutdown()` before MessageBus destruction
- **Files Modified:**
  - `libraries/CashSystem/include/MainCacher.h` - Shutdown() declaration
  - `libraries/CashSystem/src/main_cacher.cpp:51-58` - Implementation
  - `libraries/Profiler/src/BenchmarkRunner.cpp:1224` - Explicit shutdown call

### Previous Session Changes

**Task #56:**
- Added `512` to `resolutions` in `benchmark_config.json`
- Updated `GlobalMatrix::resolutions` default in `BenchmarkConfig.h`

**Task #55:**
- Added `--runs N` CLI flag (1-100)
- `CrossRunStats` struct for mean/stddev/min/max across runs
- `MultiRunResults` struct with `ComputeStatistics()`
- `ExportMultiRunToJSON()` for multi-run output
- Updated `benchmark_schema.json` with `$defs/cross_run_stats` and `multi_run` block
- `CollectCurrentTestResults()` and `ResetCurrentTestForRerun()` helpers

---

## Quick Reference

### Build Commands
```bash
cmake --build build --config Debug --parallel 16
```

### Run Benchmarks
```bash
# Quick test (single run)
./binaries/vixen_benchmark.exe --render --quick --no-package --no-open

# Full matrix with multi-run
./binaries/vixen_benchmark.exe --render --runs 3 --no-open
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
| Benchmark schema | `application/benchmark/benchmark_schema.json` |
| CLI parsing | `application/benchmark/source/BenchmarkCLI.cpp` |
| Suite config | `libraries/Profiler/include/Profiler/BenchmarkConfig.h` |
| Multi-run results | `libraries/Profiler/include/Profiler/TestSuiteResults.h` |
| Metrics export | `libraries/Profiler/src/MetricsExporter.cpp` |
| Benchmark runner | `libraries/Profiler/src/BenchmarkRunner.cpp` |

---

## Active HacknPlan Tasks

| # | Title | Status |
|---|-------|--------|
| 4 | Run 180-config benchmark | Ready |

---

## Recent Commits

| Hash | Description |
|------|-------------|
| TBD | fix(Profiler,CashSystem): Critical cleanup crashes (#77, #78) |
| `bfe7e8d` | feat(benchmark): Sprint 2 data collection polish (#61) |
| `c1f9400` | fix(RenderGraph): Enable shader counters with wrapper type extraction (#60) |
| `4a265b6` | feat(RenderGraph): Add conversion_type pattern for wrapper types (#59) |

---

## Architecture Improvements

### RegisterExternalCleanup API
New RenderGraph feature allowing external systems to integrate with dependency-aware cleanup:
```cpp
// Example: Register FrameCapture cleanup before DeviceNode
renderGraph->RegisterExternalCleanup(
    "benchmark_device",  // Dependency node name
    [capture = frameCapture_]() {  // Lambda with shared ownership
        if (capture) capture->Cleanup();
    },
    "FrameCapture"  // Debug name
);
```

**Benefits:**
- External systems inherit graph's recursive cleanup ordering
- Prevents use-after-free by enforcing dependency order
- Reusable pattern for future external integrations

---

## Next Steps

1. **Commit current changes** - Tasks #77, #78 (critical stability fixes)
2. **Build standalone package** - Clean state for distribution
3. **Task #4** (16h) - Run full benchmark matrix (now stable)

---

**End of Active Context**
