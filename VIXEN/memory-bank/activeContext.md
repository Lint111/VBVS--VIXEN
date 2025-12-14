# Active Context

**Last Updated**: December 14, 2025
**Current Branch**: `main`
**Status**: Tasks #55, #56 Complete - Ready for Task #4 (Benchmark Matrix)

---

## Current State

### Sprint 2 Progress - All Config Tasks Complete ✅

| Task | Title | Status | Impact |
|------|-------|--------|--------|
| #40 | Warmup frames to 100 | ✅ Done | Config already set |
| #39 | BLAS/TLAS build time | ✅ Done | New JSON fields |
| #44 | Cache hit rate counters | ✅ Done | New JSON field |
| #55 | Multi-run iterations | ✅ Done | `--runs N` flag, cross-run stats |
| #56 | 512x512 resolution | ✅ Done | Added to resolutions array |
| #4 | Run benchmark matrix | ⏳ Ready | No longer blocked |

### Session 3 Changes

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
| `bfe7e8d` | feat(benchmark): Sprint 2 data collection polish (#61) |
| `c1f9400` | fix(RenderGraph): Enable shader counters with wrapper type extraction (#60) |
| `4a265b6` | feat(RenderGraph): Add conversion_type pattern for wrapper types (#59) |

---

## Next Steps

1. **Commit current changes** - Tasks #55 and #56
2. **Build standalone package** - Clean state for distribution
3. **Task #4** (16h) - Run full benchmark matrix

---

**End of Active Context**
