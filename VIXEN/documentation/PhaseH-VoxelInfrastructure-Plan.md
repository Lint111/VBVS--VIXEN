# Phase H: Voxel Data Infrastructure

**Created**: November 8, 2025
**Status**: Planning → Implementation
**Priority**: HIGH - Critical path for research execution
**Estimated Duration**: 3-4 weeks

---

## Overview

Phase H implements the voxel data infrastructure required for comparative ray tracing research. This phase builds the foundation for Phases I-N by creating voxel data structures, procedural scene generation, and GPU upload utilities.

---

## Prerequisites ✅

All prerequisites met:
- ✅ Phase G (Compute Pipeline) - COMPLETE
- ✅ Infrastructure Systems (Testing, Logging, Variadic, Context, Hooks) - COMPLETE
- ✅ ComputeDispatchNode + ComputePipelineNode - Working
- ✅ VoxelRayMarch.comp - Baseline shader implemented
- ✅ CameraNode - Camera transformations ready
- ✅ Research preparation (OctreeDesign.md, TestScenes.md) - Design complete

---

## Goals

### Primary Goals
1. **Octree Data Structure**: Implement sparse voxel octree (SVO) for efficient voxel storage
2. **Procedural Generation**: Generate 3 test scenes (Cornell Box, Cave, Urban Grid)
3. **GPU Upload**: Transfer voxel data to GPU buffers with proper descriptor binding
4. **Traversal Utilities**: Helper functions for DDA traversal and empty space skipping
5. **VoxelGridNode Integration**: Complete VoxelGridNode implementation (currently 60% done)

### Success Criteria
- ✅ 256³ octree loads in <100ms
- ✅ Procedural scenes match target densities (±5%)
- ✅ GPU upload working with descriptor sets
- ✅ Traversal utilities validated with unit tests
- ✅ VoxelRayMarch.comp renders procedural scenes correctly

---

## Architecture

### Octree Data Structure

Based on [OctreeDesign.md](VoxelStructures/OctreeDesign.md):

**Hybrid Octree Design**:
- **Coarse Level**: Pointer-based octree nodes (36 bytes each)
- **Fine Level**: 8³ brick maps (512 bytes each)
- **Compression**: 9:1 ratio for sparse scenes (10% density)
- **Indexing**: Morton code for cache locality

**Node Structure**:
```cpp
struct OctreeNode {
    uint32_t childMask;        // 8 bits for child presence
    uint32_t brickOffset;      // Offset to brick data (if leaf)
    glm::vec3 center;          // Node center position
    float halfSize;            // Half-width of node bounds
    uint32_t reserved[4];      // Padding to 36 bytes
};

struct VoxelBrick {
    uint8_t voxels[8][8][8];   // 512 voxel values (8³)
};
```

**Memory Layout**:
```
[Octree Nodes] → [Voxel Bricks] → [Material Data]
     ↓                ↓                  ↓
  Hierarchy      Fine detail        Properties
```

---

### Test Scenes

Based on [TestScenes.md](Testing/TestScenes.md):

#### Scene 1: Cornell Box (10% density - sparse)
- **Size**: 64³ voxels
- **Features**: 6 walls, 1 light, 2 boxes
- **Density**: ~10% filled (sparse octree optimization)
- **Purpose**: Baseline performance, shadow testing

#### Scene 2: Cave System (50% density - medium)
- **Size**: 128³ voxels
- **Features**: Perlin noise-based tunnels and chambers
- **Density**: ~50% filled (moderate octree depth)
- **Purpose**: Medium complexity traversal

#### Scene 3: Urban Grid (90% density - dense)
- **Size**: 256³ voxels
- **Features**: Procedural buildings, streets, details
- **Density**: ~90% filled (stress test)
- **Purpose**: Worst-case performance evaluation

---

## Implementation Plan

### H.1: Octree Data Structure (1 week)

**Tasks**:
1. Implement `OctreeNode` and `VoxelBrick` structures
2. Implement octree construction from voxel data
3. Add Morton code indexing for cache optimization
4. Implement octree serialization/deserialization
5. Write unit tests for octree operations

**Files**:
- `RenderGraph/include/Data/VoxelOctree.h` (NEW)
- `RenderGraph/src/Data/VoxelOctree.cpp` (NEW)
- `RenderGraph/tests/test_voxel_octree.cpp` (NEW)

**Deliverables**:
- Octree construction from dense voxel grid
- Octree traversal utilities
- Memory-efficient storage (9:1 compression for sparse)
- Unit tests with 80%+ coverage

---

### H.2: Procedural Scene Generation (1 week)

**Tasks**:
1. Implement Cornell Box generator
2. Implement Perlin noise-based cave generator
3. Implement procedural urban grid generator
4. Add material assignment per voxel
5. Validate density targets (±5%)

**Files**:
- `RenderGraph/include/Data/SceneGenerator.h` (NEW)
- `RenderGraph/src/Data/SceneGenerator.cpp` (NEW)
- `RenderGraph/tests/test_scene_generator.cpp` (NEW)

**Procedural Algorithms**:

**Cornell Box**:
```cpp
void GenerateCornellBox(VoxelGrid& grid, uint32_t size) {
    // 1. Fill boundary walls
    // 2. Carve interior space
    // 3. Add colored walls (red left, green right, white others)
    // 4. Add light source (top center)
    // 5. Add two boxes (rotated cubes)
}
```

**Cave System (Perlin Noise)**:
```cpp
void GenerateCaveSystem(VoxelGrid& grid, uint32_t size, float threshold) {
    // 1. Generate 3D Perlin noise field
    // 2. Threshold to create solid/empty space
    // 3. Apply cellular automata smoothing
    // 4. Ensure connectivity between chambers
    // 5. Add stalactites/stalagmites details
}
```

**Urban Grid**:
```cpp
void GenerateUrbanGrid(VoxelGrid& grid, uint32_t size) {
    // 1. Generate street grid layout
    // 2. Place buildings with varying heights
    // 3. Add windows, doors, architectural details
    // 4. Add sidewalks and street features
    // 5. Ensure 90% density target
}
```

**Deliverables**:
- 3 scene generators with target densities
- Material assignment (albedo, roughness, metallic)
- Density validation tests
- Performance: <1 second generation for 256³

---

### H.3: GPU Upload & Integration (3-5 days)

**Tasks**:
1. Complete VoxelGridNode implementation (currently 60% done)
2. Implement octree linearization for GPU consumption
3. Create VkBuffer and upload voxel data
4. Bind octree data to compute shader descriptors
5. Update VoxelRayMarch.comp to read octree data

**Files**:
- `RenderGraph/src/Nodes/VoxelGridNode.cpp` (UPDATE - currently 60% done)
- `RenderGraph/include/Data/Nodes/VoxelGridNodeConfig.h` (UPDATE)
- `Shaders/VoxelRayMarch.comp` (UPDATE)

**GPU Buffer Layout**:
```glsl
// GLSL descriptor set 0, binding 1
layout(std140, binding = 1) readonly buffer OctreeNodes {
    OctreeNode nodes[];
};

// Descriptor set 0, binding 2
layout(std140, binding = 2) readonly buffer VoxelBricks {
    VoxelBrick bricks[];
};

// Descriptor set 0, binding 3
layout(std140, binding = 3) readonly buffer MaterialData {
    MaterialInfo materials[];
};
```

**Deliverables**:
- VoxelGridNode fully implemented (100%)
- Octree data uploaded to GPU
- Descriptor binding working
- Visual confirmation: Cornell Box renders correctly

---

### H.4: Traversal Utilities (3-5 days)

**Tasks**:
1. Implement ray-AABB intersection (CPU side)
2. Implement DDA voxel traversal helpers
3. Add empty space skipping (octree optimization)
4. Write GLSL traversal functions for shader
5. Unit test all traversal algorithms

**Files**:
- `RenderGraph/include/Data/VoxelTraversal.h` (NEW)
- `RenderGraph/src/Data/VoxelTraversal.cpp` (NEW)
- `Shaders/VoxelTraversal.glsl` (NEW - include file)
- `RenderGraph/tests/test_voxel_traversal.cpp` (NEW)

**GLSL Traversal Helper**:
```glsl
// VoxelTraversal.glsl
struct RayHit {
    bool hit;
    vec3 position;
    vec3 normal;
    uint materialID;
    float distance;
};

RayHit TraverseOctree(Ray ray, OctreeNodes nodes, VoxelBricks bricks) {
    // 1. Ray-AABB test against root node
    // 2. DDA traversal through octree hierarchy
    // 3. Skip empty nodes (childMask == 0)
    // 4. Test voxel bricks for fine detail
    // 5. Return hit info or miss
}
```

**Deliverables**:
- Ray-AABB intersection (tested)
- DDA traversal implementation
- Empty space skipping working
- GLSL helper functions
- Unit tests with 80%+ coverage

---

## Testing Strategy

### Unit Tests (Required)
1. **test_voxel_octree.cpp**: Octree construction, traversal, compression
2. **test_scene_generator.cpp**: Density validation, material assignment
3. **test_voxel_traversal.cpp**: Ray-AABB, DDA, empty skip algorithms

### Integration Tests (Required)
1. **VoxelGridNode**: GPU upload, descriptor binding, buffer creation
2. **End-to-end**: Generate Cornell Box → Upload → Render → Verify output

### Visual Tests (Manual)
1. Cornell Box renders with correct colors and shadows
2. Cave system shows organic tunnel structure
3. Urban grid renders with building details

---

## Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| **Octree Construction** | <100ms for 256³ | Profiling timer |
| **Scene Generation** | <1s for 256³ | Profiling timer |
| **GPU Upload** | <50ms for 256³ | Vulkan timestamp queries |
| **Render Frame** | <16ms (60 FPS) | Frame time measurement |
| **Memory Usage** | <512MB for 256³ | VRAM tracking |

---

## Risks & Mitigations

### Risk 1: Octree Complexity Exceeds Estimate
**Probability**: Medium
**Impact**: High

**Mitigation**:
- Use design from OctreeDesign.md (already validated)
- Limit to hybrid approach (no ECS optimization)
- Skip advanced features (LOD, streaming) for Phase H
- If needed, use dense grid as fallback (simpler, more memory)

### Risk 2: 256³ Exceeds VRAM Budget
**Probability**: Medium
**Impact**: Medium

**Mitigation**:
- Octree compression provides 9:1 ratio for sparse scenes
- If still too large, reduce max resolution to 128³
- Monitor VRAM usage during development
- Add memory budget tracking if needed

### Risk 3: Procedural Generation Too Slow
**Probability**: Low
**Impact**: Low

**Mitigation**:
- Cache generated scenes to disk
- Load pre-generated scenes instead of runtime generation
- Optimize critical paths with profiling

---

## Dependencies

### Inputs (Already Complete)
- ComputeDispatchNode ✅
- ComputePipelineNode ✅
- CameraNode ✅
- VoxelRayMarch.comp (baseline) ✅

### Outputs (For Future Phases)
- Phase I (Performance Profiling): Uses voxel scenes for benchmarking
- Phase J (Fragment Shader): Uses same voxel data
- Phase K (Hardware RT): Converts octree to BLAS
- Phase L (Optimizations): Applies algorithms to voxel traversal

---

## Timeline

### Week 1 (Nov 8-15): Octree Data Structure
- Days 1-2: Implement OctreeNode and VoxelBrick
- Days 3-4: Implement octree construction
- Day 5: Morton code indexing
- Days 6-7: Unit tests and optimization

### Week 2 (Nov 15-22): Procedural Scene Generation
- Days 1-2: Cornell Box generator
- Days 3-4: Perlin noise cave generator
- Days 5-6: Urban grid generator
- Day 7: Density validation and material assignment

### Week 3 (Nov 22-29): GPU Upload & Integration
- Days 1-2: Complete VoxelGridNode (40% remaining)
- Day 3: Octree linearization for GPU
- Days 4-5: VkBuffer creation and upload
- Days 6-7: Descriptor binding and shader integration

### Week 4 (Nov 29-Dec 6): Traversal Utilities & Polish
- Days 1-2: Ray-AABB and DDA traversal (CPU)
- Days 3-4: GLSL traversal functions
- Day 5: Empty space skipping optimization
- Days 6-7: Integration tests and visual validation

**Target Completion**: December 6, 2025 (before holiday break)

---

## Success Metrics

**Technical**:
- ✅ All unit tests pass (80%+ coverage for new code)
- ✅ 256³ octree loads in <100ms
- ✅ Cornell Box renders at 60 FPS
- ✅ Scene densities within ±5% of targets
- ✅ Zero Vulkan validation errors

**Research**:
- ✅ 3 test scenes ready for Phase I profiling
- ✅ Voxel data format ready for Phase K conversion
- ✅ Baseline performance measurements captured

**Documentation**:
- ✅ Update memory bank with Phase H completion
- ✅ Update checkpoint with Phase H → Phase I transition
- ✅ Document octree format for future reference

---

## Next Phase

**Phase I: Performance Profiling System**
**Start Date**: December 9, 2025 (after Phase H completion)
**Duration**: 2-3 weeks
**Goal**: Implement comprehensive performance measurement infrastructure

---

## References

- [OctreeDesign.md](VoxelStructures/OctreeDesign.md) - Octree architecture design
- [TestScenes.md](Testing/TestScenes.md) - Scene specifications
- [VoxelRayTracingResearch-TechnicalRoadmap.md](VoxelRayTracingResearch-TechnicalRoadmap.md) - Research roadmap
- [ArchitecturalPhases-Checkpoint.md](ArchitecturalPhases-Checkpoint.md) - Phase tracking

---

**End of Phase H Plan**

**Author**: Claude Code + User Collaboration
**Status**: Ready for Implementation
**Next Review**: Mid-Phase H checkpoint (November 22, 2025)
