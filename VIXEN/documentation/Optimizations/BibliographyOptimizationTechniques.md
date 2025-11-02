# Optimization Techniques from Research Bibliography

**Source**: `C:\Users\liory\Docs\Performance comparison on rendering methods for voxel data.pdf`
**Papers**: 24 research papers on voxel rendering, ray tracing, and GPU optimization
**Purpose**: Extract optimization techniques for Phase L (Pipeline Variants & Optimization)
**Date**: November 2, 2025

---

## Overview

This document synthesizes optimization techniques from the research bibliography to inform Phase L implementation. Techniques are categorized by **algorithm type**, **data structure**, and **hardware utilization**.

### Research Paper Index (Referenced Papers)

Based on previous analysis of the bibliography:

- **[1] Nousiainen** - "Performance comparison on rendering methods for voxel data" (Baseline comparison)
- **[2] Aokana** - Voxel rendering techniques
- **[5] Voetter** - "Volumetric Ray Tracing with Vulkan"
- **[6] Aleksandrov et al.** - Sparse voxel octrees
- **[16] Derin et al.** - "BlockWalk" (Efficient voxel traversal)
- **Additional 19 papers** covering ray tracing, octrees, GPU optimization, and data-driven rendering

---

## Category 1: Traversal Algorithm Optimizations

### 1.1 Empty Space Skipping

**Source**: Papers [1], [6], [16]
**Technique**: Skip large regions of empty voxels without testing individual voxels

#### Implementation Strategies

**A. Hierarchical Bitmask (Paper [6])**
```cpp
// Coarse grid (e.g., 16³ blocks in 128³ grid)
// Each bit = 1 if block contains any solid voxels, 0 if completely empty
struct HierarchicalGrid {
    std::vector<uint64_t> coarseBitmap;  // 1 bit per 16³ block
    std::vector<glm::u8vec4> voxelData;  // Dense data

    bool IsBlockEmpty(glm::ivec3 blockPos) const {
        uint32_t blockIndex = blockPos.x + blockPos.y * blocksPerSide + blockPos.z * blocksPerSide * blocksPerSide;
        uint64_t word = coarseBitmap[blockIndex / 64];
        return (word & (1ULL << (blockIndex % 64))) == 0;
    }
};
```

**Shader Integration** (Compute/Fragment):
```glsl
// Before DDA loop, check coarse grid
ivec3 blockPos = ivec3(rayOrigin) / 16;
if (IsBlockEmpty(blockPos)) {
    // Skip to next block boundary
    vec3 blockMax = vec3((blockPos + 1) * 16);
    float t = rayBoxIntersection(rayOrigin, rayDir, blockMax);
    rayOrigin += rayDir * t;
}
```

**Expected Speedup**: +30-50% for sparse scenes (10-30% density)

---

**B. Octree Empty Node Skipping (Paper [6])**
```cpp
// During octree traversal, skip entire subtrees if node is empty
struct OctreeNode {
    uint8_t childMask;  // Bitmask: which children exist (0 = empty subtree)
    // ...
};

// In shader:
if (node.childMask == 0) {
    // Entire subtree is empty, skip to sibling node
    continue;
}
```

**Shader Example** (GLSL):
```glsl
// Octree traversal with early exit
while (stackSize > 0) {
    OctreeNode node = octreeBuffer.nodes[nodeIndex];

    if (node.childMask == 0) {
        // Pop stack, go to next node
        nodeIndex = stack[--stackSize];
        continue;
    }

    // Process non-empty node...
}
```

**Expected Speedup**: +40-60% for sparse scenes with octree

---

### 1.2 BlockWalk Algorithm (Paper [16] - Derin et al.)

**Source**: Paper [16] - "Efficient Voxel Traversal for BlockWalk"
**Key Insight**: Exploit spatial coherence—adjacent rays often traverse similar voxels

#### Technique Description

1. **Divide screen into tiles** (e.g., 8×8 pixels)
2. **Compute representative ray** for tile center
3. **Pre-traverse blocks** along representative ray
4. **Cache block occupancy** in shared memory (compute shader) or registers (fragment shader)
5. **All rays in tile reuse cached data**

#### Implementation (Compute Shader)

```glsl
#version 460

layout(local_size_x = 8, local_size_y = 8) in;  // Tile size

// Shared memory for block cache (8x8 tile → ~64 blocks along ray)
shared uint blockOccupancy[64];  // 1 bit per voxel in block (or min/max occupancy)

void main() {
    ivec2 tileID = ivec2(gl_WorkGroupID.xy);
    ivec2 localID = ivec2(gl_LocalInvocationID.xy);

    // Thread 0,0 pre-traverses and fills cache
    if (localID == ivec2(0, 0)) {
        vec3 tileCenter = GetRayForPixel(tileID * 8 + ivec2(4, 4));
        PreTraverseBlocks(tileCenter, blockOccupancy);
    }

    barrier();  // Wait for cache population

    // All threads use cached data
    vec3 myRay = GetRayForPixel(tileID * 8 + localID);
    vec4 color = MarchWithCache(myRay, blockOccupancy);

    imageStore(outputImage, tileID * 8 + localID, color);
}
```

**Expected Speedup**: +25-35% for dense, regular scenes (urban grid)

**Trade-off**: Requires shared memory (compute shader only) or register spilling (fragment shader)

---

### 1.3 Beam/Frustum Traversal

**Source**: Papers [2], [5]
**Technique**: Trace beam/frustum instead of individual rays, test multiple pixels simultaneously

#### Cone Tracing Variant
```glsl
// Instead of thin ray, use expanding cone
float coneRadius = distance * tan(coneAngle);

// Sample voxel grid with trilinear filtering (mimics cone footprint)
vec4 voxelSample = textureLod(voxelGrid, uvw, log2(coneRadius));
```

**Expected Speedup**: +10-20% (fewer texture samples)

**Trade-off**: Lower quality (blurred), not suitable for comparison baseline

---

## Category 2: Data Structure Optimizations

### 2.1 Sparse Voxel Octree (SVO) Variants

**Source**: Papers [6], [2]
**Goal**: Reduce memory and improve cache locality

#### A. Brick Map Optimization (Hybrid)

See `OctreeDesign.md` for full specification. Key points:

- **Coarse levels (0-4)**: Pointer-based navigation (cache-friendly)
- **Fine levels (5-8)**: Dense 8³ bricks (memory-efficient)
- **Compression**: 9:1 for sparse scenes

**Shader Access**:
```glsl
// Navigate coarse octree to find brick
uint brickOffset = TraverseToLeaf(octreeRoot, voxelPos);

// Index into brick (8³ dense array)
ivec3 localPos = voxelPos % 8;
uint voxelIndex = brickOffset + (localPos.x + localPos.y * 8 + localPos.z * 64);
vec4 voxelColor = brickBuffer.data[voxelIndex];
```

---

#### B. SVO with Contour Data (Paper [6])

**Idea**: Store distance to nearest surface (SDF-like) in empty nodes

```cpp
struct OctreeNode {
    uint8_t childMask;
    uint8_t minDistance;  // Distance to nearest solid voxel (0-255)
    // ...
};
```

**Shader Usage**:
```glsl
if (node.childMask == 0 && node.minDistance > rayStepSize) {
    // Skip ahead by minDistance
    rayPos += rayDir * node.minDistance;
}
```

**Expected Speedup**: +15-25% for medium-density scenes

---

### 2.2 Morton Code Indexing

**Source**: Papers [2], [6]
**Technique**: Z-order curve for cache-friendly memory layout

#### Morton Encoding (CPU)
```cpp
// Interleave bits of x, y, z to create Morton code
uint64_t MortonEncode(uint32_t x, uint32_t y, uint32_t z) {
    auto expandBits = [](uint32_t v) -> uint64_t {
        uint64_t x = v & 0x1FFFFF;
        x = (x | x << 32) & 0x1F00000000FFFF;
        x = (x | x << 16) & 0x1F0000FF0000FF;
        x = (x | x << 8)  & 0x100F00F00F00F00F;
        x = (x | x << 4)  & 0x10C30C30C30C30C3;
        x = (x | x << 2)  & 0x1249249249249249;
        return x;
    };

    return expandBits(x) | (expandBits(y) << 1) | (expandBits(z) << 2);
}
```

**GPU Buffer Layout**:
```cpp
// Sort voxels by Morton code before upload
std::map<uint64_t, glm::u8vec4> voxelMap;  // Sorted by Morton code
for (auto& [mortonCode, color] : voxelMap) {
    // Upload in Morton order → better GPU cache locality
}
```

**Expected Speedup**: +5-10% (cache hit rate improvement)

---

## Category 3: GPU Hardware Optimizations

### 3.1 Wavefront/Warp Coherence

**Source**: Papers [1], [5], [16]
**Problem**: Divergent rays cause warp/wavefront stalls (threads wait for slowest thread)

#### Solution A: Ray Reordering (Advanced)

**Technique**: Sort rays by direction before tracing
```cpp
// CPU-side or compute shader pre-pass
struct RayBatch {
    std::vector<Ray> rays;
    glm::vec3 averageDirection;
};

// Bin rays by octant (8 bins based on sign of direction components)
std::array<RayBatch, 8> bins;
for (const auto& ray : allRays) {
    uint32_t octant = (ray.dir.x > 0 ? 1 : 0) |
                      (ray.dir.y > 0 ? 2 : 0) |
                      (ray.dir.z > 0 ? 4 : 0);
    bins[octant].rays.push_back(ray);
}

// Process each bin separately (better coherence)
for (auto& bin : bins) {
    TraceRayBatch(bin);
}
```

**Expected Speedup**: +10-20% (depends on scene complexity)

**Trade-off**: Significant implementation complexity, requires multi-pass

---

#### Solution B: Tile-Based Rendering (BlockWalk)

Already covered in 1.2. Tiles naturally group coherent rays.

---

### 3.2 Texture Cache Optimization

**Source**: Papers [2], [5]
**Technique**: Optimize 3D texture access patterns for GPU cache

#### A. Swizzled Texture Layout

**Vulkan**: Use `VK_IMAGE_TILING_OPTIMAL` (automatic swizzling)

```cpp
VkImageCreateInfo imageInfo = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_3D,
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .extent = {128, 128, 128},
    .tiling = VK_IMAGE_TILING_OPTIMAL,  // GPU-optimized layout
    .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
};
```

**Expected Speedup**: Already applied (Vulkan default)

---

#### B. Mipmapping for LOD

**Technique**: Use lower-resolution voxel data for distant pixels

```glsl
// Calculate LOD based on ray distance
float lod = log2(distance / baseDistance);

// Sample with mipmap
vec4 voxel = textureLod(voxelGrid, uvw, lod);
```

**Expected Speedup**: +15-25% (fewer cache misses for distant geometry)

**Trade-off**: Requires generating mipmaps (CPU or GPU pre-pass)

---

### 3.3 Bandwidth Reduction

**Source**: Papers [1], [5]
**Goal**: Minimize memory transfers (GPU bandwidth is limited)

#### A. Quantized Voxel Data

**Technique**: Store voxels as compressed format (e.g., RGB565, RGBA4444)

```cpp
// RGBA8 (32 bits) → RGB565 (16 bits) = 50% reduction
VkFormat compressedFormat = VK_FORMAT_R5G6B5_UNORM_PACK16;
```

**Expected Speedup**: +10-15% (memory-bound scenarios)

**Trade-off**: Color banding (acceptable for research comparison)

---

#### B. Block Compression (BC7/ASTC)

**Technique**: Use GPU texture compression

```cpp
// ASTC 4×4×4 3D blocks (8 bpp)
VkFormat compressedFormat = VK_FORMAT_ASTC_4x4x4_UNORM_BLOCK_EXT;
```

**Expected Speedup**: +20-30% (memory bandwidth dominated)

**Trade-off**: Quality loss, requires compression step

---

## Category 4: Algorithmic Variants (Phase L Targets)

### 4.1 Three Algorithm Variants for Testing

Based on bibliography analysis, Phase L should implement:

#### Variant 1: Baseline DDA (Already Implemented)
- **Source**: Amanatides & Woo (classic reference)
- **Characteristics**: No optimizations, pure stepping algorithm
- **Use**: Baseline for comparison

#### Variant 2: Empty Space Skipping
- **Technique**: Hierarchical bitmap (Section 1.1.A)
- **Implementation**: Add coarse grid check before DDA loop
- **Expected Improvement**: +30-50% for sparse scenes
- **Code Complexity**: Low (+100 lines)

#### Variant 3: BlockWalk
- **Source**: Paper [16] (Derin et al.)
- **Technique**: Tile-based traversal with shared memory cache (Section 1.2)
- **Expected Improvement**: +25-35% for dense scenes
- **Code Complexity**: Medium (+300 lines, compute shader only)

---

## Category 5: Hardware Ray Tracing Optimizations

**Source**: Papers [5], [8]

### 5.1 BLAS Optimization Strategies

#### A. Adaptive BLAS Granularity

**Problem**: 1 AABB per voxel = millions of primitives (slow BVH build)

**Solution**: Group voxels into larger AABBs (e.g., 4³ = 64 voxels per AABB)

```cpp
std::vector<VoxelAABB> BuildCoarseAABBs(const VoxelGrid& grid, uint32_t granularity) {
    std::vector<VoxelAABB> aabbs;

    for (uint32_t x = 0; x < grid.resolution; x += granularity) {
        for (uint32_t y = 0; y < grid.resolution; y += granularity) {
            for (uint32_t z = 0; z < grid.resolution; z += granularity) {
                // Check if block contains any solid voxels
                bool hasSolid = CheckBlockOccupancy(grid, x, y, z, granularity);

                if (hasSolid) {
                    aabbs.push_back({
                        .min = glm::vec3(x, y, z),
                        .max = glm::vec3(x + granularity, y + granularity, z + granularity)
                    });
                }
            }
        }
    }

    return aabbs;
}
```

**Trade-off**:
- Fewer AABBs → Faster BLAS build
- Larger AABBs → More false positives in intersection shader (must test individual voxels)

**Recommendation**: Test granularities of 1, 2, 4, 8 in Phase L

---

#### B. BLAS Build Flags

```cpp
VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
    // ...
    .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR  // Option 1: Fast trace
    // OR
    .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR  // Option 2: Fast build
};
```

**Research Question**: Does fast-trace or fast-build perform better for static voxel scenes?

**Phase L Test**: Compare both flags across all scenes

---

### 5.2 Custom Intersection Shader Optimization

**Problem**: Intersection shader called millions of times per frame (hot path)

**Optimization**: Early exit based on AABB granularity

```glsl
// If AABB represents single voxel → trivial accept
if (all(lessThan(aabbMax - aabbMin, vec3(1.01)))) {
    // Single voxel, guaranteed hit
    reportIntersectionEXT(tNear, 0);
    return;
}

// Otherwise, test individual voxels within AABB
for (int x = 0; x < granularity; ++x) {
    for (int y = 0; y < granularity; ++y) {
        for (int z = 0; z < granularity; ++z) {
            // Test voxel at (aabbMin + vec3(x, y, z))
        }
    }
}
```

**Expected Speedup**: +10-15% (reduces intersection shader overhead)

---

## Category 6: Hybrid Pipeline Strategies

**Source**: Papers [5], [16]
**Goal**: Combine strengths of multiple pipelines

### 6.1 Hybrid Compute + Fragment

**Technique**: Use compute shader for initial ray marching, fragment shader for final shading

**Pipeline**:
1. **Compute Pass**: March rays, output hit positions + normals to buffer
2. **Fragment Pass**: Read buffer, apply complex shading (GI, reflections)

**Advantage**: Decouple traversal (compute-optimized) from shading (graphics-pipeline features)

---

### 6.2 Hybrid Compute + Hardware RT

**Technique**: Use compute shader for primary rays, hardware RT for secondary rays (shadows, reflections)

**Pipeline**:
1. **Compute Pass**: Primary ray march, output first hit
2. **RT Pass**: Trace shadow rays from hit positions

**Advantage**: Minimize RT overhead (only used where beneficial)

---

### 6.3 Hybrid RTX Surface-Skin (ADVANCED) ⭐

**Source**: User innovation, VIXEN research extension
**Document**: `HybridRTX-SurfaceSkin-Architecture.md` (~110 pages)

**Concept**: Use RTX hardware for fast initial surface intersection, then switch to ray marching for complex materials.

#### Surface Skin Extraction

**Key Insight**: Only voxels at material boundaries affect light transport.

**Algorithm**:
```cpp
bool IsSurfaceVoxel(const VoxelGrid& grid, glm::ivec3 pos) {
    // 1. Has empty neighbors → air boundary
    // 2. Neighbors have different material IDs → transition
    // 3. At least one neighbor is non-opaque → light interaction
}

SurfaceSkinBuffer ExtractSurfaceSkin(const VoxelGrid& grid) {
    // Result: 20% of full grid (5× reduction for urban scene)
}
```

**Compression**: 134M voxels → **27M surface voxels** (urban 512³ example)

#### Virtual Geometry Generation

**Greedy Meshing**: Merge coplanar adjacent quads into rectangles

```cpp
std::vector<VirtualQuad> GreedyMesh(const SurfaceSkinBuffer& skin) {
    // Group by normal + material → Merge rectangles
    // Result: 10M triangles (vs 54M naïve)
}
```

**Triangle Conversion**: Standard RTX geometry (not AABBs)
- **Advantage**: Faster than custom intersection shader
- **Native hardware acceleration**: BVH traversal

#### Hybrid Pipeline

```glsl
// Step 1: RTX traces to surface skin (fast initial hit)
traceRayEXT(surfaceSkinTLAS, ...);

// Step 2: Material-specific continuation
if (mat.isOpaque) {
    finalColor = ShadeOpaque();  // Done
} else if (mat.isRefractive) {
    finalColor = MarchRefractiveVolume();  // Glass/water volume marching
} else if (mat.isVolumetric) {
    finalColor = MarchVolumetricMedia();   // Fog/smoke with scattering
}
```

**Material System**:
- **Opaque**: Simple diffuse shading (RTX hit = final)
- **Refractive**: Refract at boundary, march through volume, Beer's law absorption
- **Volumetric**: In-scattering + extinction (participating media)
- **Reflective**: Bounce secondary ray

#### Expected Performance

| Scene (Density) | Pure Ray March | Hybrid RTX | Speedup |
|-----------------|----------------|------------|---------|
| Cornell (10%)   | 16 ms          | **5 ms**   | **3.2×** |
| Cave (50%)      | 28 ms          | **12 ms**  | **2.3×** |
| Urban (90%)     | 45 ms          | **18 ms**  | **2.5×** |

**Key Advantages**:
- RTX skips empty space automatically (BVH)
- Surface skin reduces geometry by 5× (less BLAS overhead)
- Material flexibility beyond pure RT (glass, fog, etc.)
- ~10× faster initial intersection than DDA

**Implementation Complexity**: High (5-7 weeks for Phase N+1)

**Research Value**: Publication-worthy innovation combining RTX + flexible materials

---

## Category 7: Advanced Caching & Streaming

### 7.1 GigaVoxels Sparse Octree Streaming ⭐

**Source**: Crassin et al. (2009) - "GigaVoxels" paper (from bibliography)
**Document**: `GigaVoxels-CachingStrategy.md` (~90 pages)

**Problem**: Limited VRAM cannot hold multi-terabyte voxel datasets.

**Solution**: GPU-managed LRU cache with ray-guided on-demand streaming.

#### Brick Pool Architecture

**Concept**: 3D texture atlas acts as cache for voxel "bricks" (8³ dense voxel blocks)

```cpp
class BrickPool {
    VkImage brickAtlas_;  // E.g., 2048³ texture
    std::unordered_map<uint32_t, BrickSlot> slots_;  // LRU tracking

    uint32_t AllocateBrickSlot();  // Evict least recently used
    void UploadBrick(VkCommandBuffer cmd, const BrickData& brick, uint32_t slot);
};
```

**Cache Size**: 2 GB (vs 256 GB full dataset) → **128× memory reduction**

#### Ray-Guided Streaming

**Key Innovation**: Shaders request missing bricks during traversal

```glsl
// GPU shader
if (node.brickPointer == INVALID_BRICK) {
    RequestBrickLoad(nodeIndex);  // Atomic append to request buffer
    return SampleParentLOD();     // Use lower resolution temporarily
}

// Request buffer (GPU → CPU)
layout(set = 0, binding = 3) buffer RequestBuffer {
    uint counter;
    uint nodeIndices[4096];
};
```

**CPU Streaming Manager**:
```cpp
void ProcessRequests(VkCommandBuffer cmd) {
    // 1. Read request buffer from GPU
    auto requests = ReadRequestBuffer();

    // 2. Sort by priority (camera distance, LOD)
    std::sort(requests.begin(), requests.end(), ComparePriority);

    // 3. Load top N bricks (budget: 100 MB/frame @ 60 FPS = 6 GB/s)
    for (uint32_t nodeIndex : requests) {
        BrickData brick = LoadBrick(nodeIndex);  // From CPU cache or disk
        uint32_t slot = brickPool_.AllocateBrickSlot();
        brickPool_.UploadBrick(cmd, brick, slot);
    }
}
```

#### Multi-Resolution Mipmapping

**Graceful Degradation**: Always show *something*, progressively refine

```cpp
struct OctreeNode {
    uint32_t brickPointers[4];  // LOD 0 (full), LOD 1, LOD 2, LOD 3
};

// Shader fallback
vec4 SampleWithFallback(OctreeNode node, vec3 pos) {
    if (node.brickPointers[0] != INVALID) return SampleBrick(node.brickPointers[0], pos);
    if (node.brickPointers[1] != INVALID) return SampleBrick(node.brickPointers[1], pos);
    // ... continue fallback
}
```

**Visual Quality**: No visible "pop-in" (mipmaps ensure smooth transitions)

#### Scalability Metrics

| Grid Size | Full VRAM | GigaVoxels Cache | Reduction |
|-----------|-----------|------------------|-----------|
| 512³      | 512 MB    | 64 MB            | **8×**    |
| 1024³     | 4 GB      | 256 MB           | **16×**   |
| 2048³     | 32 GB     | 1 GB             | **32×**   |
| 4096³     | 256 GB    | 2 GB             | **128×**  |

**Performance**:
- **Cold start**: +20-30 ms (initial streaming)
- **Warm cache**: +1-2 ms overhead (request processing)
- **Steady state**: Cache hit rate > 95% (≈ baseline)

**Expected Speedup**: Neutral (same performance with massively larger datasets)

**Research Value**:
- Enables 4096³ grids (impossible with traditional approaches)
- Real-world applicability (streaming essential for production)
- Bandwidth optimization showcase

**Implementation Complexity**: High (4-6 weeks for Phase N+2)

---

## Category 8: Implementation Priority Matrix (Updated)

### Tier 1: Core Research (Phases G-N)

**Must Implement**:
1. Baseline DDA (compute/fragment)
2. Empty space skipping (hierarchical bitmap)
3. BlockWalk (tile-based cache)
4. Hardware RT (AABB voxels)
5. Octree baseline (hybrid pointer + brick map)

**Rationale**: Essential for answering core research question (180 configurations)

---

### Tier 2: Advanced Extensions (Phases N+1, N+2)

**High Value**:
1. **Hybrid RTX Surface-Skin** (Phase N+1)
   - Publication-worthy innovation
   - Material flexibility demonstration
   - 2-3× speedup potential

2. **GigaVoxels Streaming** (Phase N+2)
   - Scalability showcase (4096³ grids)
   - Industry-relevant technique
   - Bandwidth optimization analysis

**Rationale**: Extended journal publication, demonstrates production readiness

---

### Tier 3: Optional (Time Permitting)

1. Texture compression (ASTC, BC7)
2. Mipmapping/LOD (without streaming)
3. Additional hybrid variants

**Rationale**: Incremental improvements, lower research novelty

---

## Phase L Implementation Roadmap

Based on bibliography analysis, Phase L should focus on:

### Priority 1: Algorithm Variants (Core Research Question)
1. **Baseline DDA** (already done)
2. **Empty Space Skipping** (hierarchical bitmap)
3. **BlockWalk** (tile-based with cache)

**Rationale**: These represent distinct algorithmic approaches with measurable trade-offs.

---

### Priority 2: Hardware RT Variants
1. **BLAS granularity variants** (1, 2, 4, 8 voxels per AABB)
2. **Build flag variants** (fast-trace vs fast-build)

**Rationale**: Tuning parameters significantly affect hardware RT performance.

---

### Priority 3: Data Structure Variants
1. **Dense 3D texture** (baseline)
2. **Sparse octree + brick map** (from `OctreeDesign.md`)

**Rationale**: Fundamental architectural difference with memory/bandwidth impact.

---

### Priority 4: Optional (Time Permitting)
1. **Hybrid pipelines** (compute + fragment, compute + RT)
2. **Texture compression variants** (ASTC, BC7)
3. **Mipmapping/LOD**

**Rationale**: Interesting but lower priority for core research question.

---

## Expected Performance Matrix (Predictions)

| Scene (Density) | Baseline DDA | Empty Skip | BlockWalk | HW RT (1 AABB/voxel) | HW RT (4³ AABB) |
|-----------------|--------------|------------|-----------|----------------------|-----------------|
| Cornell (10%)   | 1.0×         | **1.5×**   | 1.1×      | 1.3×                 | **1.4×**        |
| Cave (50%)      | 1.0×         | 1.2×       | **1.3×**  | **1.4×**             | 1.3×            |
| Urban (90%)     | 1.0×         | 1.05×      | **1.35×** | 1.1×                 | **1.5×**        |

**Key Hypotheses**:
1. Empty space skipping dominates for sparse scenes
2. BlockWalk dominates for dense, coherent scenes
3. Hardware RT benefits from AABB granularity tuning
4. Dense scenes favor coarser AABBs (fewer false positives)

---

## Validation Metrics (Phase I + M)

For each optimization, measure:

1. **Frame Time** (ms) - Overall performance
2. **GPU Time** (ms) - Rendering workload
3. **Bandwidth** (GB/s) - Memory pressure
4. **Cache Hit Rate** (%) - Memory efficiency (if available via VK_KHR_performance_query)
5. **Traversal Steps** (count) - Algorithm efficiency (shader instrumentation)

**Statistical Analysis**: Run 300 frames per configuration, report min/max/mean/stddev/percentiles

---

## References

**Research Papers** (from bibliography):
- [1] Nousiainen - Baseline comparison
- [2] Aokana - Voxel rendering techniques
- [5] Voetter - Vulkan volumetric RT
- [6] Aleksandrov - Sparse voxel octrees
- [16] Derin - BlockWalk algorithm

**Implementation Documents**:
- `VoxelRayMarch-Integration-Guide.md` - Compute shader baseline
- `FragmentRayMarch-Integration-Guide.md` - Fragment shader variant
- `HardwareRTDesign.md` - Hardware RT pipeline
- `OctreeDesign.md` - Sparse data structure

**Profiling**:
- `PerformanceProfilerDesign.md` - Metrics collection system

---

## Next Steps (Phase L Implementation)

1. **Implement Empty Space Skipping** (compute + fragment variants)
2. **Implement BlockWalk** (compute shader only)
3. **Test BLAS granularity** (hardware RT variants)
4. **Integrate with profiler** (Phase I)
5. **Run full benchmark suite** (Phase M)
6. **Analyze results vs predictions** (validate hypotheses)

**Estimated Time**: 3-4 weeks (Phase L in roadmap)

**Deliverable**: Optimized pipelines + performance analysis document comparing all variants
