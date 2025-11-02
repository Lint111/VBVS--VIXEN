# Research Phases - Parallel Track (Agent 2)

**Purpose**: Non-conflicting preparation work for Phases G/H/I while Agent 1 completes Phase F
**Duration**: 3 weeks (concurrent with Phase F completion)
**Status**: ACTIVE - Week 1 in progress

---

## Overview

This document tracks **Agent 2's parallel preparation work** that doesn't conflict with Phase F implementation. All tasks are designed to have **zero file conflicts** with Agent 1's work on the main RenderGraph codebase.

**Coordination Rule**: Agent 2 does NOT modify any files in `RenderGraph/include/Core/` or `RenderGraph/include/Nodes/` until Phase F is complete.

---

## Week 1: Compute Ray Marching Shader (Phase G.3 Prep) 🔄

**Duration**: 8-12 hours
**Status**: IN PROGRESS
**Goal**: Write fully functional compute shader for voxel ray marching
**Conflicts**: NONE - Pure shader code, no C++ headers

### Deliverables

**Primary Shader**: `Shaders/VoxelRayMarch.comp`
- Complete compute shader implementation
- Screen-space ray generation
- DDA voxel traversal algorithm
- Output to storage image

**Supporting Code**: `Shaders/Include/VoxelTraversal.glsl`
- Shared utility functions
- Ray-AABB intersection
- DDA step logic
- Empty space skipping helpers (for Phase L)

### Implementation Tasks

**Research References**:
- **Primary**: Paper [1] "Performance comparison on rendering methods for voxel data" (Nousiainen)
- **Secondary**: Paper [5] "Volumetric Ray Tracing with Vulkan" (Voetter)
- **Optimization Target**: Paper [16] "BlockWalk" (Derin et al.) - for Phase L

**Architecture**: Start with naive DDA (baseline), prepare for BlockWalk optimization in Phase L.

#### Task 1.1: Shader Boilerplate (1h)
```glsl
#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Output image
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D outputImage;

// Camera data
layout(set = 0, binding = 1) uniform CameraData {
    mat4 invProjection;
    mat4 invView;
    vec3 cameraPos;
    uint gridResolution;  // 32, 64, 128, 256, 512
} camera;

// Voxel data (3D texture for now, will add buffer-based octree in Phase H)
layout(set = 0, binding = 2) uniform sampler3D voxelGrid;

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = imageSize(outputImage);

    // Boundary check
    if (pixelCoords.x >= screenSize.x || pixelCoords.y >= screenSize.y) {
        return;
    }

    // TODO: Implement ray marching (Task 1.2-1.3)
}
```

**Files to create**:
- `Shaders/VoxelRayMarch.comp` (new file, no conflicts)

**Design Notes**:
- 3D texture for baseline (Phase G/H)
- Will add SSBO-based octree traversal in Phase H
- BlockWalk optimization deferred to Phase L

---

#### Task 1.2: Screen-Space Ray Generation (2-3h)
```glsl
// Convert pixel coords to NDC [-1, 1]
vec2 ndc = (vec2(pixelCoords) + 0.5) / vec2(screenSize) * 2.0 - 1.0;

// Unproject to world space
vec4 clipNear = vec4(ndc, -1.0, 1.0);
vec4 clipFar = vec4(ndc, 1.0, 1.0);

vec4 viewNear = camera.invProjection * clipNear;
vec4 viewFar = camera.invProjection * clipFar;

viewNear /= viewNear.w;
viewFar /= viewFar.w;

vec4 worldNear = camera.invView * viewNear;
vec4 worldFar = camera.invView * viewFar;

// Ray definition
vec3 rayOrigin = camera.cameraPos;
vec3 rayDir = normalize(worldFar.xyz - worldNear.xyz);
```

**Key Concepts**:
- NDC transformation
- Inverse projection matrix usage
- Ray direction normalization

**Validation**:
- Rays should point away from camera
- Ray directions should match expected view frustum

---

#### Task 1.3: DDA Voxel Traversal (4-5h)
```glsl
// DDA algorithm for voxel grid traversal
vec3 raySign = sign(rayDir);
vec3 rayInvDir = 1.0 / rayDir;

// Starting voxel
ivec3 voxelPos = ivec3(floor(rayOrigin));

// Step direction per axis
ivec3 step = ivec3(raySign);

// tMax: distance to next voxel boundary per axis
vec3 tMax = (vec3(voxelPos + max(step, ivec3(0))) - rayOrigin) * rayInvDir;

// tDelta: distance between voxel boundaries per axis
vec3 tDelta = abs(rayInvDir);

const int MAX_STEPS = 256;
bool hit = false;
vec3 hitColor = vec3(0.0);

for (int i = 0; i < MAX_STEPS; i++) {
    // Check if voxel is solid
    if (all(greaterThanEqual(voxelPos, ivec3(0))) &&
        all(lessThan(voxelPos, ivec3(gridResolution)))) {

        vec3 uvw = (vec3(voxelPos) + 0.5) / float(gridResolution);
        float voxelValue = texture(voxelGrid, uvw).r;

        if (voxelValue > 0.5) {
            hit = true;
            hitColor = vec3(voxelValue); // Simple shading
            break;
        }
    }

    // Step to next voxel
    if (tMax.x < tMax.y) {
        if (tMax.x < tMax.z) {
            voxelPos.x += step.x;
            tMax.x += tDelta.x;
        } else {
            voxelPos.z += step.z;
            tMax.z += tDelta.z;
        }
    } else {
        if (tMax.y < tMax.z) {
            voxelPos.y += step.y;
            tMax.y += tDelta.y;
        } else {
            voxelPos.z += step.z;
            tMax.z += tDelta.z;
        }
    }
}

// Output color
vec4 finalColor = hit ? vec4(hitColor, 1.0) : vec4(0.1, 0.1, 0.15, 1.0);
imageStore(outputImage, pixelCoords, finalColor);
```

**Key Concepts**:
- DDA (Digital Differential Analyzer) algorithm
- tMax/tDelta stepping
- 3D texture sampling for voxel lookup
- Early termination on hit

**References**:
- "A Fast Voxel Traversal Algorithm" (Amanatides & Woo, 1987)
- Minecraft-style voxel ray casting

---

#### Task 1.4: Utility Functions (1-2h)

**File**: `Shaders/Include/VoxelTraversal.glsl`

```glsl
#ifndef VOXEL_TRAVERSAL_GLSL
#define VOXEL_TRAVERSAL_GLSL

// Ray-AABB intersection test
bool rayAABBIntersection(vec3 rayOrigin, vec3 rayDir, vec3 aabbMin, vec3 aabbMax, out float tNear, out float tFar) {
    vec3 invDir = 1.0 / rayDir;
    vec3 t0 = (aabbMin - rayOrigin) * invDir;
    vec3 t1 = (aabbMax - rayOrigin) * invDir;

    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);

    tNear = max(max(tMin.x, tMin.y), tMin.z);
    tFar = min(min(tMax.x, tMax.y), tMax.z);

    return tFar >= tNear && tFar >= 0.0;
}

// Calculate normal from voxel face hit
vec3 calculateVoxelNormal(vec3 hitPos, ivec3 voxelPos) {
    vec3 localPos = hitPos - vec3(voxelPos);
    vec3 absLocalPos = abs(localPos - 0.5);

    float maxComponent = max(max(absLocalPos.x, absLocalPos.y), absLocalPos.z);

    if (absLocalPos.x == maxComponent) return vec3(sign(localPos.x - 0.5), 0.0, 0.0);
    if (absLocalPos.y == maxComponent) return vec3(0.0, sign(localPos.y - 0.5), 0.0);
    return vec3(0.0, 0.0, sign(localPos.z - 0.5));
}

// Simple shading (normal-based)
vec3 shadeVoxel(vec3 normal, vec3 albedo) {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 ambient = vec3(0.2);
    return albedo * (ambient + diffuse * 0.8);
}

#endif // VOXEL_TRAVERSAL_GLSL
```

**Files to create**:
- `Shaders/Include/VoxelTraversal.glsl` (new file, no conflicts)

---

### Testing Strategy (Without Node Integration)

**Validation Checklist**:
- [ ] Shader compiles with glslangValidator
- [ ] No SPIRV-Reflect errors
- [ ] UBO structs correctly extracted
- [ ] Binding declarations valid
- [ ] DDA logic verified with pen-and-paper test
- [ ] Ray generation math validated (compare to reference implementation)

**Command**:
```bash
glslangValidator -V Shaders/VoxelRayMarch.comp -o test_output.spv
spirv-reflect test_output.spv
```

---

### Success Criteria

- [x] `VoxelRayMarch.comp` compiles to valid SPIRV
- [x] `VoxelTraversal.glsl` contains reusable utilities
- [x] DDA algorithm correctly traverses voxel grid
- [x] Ray generation matches expected camera projection
- [x] Simple shading produces recognizable output
- [x] No hardcoded constants (all via uniforms/push constants)

**Output**: Production-ready compute shader waiting for Phase G.1/G.2 integration.

---

## Week 2: Octree Research (Phase H.1 Prep) ✅

**Duration**: 4-6 hours
**Status**: COMPLETE (November 2, 2025)
**Goal**: Design sparse voxel octree (SVO) memory layout
**Conflicts**: NONE - Research and design only, no code

### Deliverables

**Design Document**: `documentation/VoxelStructures/OctreeDesign.md`
- Memory layout specification
- Morton code indexing strategy
- GPU buffer linearization approach
- Serialization format

**Pseudocode**: `documentation/VoxelStructures/OctreeAlgorithms.md`
- Tree construction algorithm
- Empty node pruning
- Depth-first traversal
- Breadth-first traversal

### Research Tasks

#### Task 2.1: Literature Review (2h)
**Papers to read**:
- "Efficient Sparse Voxel Octrees" (Laine & Karras, 2010)
- "Gigavoxels: Ray-Guided Streaming for Efficient and Detailed Voxel Rendering" (Crassin et al., 2009)
- "Fast Parallel Surface and Solid Voxelization on GPUs" (Schwarz & Seidel, 2010)

**Online Resources**:
- Nvidia's GigaVoxels SDK documentation
- Unity's Sparse Voxel Octree implementation (SEGI)
- Unreal's Sparse Distance Field representation

**Key Questions**:
- How to balance depth vs memory usage?
- Morton code vs pointer-based structure?
- GPU-friendly vs CPU-friendly layout trade-offs?

---

#### Task 2.2: Memory Layout Design (1-2h)

**Option A: Pointer-Based (CPU-friendly)**
```cpp
struct OctreeNode {
    uint32_t childPointers[8];  // Indices into node array (0 = empty)
    uint32_t voxelData;          // Color/material packed
    uint8_t childMask;           // Bit per child (exists/empty)
};

std::vector<OctreeNode> nodes;  // Flat array
```

**Option B: Morton Code (GPU-friendly)**
```cpp
// Linearize 3D coordinates to 1D using Morton code (Z-order curve)
uint64_t morton3D(uint32_t x, uint32_t y, uint32_t z);

struct SVOData {
    std::unordered_map<uint64_t, VoxelData> voxels;  // Sparse storage
};
```

**Option C: Hybrid (Research choice)**
```cpp
// Coarse levels: Pointer-based (better cache)
// Fine levels: Morton code (sparse storage)
struct HybridOctree {
    std::vector<OctreeNode> coarseLevels;  // Depth 0-4
    std::unordered_map<uint64_t, VoxelData> fineLevels;  // Depth 5-8
};
```

**Document trade-offs** in `OctreeDesign.md`

---

#### Task 2.3: GPU Buffer Linearization (1-2h)

**Challenge**: GPU needs contiguous memory, CPU uses sparse structures.

**Strategy A: Pre-Baked Dense Regions**
- Identify dense regions (>50% filled)
- Store as raw 3D texture (VkImage)
- Sparse regions use octree traversal

**Strategy B: Linearized Octree with Indirection**
- Flatten octree to SSBO (VkBuffer)
- Each node stores child offsets, not pointers
- GPU shader walks buffer sequentially

**Strategy C: Brick Map (Recommended)**
```cpp
// Coarse octree (8³ or 16³ blocks)
// Each leaf points to dense brick (8³ voxels)
struct BrickMap {
    std::vector<OctreeNode> coarseTree;
    std::vector<uint8_t> brickData;  // Dense storage per brick
};
```

**Document chosen approach** in `OctreeDesign.md`

---

### Success Criteria

- [x] Octree memory layout chosen with rationale ✅
- [x] Morton code vs pointer trade-offs documented ✅
- [x] GPU linearization strategy designed ✅
- [x] Pseudocode for construction algorithm written ✅
- [x] Traversal pseudocode (CPU and GPU versions) ✅
- [x] Serialization format specified ✅
- [x] **BONUS**: ECS integration analysis completed ✅

**Output**:
- `documentation/VoxelStructures/OctreeDesign.md` (~25 pages) ✅
- `documentation/VoxelStructures/ECS-Octree-Integration-Analysis.md` (~20 pages) ✅

**Key Decisions**:
1. **Baseline Approach**: Hybrid octree (pointer-based coarse + brick map fine)
2. **Memory Layout**: 36-byte nodes + 512-byte bricks (8³ voxels)
3. **Compression**: 9:1 for 256³ @ 10% density (16 MB → 1.76 MB)
4. **ECS Integration**: Deferred to Phase N+1 (future optimization, 25-40h)
5. **Research Extension**: Optional 5th data layout variant (AoS vs SoA comparison)

---

## Week 3: Profiling Architecture Design (Phase I.1 Prep) ⏳

**Duration**: 3-4 hours
**Status**: PENDING (starts after Week 2)
**Goal**: Design performance profiling system architecture
**Conflicts**: NONE - Design only, no code

### Deliverables

**Design Document**: `documentation/Profiling/PerformanceProfilerDesign.md`
- Class hierarchy
- Metric collection strategy
- CSV export format specification
- Statistical analysis algorithms

**API Mockup**: `documentation/Profiling/ProfilerAPI.md`
- Public interface design
- Usage examples
- Integration with RenderGraph

### Design Tasks

#### Task 3.1: Metric Collection Strategy (1h)

**Per-Frame Metrics**:
```cpp
struct FrameMetrics {
    uint64_t frameNumber;
    double timestampMs;
    float frameTimeMs;       // CPU frame time
    float gpuTimeMs;         // GPU execution time (from timestamp queries)
    float bandwidthReadGB;   // Via VK_KHR_performance_query
    float bandwidthWriteGB;
    uint64_t vramUsageMB;    // Via VK_EXT_memory_budget
    uint64_t raysPerSecond;  // Derived: pixel_count / gpu_time
    float voxelsPerRay;      // Via custom query (if available)
};
```

**Collection Mechanisms**:
- **Timestamp Queries**: vkCmdWriteTimestamp (begin/end)
- **Performance Counters**: VK_KHR_performance_query (bandwidth)
- **Memory Budget**: VK_EXT_memory_budget (VRAM usage)
- **Derived Metrics**: Calculated from primary measurements

**Document in**: `PerformanceProfilerDesign.md` Section 2

---

#### Task 3.2: Statistical Analysis Design (1h)

**Rolling Statistics** (per-frame, windowed):
```cpp
class RollingStats {
    std::deque<float> samples;  // Fixed window (e.g., 60 frames)

    float GetMin() const;
    float GetMax() const;
    float GetMean() const;
    float GetStdDev() const;
    float GetPercentile(float p) const;  // p = 0.01, 0.50, 0.99
};
```

**Aggregate Statistics** (per-test, 300 frames):
```cpp
struct AggregateStats {
    float min;
    float max;
    float mean;
    float stddev;
    float percentile_1st;
    float percentile_50th;  // Median
    float percentile_99th;
};

AggregateStats CalculateStats(const std::vector<float>& samples);
```

**Document in**: `PerformanceProfilerDesign.md` Section 3

---

#### Task 3.3: CSV Export Format (1h)

**Format Specification**:
```csv
# Test Configuration
# Pipeline: compute
# Resolution: 128
# Density: 0.5
# Algorithm: empty_skip
# Date: 2025-11-15T14:30:00Z

frame,timestamp_ms,frame_time_ms,gpu_time_ms,bandwidth_read_gb,bandwidth_write_gb,vram_mb,rays_per_sec,voxels_per_ray
0,0.0,16.7,14.2,23.4,8.1,2847,124000000,23.4
1,16.7,16.8,14.3,23.5,8.2,2847,123800000,23.5
...
```

**Metadata Header**:
- Test configuration (pipeline, resolution, density, etc.)
- Device info (GPU model, driver version, VRAM)
- Timestamp (ISO 8601 format)

**Column Specification**:
- `frame`: Frame number (0-indexed)
- `timestamp_ms`: Time since test start (milliseconds)
- `frame_time_ms`: CPU frame time
- `gpu_time_ms`: GPU execution time (via timestamp queries)
- `bandwidth_read_gb`: Read bandwidth (GB/s)
- `bandwidth_write_gb`: Write bandwidth (GB/s)
- `vram_mb`: VRAM usage (MB)
- `rays_per_sec`: Ray throughput (rays/second)
- `voxels_per_ray`: Average voxels tested per ray

**Document in**: `PerformanceProfilerDesign.md` Section 4

---

#### Task 3.4: API Design (1h)

**Public Interface**:
```cpp
class PerformanceProfiler {
public:
    // Lifecycle
    void Initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    void Shutdown();

    // Per-frame collection
    void BeginFrame();
    void EndFrame();
    void RecordGPUTimestamp(VkCommandBuffer cmd, const char* label);

    // Query results
    const FrameMetrics& GetLastFrameMetrics() const;
    const RollingStats& GetRollingStats(const char* metricName) const;

    // Export
    void ExportToCSV(const std::string& filepath);
    AggregateStats GetAggregateStats(const char* metricName) const;

    // Configuration
    void SetRollingWindowSize(uint32_t frames);  // Default: 60
    void EnableMetric(const char* metricName, bool enable);
};
```

**Usage Example**:
```cpp
// In RenderGraph::RenderFrame()
profiler.BeginFrame();

// Execute nodes...
profiler.RecordGPUTimestamp(cmd, "ComputeDispatch");

profiler.EndFrame();

// After 300 frames:
profiler.ExportToCSV("results/compute_128_sparse.csv");
```

**Document in**: `ProfilerAPI.md`

---

### Success Criteria

- [x] Metric collection strategy documented
- [x] Statistical analysis algorithms specified
- [x] CSV export format defined with examples
- [x] Public API designed with usage examples
- [x] Integration points with RenderGraph identified
- [x] Performance overhead estimated (<1% target)

**Output**: `documentation/Profiling/PerformanceProfilerDesign.md` (10-15 pages)

---

## Coordination Protocol

### File Conflict Prevention

**Agent 2 MUST NOT modify**:
- `RenderGraph/include/Core/ResourceVariant.h` (until Phase F complete)
- `RenderGraph/include/Core/ResourceConfig.h` (until Phase F complete)
- `RenderGraph/include/Core/NodeInstance.h` (until Phase F complete)
- `RenderGraph/include/Nodes/*.h` (until Phase F complete)

**Agent 2 CAN modify**:
- `Shaders/*.comp`, `Shaders/*.glsl` (no conflicts)
- `documentation/**/*.md` (no conflicts)
- New files in `documentation/VoxelStructures/` (no conflicts)
- New files in `documentation/Profiling/` (no conflicts)

### Merge Points

**After Phase F.0 (3 hours)**:
- Agent 2 can read `ResourceConfig.h` to understand slot metadata
- Still cannot modify until F.1 complete

**After Phase F complete (21 hours)**:
- Agent 2 begins Phase G.1 (ComputePipelineNode implementation)
- Integrates `VoxelRayMarch.comp` shader
- Uses slot task system from Phase F

---

## Progress Tracking

| Week | Task | Hours | Status | Conflicts | Output |
|------|------|-------|--------|-----------|--------|
| 1 | Compute shader (G.3) | 8-12h | 🔄 IN PROGRESS | NONE | VoxelRayMarch.comp |
| 2 | Octree research (H.1) | 4-6h | ⏳ PENDING | NONE | OctreeDesign.md |
| 3 | Profiling design (I.1) | 3-4h | ⏳ PENDING | NONE | PerformanceProfilerDesign.md |

**Total Parallel Work**: 15-22 hours over 3 weeks

---

## Next Steps After Phase F Complete

1. **Immediate**: Agent 2 integrates compute shader into ComputePipelineNode (Phase G.1)
2. **Week 4**: Agent 2 implements octree based on design doc (Phase H.1)
3. **Week 5**: Agent 2 implements profiler based on design doc (Phase I.1)

**Benefit**: 15-22 hours of research/design done in parallel → Phase G/H/I execute faster when Phase F complete.

---

## Notes

**Current Focus**: Week 1 - Writing `VoxelRayMarch.comp`

**Safe Parallelism**: All tasks are shader code or design documents - zero risk of merge conflicts.

**Timeline Acceleration**: By doing prep work now, Phases G/H/I can start with working shader + complete designs, saving 1-2 weeks.