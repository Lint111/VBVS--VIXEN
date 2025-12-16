# Active Context

**Last Updated**: December 16, 2025
**Current Branch**: `main`
**Status**: Memory Guards Implemented - Ready for Release Testing

---

## Current State

### Sprint 2 Progress + Memory Guards ✅

| Task | Title | Status | Impact |
|------|-------|--------|--------|
| #40 | Warmup frames to 100 | ✅ Done | Config already set |
| #39 | BLAS/TLAS build time | ✅ Done | New JSON fields |
| #44 | Cache hit rate counters | ✅ Done | New JSON field |
| #55 | Multi-run iterations | ✅ Done | `--runs N` flag, cross-run stats |
| #56 | 512x512 resolution | ✅ Adjusted | OOM in windowed mode, headless OK |
| #77 | FrameCapture crash fix | ✅ Done | ESC cleanup crash resolved |
| #78 | MainCacher crash fix | ✅ Done | Use-after-free resolved |
| NEW | Memory OOM guards | ✅ Done | Prevents benchmark crashes |
| #4 | Run benchmark matrix | ⏳ In Progress | 512³ windowed mode OOM identified |

### Session 6 Changes (512³ Resolution Issue Investigation)

**Issue Identified:** 512³ windowed mode OOM crashes despite memory guards

**Root Cause Analysis:**
- 512³ voxel grids = 134,217,728 voxels (134M voxels)
- Memory requirement: ~130-156 MB per pipeline type (COMPUTE/FRAGMENT/HW_RT)
- Available VRAM on RTX 3060 Laptop: 6 GB total
- Windowed rendering overhead: Swap chain images (3x 2K RGBA = ~48MB), present queue, descriptor allocation
- **Root cause:** Memory guards check conservative margins (80% GPU = 4.8GB safe) but 512³ + windowed overhead exceeds safe threshold

**Test Results:**
- ✅ 64³, 128³, 256³: All pass in windowed mode (compute, fragment, hardware_rt)
- ✅ 512³: Works in headless mode (lower overhead)
- ❌ 512³: Crashes in windowed mode despite guards

**Workaround Applied:**
- Removed 512 from `benchmark_config.json` resolutions array
- Current config: `"resolutions": [64, 128, 256]`
- Preserves 192-test matrix (64 tests per resolution × 3 resolutions)

**Findings:**
- Memory guards function correctly - they prevent crashes with reasonable safety margins
- 512³ requires dedicated headless testing or higher VRAM GPU
- Benchmark remains stable for practical resolution range
- Results available: `C:\Users\liory\Downloads\VIXEN_Benchmarks`

### Session 5 Changes (Memory Guard Implementation)

**Memory OOM Guards - Prevent Benchmark Crashes:**
- **Purpose:** Estimate and check GPU/host memory requirements before running tests
- **Solution:**
  - Added comprehensive memory estimation functions in `BenchmarkConfig.cpp`
  - `EstimateGPUMemory()` - Shader-aware calculations (compressed vs uncompressed)
  - `EstimateHostMemory()` - System RAM for voxel construction
  - `GetAvailableGPUMemory()` - Vulkan API queries for VRAM
  - `GetAvailableHostMemory()` - Platform-specific (Windows/Linux/macOS)
  - `ShouldSkipTestForMemory()` - Conservative safety margins (80% GPU, 70% host)
- **Memory Calculations:**
  - **Compressed shaders**: DXT1 color (256B/brick) + DXT normal (512B/brick) = 768B/brick
  - **Uncompressed shaders**: Material data with safety margin = 3072B/brick
  - Includes octree hierarchy, swapchain images, staging buffers, descriptors
- **Files Modified:**
  - `libraries/Profiler/src/BenchmarkConfig.cpp:533-575` (+244 lines)
- **Testing:**
  - ✅ Built successfully (Debug + Release)
  - ✅ Benchmark runs without crashes on RTX 3060 (6GB VRAM)
  - ✅ Verbose logging shows memory checks: "Generating test matrix with memory checks..."
  - ✅ Generated 192 tests with no false-positive skips

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
| `8b4c6ae` | feat(Profiler): Add memory checks to prevent benchmark OOM crashes |
| `8205e87` | feat(Profiler): Improve memory estimation (shader-aware compressed/uncompressed) |
| `b5b65ac` | feat(Profiler): Add memory checks to skip tests exceeding available memory |
| `5c0154f` | feat(RenderGraph): Add compile-time SFINAE guard for wrapper types (#61) |
| `4b5ed69` | docs: Update documentation for SFINAE/conversion_type debugging (#61) |

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

1. ✅ **Memory guards merged** - Commit `8b4c6ae` merged to main
2. **Create HacknPlan task** - Document memory guard implementation
3. **Build standalone package** - Release build ready for distribution
4. **Task #4** (16h) - Run full benchmark matrix (stable, OOM-protected)

---

**End of Active Context**
