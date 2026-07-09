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
- [[../01-Architecture/Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07]] (2026-07-05) — implements the same core
  mechanism (brick-pointer sentinel → coarser-LOD fallback, async on-demand brick population), arrived
  at independently; see that Plan's "Prior art" note for what it reuses vs. deliberately diverges from
  (proactive per-tree distance/FOV gating instead of GigaVoxels' reactive per-ray request buffer; no
  disk/out-of-core tier — VIXEN's data already fits in host-visible memory)

## Deeper sources (2026-07-05): thesis + 2024 follow-on, not just the 2009 paper

This note above (and the summary sections below) reflect **only the 2009 I3D conference paper** — the
compressed early announcement. Two much richer sources exist and should be preferred for any future
implementation work:

1. **Crassin's PhD thesis** (2011, "GigaVoxels: A Voxel-Based Rendering Pipeline For Efficient
   Exploration Of Large And Detailed Scenes," 207pp). Chapter 7 "Out-of-core data management" (pp.
   117-155) is the load-bearing chapter — it describes a materially **simpler and more precise** request
   mechanism than the 2009 paper's HistoPyramid stream-compaction scheme: a flat request buffer sized
   1:1 with the page table, where every ray needing a page writes the *current frame's timestamp* into
   its slot — self-deduplicating by construction (identical concurrent writes need no atomic op), no
   per-ray output list at all. LRU eviction is maintained the same way via a parallel usage buffer,
   entirely on the GPU (§7.3.3-7.3.4) — no CPU-mirrored clone structure, unlike prior art of the time
   (Gobbetti et al.). **Quantified**: GPU kernel-fetch streaming vs. CPU-triggered copies reaches ~half
   theoretical PCIe bandwidth at scale, vs. 1/40th (small bricks) to 1/5th (large bricks) for CPU-driven
   transfer (§7.5.3); GPU-side LRU management is 1.7×-27.5× faster than CPU-side, advantage growing with
   pool size (§7.5.4).
2. **Richermoz & Neyret (2024), "GigaVoxels DP: Starvation-Less Render and Production for Large and
   Detailed Volumetric Worlds Walkthrough"** (HPG 2024, hal-04654692) — a direct successor that diagnoses
   remaining GPU-core starvation even in the GPU-native cache design (discrete render/production pass
   alternation still stalls in a "tail regime" that "can sometimes represent more than half of the total
   time"), fixed via CUDA Dynamic Parallelism (a ray hitting a miss launches its own production kernel
   directly, no CPU round-trip, no pass boundary). Measured **1.1×-4.4× speedup, average 2.1×**, biggest
   gains on the highest-churn/highest-disocclusion scenes. Relies on CUDA-specific dynamic parallelism
   with no direct Vulkan equivalent — the authors themselves flag Vulkan portability as unsolved future
   work.

Both PDFs were user-supplied 2026-07-05 (not previously in this repo) and read in full to ground
[[../01-Architecture/Sparse-Mip-ESVO-LOD-Inc1-Plan-2026-07]]'s "Prior art"/"Inc2 candidate" notes — see
that Plan for the concrete comparison against VIXEN's current design and what's actually worth adopting
(the flat request-buffer/GPU-LRU trick) vs. deferred (CUDA dynamic-parallelism scheduling, a genuine
Vulkan-portability problem).
