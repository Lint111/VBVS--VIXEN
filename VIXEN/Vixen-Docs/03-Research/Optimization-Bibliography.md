---
title: Optimization Techniques Bibliography
tags: [research, optimization, bibliography, algorithms]
created: 2025-11-02
status: reference
---

# Optimization Techniques from Research Bibliography

**Source**: 24 research papers on voxel rendering, ray tracing, GPU optimization

## Paper Index

| # | Author | Topic |
|---|--------|-------|
| 1 | Nousiainen | Performance comparison baseline |
| 2 | Aokana | Voxel rendering techniques |
| 5 | Voetter | Volumetric RT with Vulkan |
| 6 | Aleksandrov | Sparse voxel octrees |
| 16 | Derin | BlockWalk algorithm |

## Category 1: Traversal Optimizations

### 1.1 Empty Space Skipping

**Hierarchical Bitmask** [Paper 6]:
- Coarse grid (16³ blocks)
- 1 bit per block (empty/occupied)
- Skip entire blocks during traversal

```glsl
ivec3 blockPos = ivec3(rayOrigin) / 16;
if (IsBlockEmpty(blockPos)) {
    // Skip to next block boundary
    rayOrigin += rayDir * rayBoxIntersection();
}
```

**Expected Speedup**: +30-50% for sparse scenes

### 1.2 BlockWalk Algorithm [Paper 16]

**Concept**: Exploit ray coherence within screen tiles

1. Divide screen into 8×8 tiles
2. Pre-traverse blocks along tile center ray
3. Cache block occupancy in shared memory
4. All tile rays reuse cached data

```glsl
shared uint blockOccupancy[64];

if (localID == ivec2(0,0)) {
    PreTraverseBlocks(tileCenter, blockOccupancy);
}
barrier();
vec4 color = MarchWithCache(myRay, blockOccupancy);
```

**Expected Speedup**: +25-35% for dense scenes

### 1.3 Octree Empty Node Skipping

```glsl
if (node.childMask == 0) {
    continue;  // Skip empty subtree
}
```

**Expected Speedup**: +40-60% for sparse scenes

## Category 2: Data Structure Optimizations

### 2.1 Morton Code Indexing

Z-order curve for cache-friendly memory layout:

```cpp
uint64_t MortonEncode(uint32_t x, uint32_t y, uint32_t z) {
    return expandBits(x) | (expandBits(y) << 1) | (expandBits(z) << 2);
}
```

**Expected Speedup**: +5-10% (cache hit improvement)

### 2.2 SVO with Contour Data

Store distance to nearest surface in empty nodes:

```cpp
struct OctreeNode {
    uint8_t childMask;
    uint8_t minDistance;  // Skip distance
};
```

## Category 3: GPU Hardware Optimizations

### 3.1 Wavefront Coherence

**Problem**: Divergent rays cause warp stalls

**Solution**: Ray reordering by direction octant

### 3.2 Texture Cache Optimization

- Use `VK_IMAGE_TILING_OPTIMAL`
- Mipmapping for distant voxels
- Texture compression (ASTC, BC7)

### 3.3 Bandwidth Reduction

| Format | Size | Reduction |
|--------|------|-----------|
| RGBA8 | 32 bits | Baseline |
| RGB565 | 16 bits | 50% |
| ASTC 4×4×4 | 8 bpp | 75% |

## Category 4: Hardware RT Optimizations

### 4.1 BLAS Granularity

| Granularity | AABBs | Build Time | Trace Time |
|-------------|-------|------------|------------|
| 1 voxel/AABB | Millions | Slow | Fast |
| 4³ voxels/AABB | Thousands | Fast | Medium |
| 8³ voxels/AABB | Hundreds | Very Fast | Slow |

### 4.2 Build Flags

- `PREFER_FAST_TRACE`: Static scenes
- `PREFER_FAST_BUILD`: Dynamic scenes

## Category 5: Hybrid Pipelines

### 5.1 Compute + Fragment

1. Compute: March rays → hit positions
2. Fragment: Complex shading (GI, reflections)

### 5.2 Compute + Hardware RT

1. Compute: Primary rays
2. RT: Shadow/reflection rays

### 5.3 RTX Surface-Skin

See [[Hybrid-RTX-SurfaceSkin]] for detailed design.

## Performance Matrix (Predictions)

| Scene | Empty Skip | BlockWalk | HW RT (1) | HW RT (4³) |
|-------|------------|-----------|-----------|------------|
| Sparse (10%) | **1.5×** | 1.1× | 1.3× | 1.4× |
| Medium (50%) | 1.2× | **1.3×** | **1.4×** | 1.3× |
| Dense (90%) | 1.05× | **1.35×** | 1.1× | **1.5×** |

## Implementation Priority

### Tier 1: Core Research
1. Baseline DDA ✓
2. Empty space skipping
3. BlockWalk
4. Hardware RT variants
5. Octree baseline

### Tier 2: Advanced (Phase N+)
1. [[Hybrid-RTX-SurfaceSkin]]
2. [[GigaVoxels-Streaming]]

### Tier 3: Optional
1. Texture compression
2. Advanced mipmapping

## Related

- [[../02-Implementation/Ray-Marching]] - Baseline implementation
- [[Hardware-RT]] - RTX pipeline
- [[Pipeline-Comparison]] - Comparative study
