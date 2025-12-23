VIXEN Benchmark Tool - Standalone Package
==========================================

Usage:
  vixen_benchmark.exe [options]

Quick Start:
  vixen_benchmark.exe --config benchmark_config_logic_test.json  # Logic test (6 configs, ~1 min)
  vixen_benchmark.exe --quick --render                           # Quick test (12 configs)
  vixen_benchmark.exe --quick --headless                         # Quick headless test
  vixen_benchmark.exe --full --render                            # Full matrix (144 configs)

Options:
  --help              Show all options
  --list-gpus         List available GPUs
  --runs N            Run each config N times for statistical robustness
  --config FILE       Use custom JSON configuration
  --output DIR        Output directory for results

Results are saved to: Downloads/VIXEN_Benchmarks/

Package Contents:
  vixen_benchmark.exe              - Benchmark executable
  tbb12*.dll                       - Threading Building Blocks library
  benchmark_config.json            - Default test configuration (144 tests)
  benchmark_config_logic_test.json - Minimal logic test (6 tests)
  benchmark_schema.json            - JSON schema for result validation
  shaders/                         - GLSL shader sources (compiled at runtime)
  cache/                           - Shader compilation cache (auto-populated)
  generated/                       - Generated files (auto-populated)

Configuration Variants:
  benchmark_config_logic_test.json - Minimal matrix for testing logic
    - 1 resolution (64³), 1 scene (cornell), 1 screen size (1280x720)
    - All 3 pipeline types (COMPUTE, FRAGMENT, HW_RT)
    - Both compression variants per pipeline
    - Total: 6 tests (~1 minute with multi-GPU)
    - Use for: Multi-GPU logic testing, quick smoke tests

  benchmark_config.json - Full performance matrix
    - 3 resolutions (64³, 128³, 256³)
    - 4 scenes (cornell, noise, tunnels, cityscape)
    - 2 screen sizes (720p, 1080p)
    - All 3 pipeline types with compression variants
    - Total: 144 tests (~30-60 minutes per GPU)
    - Use for: Performance data collection, thorough testing
