# Product Context

## Why This Project Exists

**Primary Mission** (November 2025): VIXEN is a **voxel ray tracing research platform** for comparative pipeline analysis targeting academic publication (May 2026).

**Secondary Mission**: Provide a structured, production-quality implementation of modern graph-based rendering architecture demonstrating industry-standard patterns (Unity HDRP, Unreal RDG).

**Legacy Origin**: Originally a hands-on learning path for Vulkan fundamentals, now evolved into a research-grade codebase.

## Problem It Solves

### Research Challenge
**Question**: How do different Vulkan ray tracing/marching pipeline architectures affect rendering performance, GPU bandwidth utilization, and scalability for data-driven voxel rendering?

**Gap in Literature**: Limited comparative analysis of Vulkan-specific pipelines (compute shader, fragment shader, hardware RT, hybrid) for voxel rendering at scale.

**Test Matrix**: 180 configurations (4 pipelines × 5 resolutions × 3 densities × 3 algorithms)

### Engineering Challenge
Production-quality graph-based rendering requires:
- **Type-safe resource management** - Compile-time validation eliminates runtime errors
- **Automated testing** - 40% coverage ensures correctness
- **Comprehensive logging** - ILoggable interface enables debugging
- **Flexible architecture** - Variadic nodes, lifecycle hooks, context system
- **Performance profiling** - Timestamp queries, bandwidth monitoring, CSV export

### Solution Approach
VIXEN provides:
- **Research-Grade Infrastructure** - Testing, logging, profiling built-in from the start
- **Reproducible Results** - Deterministic execution, fixed timestep loops
- **Automated Testing** - Headless rendering, 180-config test matrix
- **Open Source Research** - All code, data, and analysis tools publicly available
- **Academic Rigor** - 24-paper bibliography, validated metrics

## How It Should Work

### Research Workflow

**Test Execution**:
1. Configure test matrix (JSON specification)
2. Run automated benchmark suite (headless rendering)
3. Collect metrics (frame time, GPU time, bandwidth, VRAM)
4. Export results (CSV with 300 frames per configuration)
5. Analyze data (statistical summaries, visualizations)

**Development Workflow**:
1. **Write tests first** - GoogleTest suites with expected behavior
2. **Implement nodes** - TypedNode with compile-time slot validation
3. **Run coverage** - LCOV visualization in VS Code
4. **Check logs** - ILoggable interface with LOG_* macros
5. **Profile performance** - Timestamp queries, bandwidth counters

### Current Platform Capabilities

**Graph Construction**:
- 19+ node types (Window, Device, SwapChain, RenderPass, Framebuffer, Pipeline, etc.)
- TypedNode API with `In()`/`Out()` compile-time validation
- Variadic nodes for dynamic slot arrays
- ConnectVariadic API for runtime wiring

**Execution Model**:
- Multi-rate loop system (per-frame, fixed 60Hz, fixed 120Hz)
- Frame-in-flight synchronization (4 concurrent frames)
- Two-tier sync (CPU-GPU fences, GPU-GPU semaphores)
- Present fences (VK_EXT_swapchain_maintenance1)

**Infrastructure**:
- Testing framework (40% coverage, 10 test suites)
- Logging system (LOG_TRACE/DEBUG/INFO/WARNING/ERROR)
- Context system (SetupContext, CompileContext, ExecuteContext, CleanupContext)
- Lifecycle hooks (6 graph phases + 8 node phases = 14 hooks)

**Performance**:
- Persistent cache (9 cachers with async save/load)
- Precompiled headers (2-3× build speedup)
- Zero-overhead abstractions (std::variant, templates)
- Zero validation errors

### Expected Output (Automated Testing)
- CSV files with per-frame metrics (frame_time, gpu_time, bandwidth, vram)
- Statistical summaries (min, max, mean, stddev, percentiles)
- Performance comparisons (pipeline A vs B at resolution X)
- Visualization-ready data (JSON export)

## Design Philosophy

### Research-Grade Quality
- **Reproducible Results** - Deterministic execution, fixed timestep loops
- **Validated Metrics** - Compare bandwidth measurements against Nsight Graphics
- **Statistical Rigor** - Rolling window statistics, percentile analysis
- **Open Source** - All code, data, and methodology publicly available

### Type Safety First
- Compile-time slot validation (`In()`/`Out()` API)
- Zero-overhead abstractions (std::variant, templates)
- RAII throughout (smart pointers, no raw new/delete)
- Const-correctness on member functions

### Test-Driven Architecture
- Write tests before implementation
- 40% coverage target (critical paths)
- GoogleTest integration with VS Code Test Explorer
- LCOV visualization for coverage gaps

### Infrastructure-First Development
- Testing framework (GoogleTest, LCOV)
- Logging system (ILoggable, LOG_* macros)
- Context system (phase-specific typed contexts)
- Lifecycle hooks (14 hooks for fine-grained control)

## Future Vision

**Short-Term** (Phases H-N, by May 2026):
- Complete 4 ray tracing/marching pipelines
- Automated testing framework (180 configurations)
- Performance profiling system
- Research paper submission

**Long-Term** (Phases N+1, N+2, by August 2026):
- Hybrid RTX surface-skin pipeline (publication-worthy innovation)
- GigaVoxels streaming (128× memory reduction)
- Extended research (270 configurations total)
- Journal publication

Each addition maintains research focus: reproducible, validated, publishable results.
