# GigaVoxels Sparse Octree Caching Strategy

**Research Paper**: "GigaVoxels: Ray-Guided Streaming for Efficient and Detailed Voxel Rendering" (Crassin et al., 2009)
**Purpose**: GPU bandwidth optimization via on-demand streaming and caching
**Integration**: Advanced Phase L optimization or Phase N+1 extension
**Date**: November 2, 2025

---

## Overview

**GigaVoxels** is a sparse voxel octree rendering technique that uses **GPU-managed caching** and **on-demand data streaming** to handle massive voxel datasets (billions of voxels) with limited GPU memory.

### Core Innovation

Traditional voxel rendering loads entire datasets into VRAM. GigaVoxels instead:

1. **Streams data on-demand**: Only loads visible voxels into GPU cache
2. **Ray-guided loading**: Uses ray tracing to determine which voxels to load
3. **GPU-managed cache**: Hardware texture cache handles eviction/replacement
4. **Multi-resolution hierarchy**: Octree with mipmapped bricks at each level

---

## Key Concepts

### 1. Sparse Voxel Octree (SVO) with Bricks

**Structure**: Octree nodes point to dense 3D texture "bricks" (typically 8³ or 16³ voxels)

```cpp
struct OctreeNode {
    uint32_t childPointer;   // Offset into node pool (0 = empty)
    uint32_t brickPointer;   // Offset into brick pool (GPU texture atlas)
    uint16_t metadata;       // Flags, LOD level, etc.
};

// Brick pool: Large 3D texture atlas containing all loaded bricks
// Example: 2048³ texture → 256 bricks of 8³ each
```

**Advantage**: Decouples octree navigation (small, pointer-based) from voxel storage (dense, cache-friendly).

---

### 2. GPU Cache Management

**Problem**: Limited VRAM (8-24 GB) cannot hold multi-terabyte voxel datasets.

**Solution**: Treat brick pool as **LRU cache**, stream bricks from CPU/disk as needed.

#### Cache Architecture

```
┌─────────────────────────────────────────────┐
│  GPU Cache (Brick Pool)                     │
│  ┌──────────┬──────────┬──────────┬─────┐  │
│  │ Brick 0  │ Brick 1  │ Brick 2  │ ... │  │
│  │ (8³)     │ (8³)     │ (8³)     │     │  │
│  └──────────┴──────────┴──────────┴─────┘  │
│                                             │
│  Allocation: Ring buffer or LRU eviction   │
└─────────────────────────────────────────────┘
        ▲                            │
        │ Upload (async)             │ Evict (when full)
        │                            ▼
┌─────────────────────────────────────────────┐
│  CPU Cache (Paged Memory)                   │
│  - Larger than GPU cache (100s of GB)       │
│  - Loads from disk on page fault            │
└─────────────────────────────────────────────┘
        ▲
        │ Load on miss
        │
┌─────────────────────────────────────────────┐
│  Disk Storage (Full Dataset)                │
│  - Multi-terabyte voxel data               │
│  - Compressed octree + bricks               │
└─────────────────────────────────────────────┘
```

---

### 3. Ray-Guided Streaming (Key Innovation)

**Idea**: During ray traversal, detect when ray enters unloaded region → Request load.

#### Algorithm

**Frame N (Render + Request)**:
1. Trace rays through octree
2. When ray hits node with `brickPointer == INVALID`:
   - Mark node as "requested" (write to request buffer)
   - Use lower-LOD parent brick as placeholder (mipmapping)
3. Continue rendering with placeholder data

**Frame N+1 (Upload)**:
1. Read request buffer on CPU
2. Load requested bricks from disk/CPU cache
3. Upload to GPU brick pool (async DMA transfer)
4. Update `brickPointer` to point to newly loaded brick

**Frame N+2 (Use)**:
1. Ray traversal now finds valid `brickPointer`
2. Full-resolution data available

#### Shader Pseudocode

```glsl
// Ray traversal with on-demand loading
vec4 traceRayGigaVoxels(vec3 rayOrigin, vec3 rayDir) {
    uint nodeIndex = 0;  // Root
    float t = 0.0;

    while (t < MAX_DISTANCE) {
        OctreeNode node = octreeBuffer.nodes[nodeIndex];

        if (node.brickPointer == INVALID_BRICK) {
            // Brick not loaded - request it
            RequestBrickLoad(nodeIndex);

            // Use parent LOD as placeholder (mipmapping)
            uint parentNode = GetParentNode(nodeIndex);
            vec4 color = SampleBrickLOD(octreeBuffer.nodes[parentNode].brickPointer, rayPos, LOD_LEVEL);
            return color;
        }

        // Brick loaded - sample at full resolution
        vec4 voxel = SampleBrick(node.brickPointer, rayPos);

        if (voxel.a > 0.5) {
            return ShadeVoxel(voxel, rayPos);
        }

        // Step to next voxel
        t += DDAStep(rayPos, rayDir);
    }

    return BACKGROUND_COLOR;
}

// Atomic append to request buffer
void RequestBrickLoad(uint nodeIndex) {
    uint slot = atomicAdd(requestCounter, 1);
    if (slot < MAX_REQUESTS) {
        requestBuffer.indices[slot] = nodeIndex;
    }
}
```

---

### 4. Multi-Resolution Mipmapping

**Problem**: When brick not loaded, need to show *something* (not black hole).

**Solution**: Each octree level stores pre-filtered (mipmapped) version of data.

```cpp
struct OctreeNode {
    // ...
    uint32_t brickPointers[4];  // LOD 0 (full res), LOD 1, LOD 2, LOD 3
};

// Sample with automatic LOD fallback
vec4 SampleWithFallback(OctreeNode node, vec3 pos) {
    if (node.brickPointers[0] != INVALID) return SampleBrick(node.brickPointers[0], pos);  // Full res
    if (node.brickPointers[1] != INVALID) return SampleBrick(node.brickPointers[1], pos);  // Half res
    if (node.brickPointers[2] != INVALID) return SampleBrick(node.brickPointers[2], pos);  // Quarter res
    return PLACEHOLDER_COLOR;  // Ultimate fallback
}
```

**Benefit**: Graceful degradation—always show *something*, progressively refine as data loads.

---

## Implementation for VIXEN

### Phase 1: Static GigaVoxels (No Streaming)

**Goal**: Prove brick pool architecture works, no on-demand loading yet.

```cpp
class GigaVoxelsRenderer {
public:
    void Initialize(VkDevice device, uint32_t brickPoolSize) {
        // Create brick pool (3D texture atlas)
        brickPool_ = CreateTexture3D(device, brickPoolSize, brickPoolSize, brickPoolSize,
            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        // Create node buffer (octree)
        nodeBuffer_ = CreateBuffer(device, MAX_NODES * sizeof(OctreeNode),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        // Upload initial dataset (all bricks)
        UploadAllBricks(sourceVoxelGrid);
    }

    void Render(VkCommandBuffer cmd, const Camera& camera) {
        // Bind octree + brick pool
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
            0, 1, &descriptorSet, 0, nullptr);

        // Dispatch ray marching shader
        vkCmdDispatch(cmd, screenWidth / 8, screenHeight / 8, 1);
    }

private:
    VkImage brickPool_;         // 3D texture atlas
    VkBuffer nodeBuffer_;       // Octree structure
};
```

**Shader** (Compute):
```glsl
#version 460

layout(set = 0, binding = 0, rgba8) uniform writeonly image2D outputImage;
layout(set = 0, binding = 1) buffer OctreeNodes { OctreeNode nodes[]; };
layout(set = 0, binding = 2) uniform sampler3D brickPool;

void main() {
    vec3 rayOrigin = camera.pos;
    vec3 rayDir = CalculateRayDir();

    // Traverse octree
    uint nodeIndex = 0;
    while (/* traversal condition */) {
        OctreeNode node = nodes[nodeIndex];

        if (node.brickPointer != INVALID_BRICK) {
            // Calculate UVW in brick pool texture
            vec3 brickUVW = GetBrickUVW(node.brickPointer, localVoxelPos);
            vec4 voxel = texture(brickPool, brickUVW);

            if (voxel.a > 0.5) {
                imageStore(outputImage, pixelCoords, vec4(ShadeVoxel(voxel), 1.0));
                return;
            }
        }

        nodeIndex = TraverseToNextNode(nodeIndex, rayDir);
    }

    imageStore(outputImage, pixelCoords, vec4(BACKGROUND_COLOR, 1.0));
}
```

---

### Phase 2: Dynamic Streaming (Full GigaVoxels)

**Goal**: On-demand brick loading based on visibility.

#### CPU-Side Streaming Manager

```cpp
class BrickStreamingManager {
public:
    void ProcessRequests(VkCommandBuffer cmd) {
        // Read request buffer from GPU
        std::vector<uint32_t> requestedNodes = ReadRequestBuffer();

        // Sort by priority (distance to camera, frame timestamp)
        std::sort(requestedNodes.begin(), requestedNodes.end(), [](uint32_t a, uint32_t b) {
            return GetPriority(a) > GetPriority(b);
        });

        // Load top N bricks (budget: e.g., 100 MB/frame @ 60 FPS = 6 GB/s)
        uint32_t loadedCount = 0;
        for (uint32_t nodeIndex : requestedNodes) {
            if (loadedCount >= MAX_LOADS_PER_FRAME) break;

            // Allocate slot in brick pool (evict LRU if full)
            uint32_t brickSlot = AllocateBrickSlot();

            // Load brick data (from CPU cache or disk)
            BrickData brick = LoadBrick(nodeIndex);

            // Upload to GPU
            UploadBrickToSlot(cmd, brick, brickSlot);

            // Update octree node pointer
            UpdateNodeBrickPointer(nodeIndex, brickSlot);

            ++loadedCount;
        }

        // Clear request buffer for next frame
        ClearRequestBuffer();
    }

private:
    struct BrickSlot {
        uint32_t nodeIndex;      // Which octree node uses this slot
        uint64_t lastUsedFrame;  // For LRU eviction
    };

    std::unordered_map<uint32_t, BrickSlot> brickSlots_;  // Slot index → metadata
    std::deque<uint32_t> lruQueue_;                       // LRU eviction queue
};
```

#### GPU Request Buffer

```cpp
// Descriptor set
layout(set = 0, binding = 3) buffer RequestBuffer {
    uint counter;              // Atomic counter
    uint nodeIndices[4096];    // Requested nodes (ring buffer)
};

// Shader: Request brick load
void RequestBrickLoad(uint nodeIndex) {
    uint slot = atomicAdd(requestBuffer.counter, 1);
    if (slot < 4096) {
        requestBuffer.nodeIndices[slot] = nodeIndex;
    }
}
```

---

## Bandwidth Optimization Analysis

### Without GigaVoxels (Baseline)

**Scenario**: 512³ voxel grid @ 4 bytes/voxel (RGBA8)
- **Memory**: 512³ × 4 = 512 MB
- **Transfer**: Must upload entire 512 MB at startup (or when scene changes)
- **Bandwidth**: 512 MB × 1 upload = **512 MB** (one-time)

**Problem**: Entire dataset resident in VRAM, limits scalability.

---

### With GigaVoxels (Streaming)

**Scenario**: 4096³ voxel grid (8× resolution) with 50% visibility

**Octree Structure**:
- **Brick size**: 8³ voxels
- **Bricks per level 0**: (4096 / 8)³ = 512³ = 134 million bricks
- **Visible bricks**: 50% × 134M = **67 million bricks**

**GPU Cache**:
- **Cache size**: 2 GB
- **Bricks in cache**: 2 GB / (8³ × 4 bytes) = **1 million bricks**
- **Coverage**: 1M / 67M = **1.5% of visible data**

**Streaming**:
- **Frame budget**: 100 MB/frame @ 60 FPS = 6 GB/s
- **Bricks per frame**: 100 MB / (8³ × 4) = **50,000 bricks**
- **Time to full load**: 67M / 50K = **1340 frames** ≈ 22 seconds

**Bandwidth Savings**:
- **Without GigaVoxels**: 4096³ × 4 = **256 GB** (impossible to fit in VRAM)
- **With GigaVoxels**: 2 GB cache + 6 GB/s streaming = **Feasible**

**Result**: **128× memory reduction** (256 GB → 2 GB), **infinite scalability**

---

## Integration with VIXEN Research

### Research Value

**GigaVoxels as 5th Pipeline Variant** (Phase N+1):
1. Compute shader (baseline)
2. Fragment shader
3. Hardware RT
4. Hybrid RT + compute
5. **GigaVoxels streaming** ← New variant

**Comparative Analysis**:
- **Bandwidth**: GigaVoxels vs baseline (measure streaming overhead)
- **Scalability**: Test with 1024³, 2048³, 4096³ grids (impossible without streaming)
- **Cache efficiency**: Hit rate, eviction frequency
- **Frame pacing**: Latency of brick loading (visual "pop-in")

### Test Configuration

| Parameter | Values | Purpose |
|-----------|--------|---------|
| **Grid Resolution** | 512³, 1024³, 2048³, 4096³ | Scalability stress test |
| **Cache Size** | 512 MB, 1 GB, 2 GB, 4 GB | Optimal cache size |
| **Load Budget** | 50 MB/f, 100 MB/f, 200 MB/f | Streaming bandwidth |
| **Scene Complexity** | Sparse, Medium, Dense | Cache miss patterns |

---

## Vulkan Implementation Details

### Brick Pool Upload (Async Transfer)

```cpp
void UploadBrick(VkCommandBuffer cmd, const BrickData& brick, uint32_t brickSlot) {
    // Calculate position in 3D texture atlas
    uint32_t bricksPerSide = BRICK_POOL_SIZE / BRICK_SIZE;  // e.g., 2048 / 8 = 256
    glm::uvec3 brickPos = {
        brickSlot % bricksPerSide,
        (brickSlot / bricksPerSide) % bricksPerSide,
        brickSlot / (bricksPerSide * bricksPerSide)
    };

    // Upload to staging buffer
    memcpy(stagingBufferMapped + uploadOffset, brick.data, BRICK_SIZE * BRICK_SIZE * BRICK_SIZE * 4);

    // Copy from staging to image
    VkBufferImageCopy region = {
        .bufferOffset = uploadOffset,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {brickPos.x * BRICK_SIZE, brickPos.y * BRICK_SIZE, brickPos.z * BRICK_SIZE},
        .imageExtent = {BRICK_SIZE, BRICK_SIZE, BRICK_SIZE}
    };

    vkCmdCopyBufferToImage(cmd, stagingBuffer, brickPoolImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}
```

### Request Buffer Readback

```cpp
std::vector<uint32_t> ReadRequestBuffer() {
    // Map GPU buffer (host-visible memory)
    uint32_t* data = (uint32_t*)MapBuffer(requestBuffer);

    uint32_t count = data[0];  // Atomic counter value
    std::vector<uint32_t> requests(data + 1, data + 1 + count);

    UnmapBuffer(requestBuffer);

    return requests;
}
```

---

## Performance Predictions

### Expected Results

| Scene (Resolution) | Baseline (Dense) | GigaVoxels (Streamed) | Bandwidth Reduction |
|--------------------|------------------|------------------------|---------------------|
| Cornell (512³)     | 512 MB VRAM      | 64 MB cache            | **8×** less memory  |
| Cave (1024³)       | 4 GB VRAM        | 256 MB cache           | **16×** less memory |
| Urban (2048³)      | **32 GB** (OOM)  | 1 GB cache             | **32×** less memory |
| Urban (4096³)      | **256 GB** (impossible) | 2 GB cache      | **128×** less memory |

**Frame Time Impact**:
- **Cold start** (empty cache): +20-30 ms (initial streaming)
- **Warm cache** (mostly loaded): +1-2 ms (request buffer processing)
- **Steady state**: ≈ baseline (cache hit rate > 95%)

---

## References

**Research Papers**:
- Crassin et al. (2009) - "GigaVoxels: Ray-Guided Streaming for Efficient and Detailed Voxel Rendering"
- Crassin & Green (2012) - "Octree-Based Sparse Voxelization Using the GPU Hardware Rasterizer"
- Kämpe et al. (2013) - "High Resolution Sparse Voxel DAGs"

**Implementation References**:
- NVIDIA GigaVoxels SDK (legacy, deprecated)
- Unity SEGI (Sparse Voxel GI) - uses similar techniques
- Unreal Engine 5 Nanite (geometry streaming, similar concept)

**VIXEN Documents**:
- `OctreeDesign.md` - Baseline octree structure (brick map hybrid)
- `HardwareRTDesign.md` - Hardware RT acceleration structures
- `BibliographyOptimizationTechniques.md` - Bandwidth reduction strategies

---

## Next Steps (Phase N+1 Implementation)

1. **Implement static brick pool** (Phase 2 baseline - no streaming)
2. **Add request buffer** (GPU → CPU communication)
3. **Implement streaming manager** (CPU-side loader)
4. **Add LRU cache eviction** (memory management)
5. **Integrate with profiler** (measure bandwidth, cache hit rate)
6. **Run comparative benchmarks** (GigaVoxels vs baseline)

**Estimated Time**: 4-6 weeks (advanced feature)

**Research Value**: Demonstrates scalability to multi-gigavoxel datasets, critical for real-world applications
