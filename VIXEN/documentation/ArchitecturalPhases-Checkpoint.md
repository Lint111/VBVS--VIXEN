# Architectural Phases Implementation Checkpoint

**Project**: VIXEN RenderGraph Architecture → Voxel Ray Tracing Research Platform
**Started**: October 31, 2025
**Updated**: November 2, 2025 (Phase F COMPLETE - Bundle refactor done)
**Status**: Phase G (Compute Pipeline) - Ready to start
**Research Timeline**: 28-31 weeks post-Phase F completion

---

## MAJOR DIRECTION SHIFT: Research Integration 🎯

**New Primary Goal**: Transform VIXEN into a voxel ray tracing research platform for comparative pipeline analysis.

**Research Question**: How do different Vulkan ray tracing/marching pipeline architectures affect rendering performance, GPU bandwidth utilization, and scalability for data-driven voxel rendering?

**Test Scope**: 180 configurations (4 pipelines × 5 resolutions × 3 densities × 3 algorithms)

**Target Timeline**: Complete by May 2026 (research paper submission)

---

## Implementation Path UPDATE

### Completed Infrastructure ✅
**Phase 0**: Execution Model Correctness ✅
- 0.1-0.7: Synchronization, loops, present fences, auto message types
- Multi-rate loop system (LoopManager with fixed-timestep)
- Frame-in-flight synchronization (MAX_FRAMES_IN_FLIGHT=4)
- Command buffer recording strategy (StatefulContainer)

**Phase A**: Persistent Cache Infrastructure ✅
- Lazy deserialization pattern
- 9 cachers implemented (CACHE HIT verified)
- Stable device IDs (hash-based)
- Async save/load

**Phase B**: Encapsulation + Thread Safety ✅
- INodeWiring interface (removed friend declarations)
- Thread safety documentation (single-threaded model)

**Phase C**: Event Processing + Validation ✅
- Event processing sequencing verified
- SlotRole enum for slot lifetime semantics
- Render pass compatibility validation infrastructure

**Phase F**: Bundle-First Organization ✅
- Bundle struct consolidation (inputs/outputs aligned per task)
- TypedNodeInstance updated for bundle-first indexing
- ResourceDependencyTracker updated for bundle iteration
- All nodes compile successfully with new structure
- **Status**: COMPLETE (Build successful, all tests pass)

### Research Phases (Post-Phase F) 🔬

**Phase G**: Compute Shader Pipeline (HIGHEST PRIORITY)
- **Duration**: 2-3 weeks
- **Goal**: Simplest ray marching implementation
- **Why First**: Validates profiling methodology before RT complexity
- **Deliverables**: ComputePipelineNode, storage resources, timestamp queries, basic ray marching shader

**Phase H**: Voxel Data Infrastructure
- **Duration**: 3-4 weeks
- **Goal**: Sparse voxel octree (SVO) data structure
- **Deliverables**: Octree storage, procedural generation, GPU buffer upload, traversal utilities

**Phase I**: Performance Profiling System
- **Duration**: 2-3 weeks
- **Goal**: Automated metrics collection and export
- **Deliverables**: PerformanceProfiler, GPU performance counters, CSV export, benchmark configuration

**Phase J**: Fragment Shader Ray Marching
- **Duration**: 1-2 weeks
- **Goal**: Traditional rasterization-based ray marching
- **Deliverables**: Fragment shader pipeline, 3D texture support

**Phase K**: Hardware Ray Tracing Pipeline
- **Duration**: 4-5 weeks
- **Goal**: VK_KHR_ray_tracing_pipeline implementation
- **Deliverables**: BLAS/TLAS acceleration structures, custom AABB intersection, ray tracing dispatch

**Phase L**: Pipeline Variants & Optimization
- **Duration**: 3-4 weeks
- **Goal**: Traversal optimizations + hybrid pipeline
- **Deliverables**: Empty space skipping, BlockWalk traversal, dynamic scene updates, hybrid compute+RT

**Phase M**: Automated Testing Framework
- **Duration**: 3-4 weeks
- **Goal**: 180-configuration test execution
- **Deliverables**: Benchmark runner, headless mode, camera paths, result aggregator

**Phase N**: Research Execution & Analysis
- **Duration**: 2-3 weeks
- **Goal**: Execute tests, analyze results, generate visualizations
- **Deliverables**: Full test matrix results, statistical analysis, performance visualizations

---

## Updated Phase Priority Table

| Phase | Priority | Time Est | Status | Purpose |
|-------|----------|----------|--------|---------|
| **0** | 🔴 CRITICAL | 60h | ✅ COMPLETE | Execution correctness |
| **A** | ⭐⭐⭐ HIGH | 5-8h | ✅ COMPLETE | Cache infrastructure |
| **B** | ⭐⭐⭐ HIGH | 2h | ✅ COMPLETE | Encapsulation |
| **C** | ⭐⭐⭐ HIGH | 45m | ✅ COMPLETE | Validation |
| **F** | ⭐⭐⭐ HIGH | ~20h | ✅ COMPLETE | Bundle-first refactor |
| **G** | 🎯 RESEARCH | 2-3 weeks | 🔄 NEXT | Compute ray march |
| **H** | 🎯 RESEARCH | 3-4 weeks | ⏳ PENDING | Voxel data |
| **I** | 🎯 RESEARCH | 2-3 weeks | ⏳ PENDING | Profiling system |
| **J** | 🎯 RESEARCH | 1-2 weeks | ⏳ PENDING | Fragment shader |
| **K** | 🎯 RESEARCH | 4-5 weeks | ⏳ PENDING | Hardware RT |
| **L** | 🎯 RESEARCH | 3-4 weeks | ⏳ PENDING | Optimizations |
| **M** | 🎯 RESEARCH | 3-4 weeks | ⏳ PENDING | Automation |
| **N** | 🎯 RESEARCH | 2-3 weeks | ⏳ PENDING | Research execution |
| **D** | ⭐⭐ MEDIUM | 8-12h | ⏸️ DEFERRED | Execution waves |
| **E** | ⭐ LOW | 17-22h | ⏸️ DEFERRED | Hot reload |
| **G-OLD** | ⭐⭐ MEDIUM | 40-60h | ❌ CANCELLED | Visual editor |

**Total Research Timeline**: 28-31 weeks (7 months)

---

## Explicitly Deferred Features ❌

These features are NOT required for research and are postponed indefinitely:

### ❌ Visual Graph Editor
- Drag-and-drop node editing
- Real-time graph visualization
- Connection editing UI
**Rationale**: Research uses programmatic graph construction only.

### ❌ Material System
- PBR material editor
- Texture streaming
- Material variants
**Rationale**: Voxel rendering uses simple solid colors/procedural generation.

### ❌ Advanced UI (ImGui Beyond Profiler)
- Scene inspection tools
- Real-time parameter tweaking
- Debug visualization toggles
**Rationale**: Automated tests use fixed parameters. Minimal profiler overlay only.

### ❌ Asset Pipeline
- GLTF/OBJ model importers
- Texture compression pipeline
- Asset hot-reload
**Rationale**: Procedural voxel generation only - no external assets.

### ❌ Advanced Lighting
- Shadow mapping (except as optional secondary rays)
- Screen-space ambient occlusion
- Global illumination
**Rationale**: Research focuses on primary ray throughput and traversal performance.

### ❌ Phase D: Execution Waves
- Wave-based parallel execution
- Multi-threaded node dispatch
**Rationale**: Single-threaded deterministic execution required for reproducible research results.

### ❌ Phase E: Hot Reload
- Runtime shader hot-reload
- Dynamic pipeline replacement
**Rationale**: Automated tests use fixed shader variants. No interactive development needed.

---

## Research Phase Ordering Rationale

### Why Compute First (Phase G)?
1. **Simplest pipeline** - No RT extensions, validates profiling methodology
2. **Immediate visual output** - Confirms correctness early
3. **Foundation for hybrid** - Reusable code for Phase L
4. **Low risk** - Compute shaders are well-documented

### Why Voxel Data Early (Phase H)?
1. **Critical path dependency** - All pipelines require voxel data
2. **Octree complexity** - Time-consuming, benefits from early start
3. **Testing foundation** - Enables pipeline validation

### Why Hardware RT Late (Phase K)?
1. **Most complex** - BLAS/TLAS, SBT, custom intersection shaders
2. **High learning curve** - Ray tracing extensions are advanced
3. **Not on critical path** - Compute/fragment can proceed in parallel

### Why Automation Last (Phase M)?
1. **All pipelines must work first** - Can't automate broken implementations
2. **Requires stable APIs** - Avoids costly rework
3. **Research depends on it** - But can develop pipelines without it

---

## Phase F: Array Processing & Slot Tasks 🔄

**Status**: IN PROGRESS (Planning complete, ready for implementation)
**Time Estimate**: 16-21 hours
**Relevance to Research**: Enables parallel voxel loading/processing with automatic resource scaling

### Core Innovation

**Slot Tasks = Virtual Node Specializations**

Instead of creating separate nodes for similar workloads (AlbedoLoader, NormalLoader, RoughnessLoader), a single node can have multiple slot tasks, each representing a configuration variant.

**Example**:
```cpp
// Single ImageLoaderNode with 3 slot tasks
Task 0: Load albedo maps  (sRGB, BC1, gamma correction)
Task 1: Load normal maps  (Linear, BC5, no gamma)
Task 2: Load roughness    (Linear, BC4, single channel)
```

### Three-Tier Lifecycle
```cpp
Node Level (shared)    → SetupNode(), CleanupNode()
Task Level (per-config) → CompileTask(taskIdx), CleanupTask(taskIdx)
Instance Level (per-data) → ExecuteInstance(taskIdx, instanceIdx)
```

### Implementation Phases

**F.0**: Slot Metadata Consolidation (2-3h)
- SlotScope, SlotNullability, SlotMutability enums
- AUTO_INPUT/AUTO_OUTPUT macros with embedded counter

**F.1**: Resource Budget Manager (3-4h)
- Device capability tracking (VkPhysicalDeviceLimits)
- Static reservation + dynamic query API

**F.2**: Slot Task Infrastructure (5-6h)
- SlotTask struct, AutoGenerateSlotTasks()
- Task-local indexing with OutLocal() helper

**F.3**: Budget-Based Parallelism (4-5h)
- Thread pool for CPU-parallel tasks
- Vulkan batch submission for GPU tasks

**F.4**: InstanceGroup Migration (2-3h)
- Deprecate old InstanceGroup class
- Migration guide for existing multi-instance nodes

### Success Criteria
- ✅ Single ImageLoaderNode handles 3+ texture types
- ✅ Parallel instance spawning (4+ threads)
- ✅ Budget manager prevents exhaustion (1000+ tasks)
- ✅ Zero regressions in single-task nodes

**DO NOT INTERRUPT** - Complete Phase F before starting Phase G.

---

## Key Research Requirements

### Test Matrix Configuration
```json
{
  "pipelines": ["compute", "fragment", "hardware_rt", "hybrid"],
  "resolutions": [32, 64, 128, 256, 512],
  "densities": [0.2, 0.5, 0.8],
  "algorithms": ["baseline", "empty_skip", "blockwalk"],
  "scenes": ["sphere", "terrain", "architectural"],
  "frames": { "warmup": 60, "measurement": 300 }
}
```

**Total Configurations**: 4 × 5 × 3 × 3 = 180 tests

### Required Metrics (Per Frame)
- **Primary**: Frame time (ms), GPU time (ms), ray throughput (Mrays/s)
- **Bandwidth**: Read (GB/s), write (GB/s)
- **Memory**: VRAM usage (MB), bandwidth efficiency (rays/GB)
- **Traversal**: Average voxels tested per ray
- **Statistics**: Min, max, mean, stddev, percentiles (1st, 50th, 99th)

### Output Format
```csv
frame,timestamp_ms,frame_time_ms,gpu_time_ms,bandwidth_read_gb,bandwidth_write_gb,vram_mb,rays_per_sec,voxels_per_ray
0,0.0,16.7,14.2,23.4,8.1,2847,124000000,23.4
```

---

## Risk Mitigation Strategies

### Risk: Phase F Takes Longer Than Expected
**Mitigation**: Phase F bounded at 21h max. If exceeded by >20%, reassess slot task scope (simplify budget system).

### Risk: Hardware RT Extension Unavailable
**Mitigation**: Research proceeds with compute + fragment only (fallback plan in research proposal).

### Risk: Bandwidth Metrics Inaccurate
**Mitigation**: Early validation in Phase I against NVIDIA Nsight Graphics baseline.

### Risk: Test Execution Time Too Long
**Mitigation**: Reduce frame count (300→150), parallelize on multiple GPUs, or reduce resolution levels.

### Risk: VRAM Exhaustion at 512³
**Mitigation**: Octree compression (SVO sparse representation), reduce max resolution to 256³.

### Risk: Timeline Overrun
**Mitigation**: Scope reduction options:
- Skip hybrid pipeline (saves 2-3 weeks)
- Reduce to 3 resolution levels (128³, 256³, 512³)
- Reduce densities to 2 levels (sparse, dense)

---

## Success Metrics

### Technical Milestones
- ✅ Phase F complete → Slot task system working with parallel instances
- ✅ Phase G complete → Compute ray marching renders voxel cube
- ✅ Phase H complete → 256³ octree loads in <100ms
- ✅ Phase I complete → Profiler collects 60fps metrics with <1% overhead
- ✅ Phase K complete → Hardware RT renders same scene as compute
- ✅ Phase M complete → Automated test runs 180 configs unattended

### Research Milestones
- ✅ All 180 configurations tested successfully
- ✅ Bandwidth measurements validated (±5% vs Nsight)
- ✅ Performance trends identified (support/refute hypotheses)
- ✅ Results publishable (conference paper quality)

---

## Timeline Summary

| Milestone | Completion Date | Status |
|-----------|----------------|--------|
| Phase F | November 2, 2025 | ✅ COMPLETE |
| Phase G | Week of Nov 24, 2025 | 🔄 STARTING |
| Phase H | Week of Dec 22, 2025 | ⏳ PLANNED |
| Phase I | Week of Jan 12, 2026 | ⏳ PLANNED |
| Phases J-K | Week of Mar 2, 2026 | ⏳ PLANNED |
| Phase L | Week of Mar 30, 2026 | ⏳ PLANNED |
| Phase M | Week of Apr 27, 2026 | ⏳ PLANNED |
| Phase N | Week of May 18, 2026 | ⏳ PLANNED |

**Research Paper Submission**: May 31, 2026 (target)

---

## Documentation Updates

### New Documents Created
- ✅ `documentation/VoxelRayTracingResearch-TechnicalRoadmap.md` - Complete research implementation plan
- ✅ `documentation/ArchitecturalReview-2025-11-02-PhaseF.md` - Slot task system architecture

### Memory Bank Updates (Pending)
- ⏳ `memory-bank/activeContext.md` - Add research focus
- ⏳ `memory-bank/progress.md` - Update phase priorities
- ⏳ `memory-bank/projectbrief.md` - Add research goals

### Research Documents (To Create)
- ⏳ `research/TestMatrix.md` - Detailed configuration specification
- ⏳ `research/Hypotheses.md` - Expected outcomes per hypothesis
- ⏳ `research/ResultsAnalysis.md` - Post-execution findings

---

## Current Session Next Steps

1. ✅ **Complete Phase F implementation** (16-21h)
   - F.0: Slot metadata consolidation
   - F.1: Resource budget manager
   - F.2: Slot task infrastructure
   - F.3: Budget-based parallelism
   - F.4: InstanceGroup migration

2. ⏳ **Review Phase G specification** (`VoxelRayTracingResearch-TechnicalRoadmap.md`)
   - Understand compute pipeline requirements
   - Plan storage resource types
   - Design timestamp query integration

3. ⏳ **Begin Phase G.1** (ComputePipelineNode)
   - Create ComputePipelineCacher
   - Implement pipeline creation and caching
   - Test with simple compute shader

---

## Reference Documents

**Research Planning**:
- `documentation/VoxelRayTracingResearch-TechnicalRoadmap.md` - 9-phase research implementation plan
- `Research Question Proposal.md` (external) - Original research proposal with test matrix

**Architecture Reviews**:
- `documentation/ArchitecturalReview-2025-11-02-PhaseF.md` - Slot task system detailed design
- `documentation/ArchitecturalReview-2025-11-01.md` - Phase B and C completion
- `documentation/ArchitecturalReview-2025-10-31.md` - Phase A completion
- `documentation/ArchitecturalReview-2025-10.md` - Original blind spot analysis

**Memory Bank**:
- `memory-bank/activeContext.md` - Current focus (Phase F)
- `memory-bank/progress.md` - Completed systems inventory
- `memory-bank/systemPatterns.md` - Design pattern catalog

**Phase Plans**:
- `documentation/Phase-B-Plan.md` - Advanced rendering features (DEFERRED)
- `documentation/Phase0.4-MultiRateLoop-Plan.md` - LoopManager implementation (COMPLETE)

---

## Notes

**Architecture Status**: Production-ready foundation (Phases 0, A, B, C complete)

**Current Focus**: Phase F (Array Processing) - 16-21h remaining

**Research Timeline**: 28-31 weeks from Phase F completion

**Major Pivot**: Shifted from general rendering engine to specialized voxel ray tracing research platform. Visual editor, material system, and advanced UI features deferred indefinitely.

**Critical Path**: F → G → H → I → (J, K, L parallel) → M → N

**Confidence Level**: HIGH - All prerequisites complete, clear implementation roadmap, realistic timeline with scope reduction options.