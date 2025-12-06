---
title: Hybrid RTX Surface-Skin Architecture
tags: [research, ray-tracing, hardware-rt, optimization, phase-n]
created: 2025-11-02
status: research
---

# Hybrid RTX Surface-Skin Architecture

**Concept**: Use RTX hardware for initial surface intersection, then switch to ray marching for material-specific traversal.

## Core Innovation

**Surface Skin Buffer**: Sparse representation of world boundaries and material transitions - only ~20% of voxels are "surface" voxels.

### Problem with Pure Approaches

| Approach | Pros | Cons |
|----------|------|------|
| Pure Ray Marching | Flexible materials | Slow initial intersection |
| Pure Hardware RT | Fast BVH traversal | Limited to opaque geometry |

### Hybrid Solution

```
1. Extract Surface Skin (CPU) → 5× reduction
2. Generate Virtual Geometry (quads → triangles)
3. Build RTX BLAS (triangle geometry, not AABBs)
4. RTX Trace → Find first surface hit (fast)
5. Material-Specific Continuation:
   - Opaque → Done, shade
   - Refractive → March through volume (glass)
   - Volumetric → March with scattering (fog)
   - Reflective → Bounce ray
```

## Surface Voxel Detection

A voxel is in the "surface skin" if:
1. Has empty neighbors (air boundary)
2. Neighbors have different material IDs
3. At least one neighbor is non-opaque

```cpp
bool IsSurfaceVoxel(const VoxelGrid& grid, glm::ivec3 pos) {
    VoxelMaterial center = grid.GetMaterial(pos);
    if (center.id == 0) return false;  // Air is never surface
    
    for (const auto& nPos : Get6Neighbors(pos)) {
        VoxelMaterial neighbor = grid.GetMaterial(nPos);
        if (neighbor.id == 0) return true;  // Air boundary
        if (neighbor.id != center.id && (!center.isOpaque || !neighbor.isOpaque))
            return true;  // Material transition
    }
    return false;
}
```

## Compression Ratio

**Urban scene (512³, 90% density)**:
- Full grid: 134 million voxels
- Surface skin: **27 million voxels** (5× reduction)

## Greedy Meshing

Merge adjacent coplanar quads → fewer triangles:
- 27M voxels → 10M triangles (vs 54M naïve)
- **10× reduction** in geometry

## Material System

```cpp
struct Material {
    glm::vec3 color;
    uint8_t id;
    bool isOpaque, isReflective, isRefractive, isVolumetric;
    float indexOfRefraction;  // Glass: 1.5, Diamond: 2.4
    float absorptionCoeff;    // Beer's law
    float scatterCoeff;       // Volumetric scattering
};
```

## Expected Performance

| Scene | Pure March | Hybrid RTX | Speedup |
|-------|------------|------------|---------|
| Cornell (10%) | 16 ms | 5 ms | **3.2×** |
| Cave (50%) | 28 ms | 12 ms | **2.3×** |
| Urban (90%) | 45 ms | 18 ms | **2.5×** |

## Implementation Roadmap

1. Surface skin extraction (CPU)
2. Greedy meshing optimization
3. Triangle BLAS building
4. Hybrid ray tracing shaders
5. Material-specific continuation (glass, fog, mirrors)
6. Benchmarking vs baselines

**Estimated Time**: 5-7 weeks (Phase N+1)

## Research Value

- **Innovation**: First known voxel renderer combining RTX + flexible material marching
- **4th/5th Pipeline Variant** in comparative study
- Demonstrates hybrid approach benefits

## Related

- [[Hardware-RT]] - Pure RTX approach
- [[../02-Implementation/Ray-Marching]] - Pure marching baseline
- [[Pipeline-Comparison]] - Comparative analysis
- [[Optimization-Bibliography]] - Supporting research
