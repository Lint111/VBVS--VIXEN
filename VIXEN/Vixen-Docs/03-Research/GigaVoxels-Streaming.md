---
title: GigaVoxels Streaming Architecture
tags: [research, streaming, caching, optimization, phase-n]
created: 2025-11-02
status: research
---

# GigaVoxels Sparse Octree Streaming

**Source**: Crassin et al. (2009) - "GigaVoxels: Ray-Guided Streaming for Efficient and Detailed Voxel Rendering"

## Core Innovation

Handle massive voxel datasets (billions of voxels) with limited GPU memory via:
1. **On-demand streaming**: Only load visible voxels
2. **Ray-guided loading**: Rays determine what to load
3. **GPU-managed cache**: LRU eviction in brick pool
4. **Multi-resolution hierarchy**: Mipmapped bricks

## Architecture

```
GPU Brick Pool (2 GB cache)
    ↑ Upload (async)     ↓ Evict (LRU)
CPU Cache (100s GB)
    ↑ Load on miss
Disk Storage (Terabytes)
```

## Brick Pool Design

Octree nodes point to dense 3D texture "bricks" (8³ voxels each):

```cpp
struct OctreeNode {
    uint32_t childPointer;   // Octree navigation
    uint32_t brickPointer;   // Index into brick pool
    uint16_t metadata;       // LOD level, flags
};
```

**Brick Pool**: Large 3D texture atlas (e.g., 2048³)

## Ray-Guided Streaming

**Frame N (Render + Request)**:
1. Trace rays through octree
2. Hit unloaded node → Mark as "requested"
3. Use parent LOD as placeholder

**Frame N+1 (Upload)**:
1. CPU reads request buffer
2. Loads requested bricks from disk
3. Uploads to GPU (async DMA)

**Frame N+2 (Use)**:
Full-resolution data available

### Shader Code

```glsl
if (node.brickPointer == INVALID_BRICK) {
    RequestBrickLoad(nodeIndex);  // Atomic append
    return SampleParentLOD();     // Placeholder
}
```

## Bandwidth Analysis

**Without GigaVoxels (512³)**:
- Memory: 512 MB resident

**With GigaVoxels (4096³)**:
- Full dataset: 256 GB (impossible)
- Cache: 2 GB
- Streaming: 6 GB/s @ 60 FPS
- **128× memory reduction**

## Scalability

| Grid Size | Full VRAM | Cache | Reduction |
|-----------|-----------|-------|-----------|
| 512³ | 512 MB | 64 MB | 8× |
| 1024³ | 4 GB | 256 MB | 16× |
| 2048³ | 32 GB | 1 GB | 32× |
| 4096³ | 256 GB | 2 GB | **128×** |

## Performance Impact

- **Cold start**: +20-30 ms (initial streaming)
- **Warm cache**: +1-2 ms (request processing)
- **Steady state**: Cache hit rate >95%

## Multi-Resolution Fallback

```cpp
struct OctreeNode {
    uint32_t brickPointers[4];  // LOD 0-3
};

// Graceful degradation
vec4 SampleWithFallback(OctreeNode node, vec3 pos) {
    for (int lod = 0; lod < 4; lod++) {
        if (node.brickPointers[lod] != INVALID)
            return SampleBrick(node.brickPointers[lod], pos);
    }
    return PLACEHOLDER;
}
```

## Implementation Plan

1. Static brick pool (no streaming)
2. Add request buffer (GPU → CPU)
3. Implement streaming manager
4. LRU cache eviction
5. Integrate with profiler
6. Comparative benchmarks

**Estimated Time**: 4-6 weeks (Phase N+2)

## Research Value

- Enables 4096³ grids (impossible otherwise)
- Industry-relevant technique (similar to Nanite)
- Bandwidth optimization showcase

## Related

- [[../02-Implementation/SVO-System]] - Base octree design
- [[Optimization-Bibliography]] - Supporting research
- [[../05-Progress/Roadmap]] - Phase planning
