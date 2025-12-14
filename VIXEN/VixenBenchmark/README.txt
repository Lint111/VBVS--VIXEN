VIXEN Benchmark Tool - Standalone Package
==========================================

Usage:
  vixen_benchmark.exe [options]

Quick Start:
  vixen_benchmark.exe --quick --render    # Quick visual test (12 configs)
  vixen_benchmark.exe --quick --headless  # Quick headless test
  vixen_benchmark.exe --full --render     # Full matrix (180 configs)

Options:
  --help              Show all options
  --list-gpus         List available GPUs
  --runs N            Run each config N times for statistical robustness
  --config FILE       Use custom JSON configuration
  --output DIR        Output directory for results

Results are saved to: Downloads/VIXEN_Benchmarks/

Package Contents:
  vixen_benchmark.exe   - Benchmark executable
  tbb12*.dll            - Threading Building Blocks library
  benchmark_config.json - Default test configuration
  benchmark_schema.json - JSON schema for result validation
  shaders/              - GLSL shader sources (compiled at runtime)
  cache/                - Shader compilation cache (auto-populated)
  generated/            - Generated files (auto-populated)
