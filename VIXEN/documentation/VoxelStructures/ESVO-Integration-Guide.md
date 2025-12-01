# ESVO Integration Guide

**Date**: November 9, 2025
**Phase**: H.4 - Traversal Utilities
**Algorithm**: NVIDIA Efficient Sparse Voxel Octree (ESVO)

## Overview

This document describes NVIDIA's ESVO (Efficient Sparse Voxel Octree) algorithm as implemented in the VIXEN voxel ray marching system. ESVO is the primary and only traversal algorithm used.

## Algorithm Overview

### NVIDIA ESVO (Current Implementation)

**Strengths**:
- ✅ **Production-proven** (NVIDIA Optix, Brigade Engine)
- ✅ **Implicit stack** via scale parameter (no dynamic indexing)
- ✅ **Built-in LOD** via `ray_size_coef` parameter
- ✅ **Parametric arithmetic** for exact voxel boundaries (no epsilon errors)
- ✅ **3-5× faster** than academic traversal algorithms
- ✅ **XOR-based mirroring** eliminates directional branching

**Weaknesses**:
- ⚠️ Requires ESVO node format (8 bytes vs current 40 bytes)
- ⚠️ More complex to understand (floating-point bit tricks)
- ⚠️ Assumes octree in [1,2] coordinate space

**Location**: [OctreeTraversal-ESVO.glsl](../Shaders/OctreeTraversal-ESVO.glsl)

## Key Innovations

### 1. Implicit Stack via Scale Parameter

**Current (Revelles)**:
```glsl
struct TraversalState {
    uint nodeIndex;
    float tx0, ty0, tz0, tx1, ty1, tz1;
    vec3 nodeMin, nodeMax;
    uint currentOctant;
};

TraversalState stack[32];  // 32 × 48 bytes = 1536 bytes local memory
int stackPtr = 0;
```

**ESVO**:
```glsl
struct StackEntry {
    uint parentPtr;  // 4 bytes
    float tMax;      // 4 bytes
};

StackEntry stack[23];  // 23 × 8 bytes = 184 bytes (8.4× reduction)
int scale = 23;  // Implicit depth via scale parameter
```

**Benefit**: **8.4× less local memory**, no `stackPtr` dynamic indexing.

### 2. Parametric Plane Intersection

**Concept**: Represent voxel boundaries as parametric planes `tx(x) = (x - px) / dx`.

**Transformation**:
```glsl
// Original: tx(x) = (x - px) / dx
// ESVO form: tx(x) = x * tx_coef - tx_bias

float tx_coef = 1.0 / -abs(d.x);
float tx_bias = tx_coef * p.x;

// Evaluate at voxel corner (x = pos.x)
float tx_corner = pos.x * tx_coef - tx_bias;
```

**Benefit**: Single multiply-add per axis (vs division per ray step).

### 3. XOR-Based Octant Mirroring

**Problem**: Ray direction affects which octant is entered first.

**Current approach**: Branching per axis.
```glsl
if (rayDir.x > 0) {
    octant.x = 1;
} else {
    octant.x = 0;
}
```

**ESVO approach**: XOR mask unifies all cases.
```glsl
int octant_mask = 7;  // Binary: 111
if (d.x > 0.0) octant_mask ^= 1;  // Flip X bit
if (d.y > 0.0) octant_mask ^= 2;  // Flip Y bit
if (d.z > 0.0) octant_mask ^= 4;  // Flip Z bit

// Later: permute child index
int child_shift = idx ^ octant_mask;
```

**Benefit**: Zero branching in traversal loop (GPU-friendly).

### 4. LOD Termination

**Concept**: Stop traversal when voxel projects to ≤1 pixel.

**Implementation**:
```glsl
// Voxel size at current scale
float voxelSize = scale_exp2;  // 2^(scale - 23)

// Projected size (distance from camera affects LOD)
float projectedSize = voxelSize / distance(cameraPos, rayPos);

// Terminate if voxel small enough
if (tc_max * raySizeCoef + raySizeBias >= scale_exp2) {
    break;  // Hit!
}
```

**Parameters**:
- `raySizeCoef`: Base LOD threshold (larger = coarser LOD, faster)
- `raySizeBias`: Increase LOD along ray (compensate for perspective)

**Benefit**: Automatic LOD without mipmap management.

## Data Structure

### ESVO Node Format (8 bytes) - ChildDescriptor

```cpp
struct ESVONode {
    uint32_t descriptor0;  // Child masks + pointer
    uint32_t descriptor1;  // Brick/contour data (optional)
};

// descriptor0 layout (32 bits):
// bits 0-14:  Combined child_mask (8 bits) + non_leaf_mask (7 bits)
//             Shifted by child index during traversal
// bit  16:    far_bit (0 = near pointer, 1 = far pointer)
// bits 17-31: child_offset (15 bits = 32K children per node)

// descriptor1 layout (32 bits):
// bits 0-6:   contour_mask (optional surface detail)
// bits 8-31:  brick_offset OR contour_offset
```

**Benefits**:
- **8 bytes per node** (compact memory)
- **Far pointers** enable unlimited octree depth
- **Leaf mask** distinguishes internal nodes from voxel data

### Implementation Details

The ChildDescriptor format is defined in `SVOTypes.glsl` and `libraries/SVO/include/SVO/SVOTypes.h`:

```cpp
// C++ side (SVOTypes.h)
struct ChildDescriptor {
    uint32_t validMask : 8;     // Which children exist (0-7)
    uint32_t leafMask : 8;      // Which children are leaves
    uint32_t childPointer : 15; // Base pointer to children
    uint32_t farBit : 1;        // Use far pointer table
    uint32_t brickIndex;        // For leaves: index into brick buffer
};
```

```glsl
// GLSL side (SVOTypes.glsl)
uint getValidMask(uvec2 desc) { return desc.x & 0xFFu; }
uint getLeafMask(uvec2 desc) { return (desc.x >> 8) & 0xFFu; }
uint getChildPointer(uvec2 desc) { return (desc.x >> 16) & 0x7FFFu; }
bool getFarBit(uvec2 desc) { return (desc.x & 0x80000000u) != 0u; }
uint getBrickIndex(uvec2 desc) { return desc.y; }
```

## Current Integration

### Shader Structure

The ESVO traversal is implemented directly in `VoxelRayMarch.comp`:

1. **SVOTypes.glsl** - Shared type definitions for ChildDescriptor format
2. **VoxelRayMarch.comp** - Main compute shader with ESVO traversal

### Key Functions

```glsl
// Ray coefficient initialization (handles octant mirroring)
RayCoefficients initRayCoefficients(vec3 rayDir, vec3 rayStartWorld);

// Main traversal function (ESVO algorithm)
bool traverseOctree(RayCoefficients coef, inout DebugRaySample debug,
                    out vec3 hitColor, out vec3 hitNormal, out float hitT);

// Brick DDA for sub-voxel precision after ESVO hits a leaf
bool handleLeafHit(TraversalState state, RayCoefficients coef,
                   inout DebugRaySample debug,
                   out vec3 hitColor, out vec3 hitNormal, out float hitT);
```

### Coordinate Space

ESVO operates in normalized [1,2]^3 space:
- World coordinates transformed via `octreeConfig.worldToLocal`
- Local [0,1] offset to ESVO [1,2] by adding 1.0
- Octant mirroring via XOR mask for ray direction independence

## Performance

### Measured Results (Week 2 Benchmark)

| Scene | Resolution | Performance |
|-------|------------|-------------|
| Cornell Box | 128^3 | **1,700 Mrays/sec** |
| Procedural Grid | 128^3 | ~1,500 Mrays/sec |

**Key factors**:
1. **Empty-space skipping**: ESVO advances by full voxel size when empty
2. **Implicit stack**: 23 levels x 8 bytes = 184 bytes/thread (low register pressure)
3. **LOD termination**: Stops at appropriate detail level

### Memory Efficiency

- **8 bytes per node** (ChildDescriptor format)
- **Single fetch per descent** (only accessed nodes read)
- **Brick data** stored separately for leaf voxel detail

### Debug Visualization

The shader supports multiple debug modes via `pc.debugMode` push constant:
- Mode 1: Octant mask (XYZ = RGB)
- Mode 2: Traversal depth/scale
- Mode 3: Iteration count (heat map)
- Mode 4: T-span visualization
- Mode 5: Hit normals
- Mode 6: World position
- Mode 7: Brick boundaries
- Mode 8: Material IDs

## Future Extensions

### 1. Contour Masking (Surface Detail)

ESVO supports **contour masks** for high-quality surface representation:

```glsl
// descriptor1 stores surface normal + thickness
int contour_mask = child_descriptor.y << child_shift;
if ((contour_mask & 0x80) != 0) {
    // Intersect ray with oriented plane
    float cthick = extractThickness(descriptor1);
    vec3 cnormal = extractNormal(descriptor1);

    float t_surface = IntersectPlane(rayOrigin, rayDir, cnormal, cthick);
    t_min = max(t_min, t_surface);
}
```

**Benefit**: Smooth surfaces without dense voxelization (Phase N+1)

### 2. Beam Optimization (Frustum Culling)

ESVO can trace **beam** instead of single ray:

```glsl
// Trace 2×2 beam (4 rays in parallel)
vec3 beamOrigin = pixelCenter;
vec3 beamDir[4] = {TL, TR, BL, BR};  // Four corners

// Find common ancestor node
uint sharedNode = FindCommonAncestor(beamDir[0], beamDir[1], beamDir[2], beamDir[3]);

// Descend once for all 4 rays
HitResult hits[4] = castBeamESVO(beamOrigin, beamDir, sharedNode);
```

**Benefit**: 2-3× speedup via coherence (Phase N+2)

### 3. Dynamic LOD via Temporal Coherence

**Idea**: Increase LOD during motion, decrease when static.

```glsl
uniform float cameraVelocity;  // From VulkanGraphApplication

float adaptiveLOD = mix(0.5, 2.0, cameraVelocity / 10.0);
HitResult hit = castRayESVO(..., adaptiveLOD, 0.0);
```

**Benefit**: 60 FPS during camera movement, high quality when still

## References

### Papers

- **[15] Laine & Karras (2010)**: "Efficient Sparse Voxel Octrees" - Original ESVO paper
- **[16] Derin et al. (2024)**: "BlockWalk" - Empty-space skipping benchmarks
- **[2] Fang et al. (2020)**: "SVDAG Streaming" - 6ms frame time, 2-4× speedup
- **[6] Aleksandrov et al. (2019)**: "SVO baseline" - Simple octree for comparison

### Code

- **NVIDIA Optix**: Production ray tracer using ESVO (closed-source, but described in papers)
- **Brigade Engine**: Real-time path tracer (Otoy) - uses modified ESVO
- **Appendix A (CUDA source)**: Reference implementation from user prompt

### Codebase

- **Current traversal**: [VoxelRayMarch.comp:314-436](../../Shaders/VoxelRayMarch.comp#L314-L436)
- **ESVO implementation**: [OctreeTraversal-ESVO.glsl](../../Shaders/OctreeTraversal-ESVO.glsl)
- **Octree builder**: [VoxelOctree.cpp](../../RenderGraph/src/Data/VoxelOctree.cpp)
- **Integration guide**: This document

## Status

ESVO is fully integrated and operational:

- **VoxelRayMarch.comp**: ESVO traversal with brick DDA
- **SVOTypes.glsl**: Shared ChildDescriptor format
- **VoxelGridNode**: GPU buffer management and dispatch
- **GPUPerformanceLogger**: Timing and Mrays/sec metrics

See [VoxelRayMarch-Integration-Guide.md](../Shaders/VoxelRayMarch-Integration-Guide.md) for descriptor binding details.
