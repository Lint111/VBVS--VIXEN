---
title: TesterPackage Workflow
aliases: [Benchmark Packaging, Tester Submission]
tags: [profiler, benchmark, workflow]
created: 2025-12-10
---

# TesterPackage Workflow

Automatic ZIP packaging for benchmark results. Testers just run the exe and share the resulting ZIP.

---

## Quick Start

```bash
# Run benchmarks with automatic packaging (default)
vixen_benchmark.exe --quick

# Add your name to the metadata
vixen_benchmark.exe --quick --tester "YourName"

# Skip ZIP creation (for debugging)
vixen_benchmark.exe --quick --no-package
```

---

## Package Contents

Each ZIP contains:

| File/Folder | Description |
|-------------|-------------|
| `*.json` | All benchmark result files |
| `debug_images/` | Mid-frame captures for visual validation |
| `system_info.json` | Hardware metadata |

### system_info.json Structure

```json
{
  "gpu_name": "NVIDIA GeForce RTX 3060 Laptop GPU",
  "driver_version": "572.16",
  "vram_total_mb": 6144,
  "tester_name": "JohnDoe",
  "timestamp": "2025-12-10T15:30:00Z",
  "benchmark_version": "1.0.0"
}
```

---

## Package Naming

Format: `VIXEN_benchmark_YYYYMMDD_HHMMSS_<GPU>.zip`

Example: `VIXEN_benchmark_20251210_153000_RTX_3060.zip`

---

## CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `--package` | ON | Create ZIP after export (default behavior) |
| `--no-package` | OFF | Skip ZIP creation |
| `--tester "Name"` | "" | Add tester name to system_info.json |

---

## Implementation Details

### Files

| File | Purpose |
|------|---------|
| `libraries/Profiler/include/Profiler/TesterPackage.h` | Class definition |
| `libraries/Profiler/src/TesterPackage.cpp` | ZIP creation logic |
| `libraries/Profiler/include/Profiler/BenchmarkConfig.h` | Config struct |
| `application/benchmark/source/BenchmarkCLI.cpp` | CLI parsing |

### Dependencies

- **miniz** - Lightweight ZIP library (FetchContent in dependencies/CMakeLists.txt)

### Integration Point

```cpp
// BenchmarkRunner.cpp - after ExportAllResults()
if (config_.createPackage) {
    TesterPackage package(outputDir_);
    package.Create(config_.testerName, deviceCaps);
}
```

---

## Tester Submission Workflow

1. **Tester** runs `vixen_benchmark.exe --quick --tester "Name"`
2. **Package** created in `benchmark_results/` folder
3. **Tester** shares ZIP file (email, cloud, etc.)
4. **Recipient** uses `python aggregate_results.py --unpack benchmark.zip`
5. **Aggregation** processes all tester folders with `--process-all`

---

## Related Documentation

- [[../Libraries/Profiler|Profiler Library]]
- [[Data-Visualization-Pipeline|Data Pipeline]]
- [[../Analysis/Benchmark-Data-Summary|Benchmark Data Summary]]
