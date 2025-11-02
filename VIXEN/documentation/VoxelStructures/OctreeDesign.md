# Sparse Voxel Octree (SVO) Design Document

**Created**: November 2, 2025
**Phase**: H.1 (Voxel Data Infrastructure - Research)
**Status**: Design Phase - Ready for Implementation

---

## Executive Summary

This document specifies the design of a **Sparse Voxel Octree (SVO)** data structure for efficient storage and GPU traversal of voxel data in the VIXEN research platform. The design balances memory efficiency (sparse storage) with GPU performance (cache-friendly linearization) based on analysis of 24 research papers.

**Key Design Choices**:
1. **Hybrid Structure**: Pointer-based coarse levels (0-4) + Morton code fine levels (5-8)
2. **Brick Map Approach**: 8³ dense bricks at leaf level for locality
3. **GPU Linearization**: Flat buffer with offset-based child pointers
4. **Target Resolution**: 256³ voxels (depth 8 octree)

---

## Research Context

### Bibliography Analysis

**Primary References** (from `C:\Users\liory\Docs\Performance comparison on rendering methods for voxel data.pdf`):

**[6] Aleksandrov et al. - "Voxelisation Algorithms and Data Structures: A Review"**
- Comprehensive survey of voxel data structures (static SVO/SVDAG, dynamic NanoVDB)
- Trade-offs: SVO (simple, fast), SVDAG (memory efficient, complex), NanoVDB (production-ready, heavy)
- **Recommendation**: Start with SVO for baseline, consider SVDAG compression in Phase L

**[2] Fang et al. - "Aokana: A GPU-Driven Voxel Rendering Framework"**
- Chunk-based SVDAG + LOD + streaming for 64K³ resolution
- Achieves ~6ms frame time, 2-4× speedup over prior methods
- Uses only ~2% of scene data in VRAM (streaming)
- **Key Insight**: Chunk-based approach critical for large scenes

**[16] Derin et al. - "Sparse Volume Rendering using Hardware Ray Tracing and Block Walking"**
- **BlockWalk algorithm**: Groups voxels into blocks for GPU coherence
- Outperforms traditional octree traversal in throughput and memory efficiency
- **Implication**: Our octree should support block-level queries for Phase L optimization

**[14] Herzberger et al. - "Hybrid Voxel Formats for Efficient Ray Tracing"**
- Compositional framework: each octree level can use different format
- Auto-code generation for format combinations
- Results show hybrid formats achieve superior Pareto-frontiers
- **Implication**: Our design should allow per-level format switching

**[22] Molenaar & Eisemann - "Editing Compressed High-resolution Voxel Scenes"**
- Real-time compression for SVDAG attributes
- Interactive editing without decompression
- **Implication**: Leave compression hooks for future work

**[23] Pätzold & Kolb - "Grid-free out-of-core voxelization to sparse voxel octrees"**
- GPU-based direct voxelization, out-of-core processing
- Most data doesn't exist on GPU simultaneously
- **Implication**: Streaming infrastructure for large scenes (Phase M)

### Key Takeaways

1. **SVO is the right baseline** - Simple, fast, well-understood
2. **Hybrid formats matter** - Different levels need different representations
3. **Chunking is essential** - Don't load entire octree at once
4. **Block-level queries needed** - For BlockWalk optimization (Phase L)
5. **Compression deferred** - SVDAG too complex for baseline, add later if needed

---

## Memory Layout Design

### Decision: Hybrid Structure

**Rationale**: Balance CPU-friendly traversal (coarse) with GPU-friendly sparse storage (fine).

```
Octree Depth 0-4 (Coarse): Pointer-based nodes (cache-friendly, fast traversal)
Octree Depth 5-8 (Fine):   Brick map (dense 8³ voxel bricks for locality)
```

**Why Hybrid?**

**Coarse Levels (0-4)**:
- Small memory footprint (few nodes at top)
- Frequently accessed during traversal
- Pointer-based structure has better cache behavior for branching

**Fine Levels (5-8)**:
- Massive node count (millions of voxels)
- Sparse occupancy (most voxels empty in typical scenes)
- Brick map reduces memory waste while maintaining locality

### Coarse Level Structure (Depth 0-4)

```cpp
// Pointer-based octree node
struct OctreeNode {
    uint32_t childOffsets[8];  // Offset into nodeBuffer (0 = empty child)
    uint8_t  childMask;         // Bitmask: which children exist (1 bit per child)
    uint8_t  leafMask;          // Bitmask: which children are leaves (brick pointers)
    uint16_t padding;           // Align to 4 bytes
    uint32_t brickOffset;       // If this is a leaf, offset into brickBuffer
};
// Size: 36 bytes per node

// Example memory layout:
// nodeBuffer[0] = root node (depth 0)
// nodeBuffer[1-8] = first level children (depth 1)
// nodeBuffer[9-72] = second level children (depth 2)
// ...
```

**childMask Encoding**:
```
Bit 0 = child[0] exists (---) - octant at (0,0,0)
Bit 1 = child[1] exists (+--) - octant at (1,0,0)
Bit 2 = child[2] exists (-+-) - octant at (0,1,0)
Bit 3 = child[3] exists (++-)
Bit 4 = child[4] exists (--+)
Bit 5 = child[5] exists (+-+)
Bit 6 = child[6] exists (-++)
Bit 7 = child[7] exists (+++  - octant at (1,1,1)
```

**leafMask Encoding**:
- Same bit positions as childMask
- 1 = child is a brick (leaf node at depth 4)
- 0 = child is an internal node (has children)

### Fine Level Structure (Depth 5-8): Brick Map

```cpp
// Dense voxel brick (8³ = 512 voxels)
struct VoxelBrick {
    uint8_t voxels[8][8][8];  // 512 bytes per brick
    // Each voxel: 0 = empty, 255 = solid (grayscale for now)
};

// Brick buffer is flat array of VoxelBrick structs
// Indexed by brickOffset from OctreeNode
```

**Why 8³ Bricks?**
- 512 bytes fits in one cache line (multiple times)
- Power of 2 simplifies indexing
- Small enough for fast uploads/edits
- Large enough for good locality

**Brick Indexing**:
```cpp
// Convert 3D voxel coords to brick offset + local coords
uint32_t brickIdx = voxelPos / 8;  // Integer division
ivec3 localPos = voxelPos % 8;     // Remainder

uint8_t voxelValue = brickBuffer[brickIdx].voxels[localPos.z][localPos.y][localPos.x];
```

### Memory Efficiency Analysis

**256³ Voxel Grid**:

**Dense 3D Texture**:
```
256 × 256 × 256 × 1 byte = 16,777,216 bytes = 16 MB
```

**Sparse Octree (SVO)**:
```
Assuming 10% occupancy (typical sparse scene):

Coarse nodes (depth 0-4):
  Depth 0: 1 node
  Depth 1: 8 nodes (max)
  Depth 2: 64 nodes (max)
  Depth 3: 512 nodes (max)
  Depth 4: 4096 nodes (max)
  Total: 4681 nodes × 36 bytes = 168,516 bytes (~164 KB)

Bricks (depth 5-8):
  256³ / 8³ = 32,768 total brick slots
  10% occupancy: 3,277 bricks
  3,277 × 512 bytes = 1,677,824 bytes (~1.6 MB)

Total: 164 KB + 1.6 MB = ~1.76 MB (vs 16 MB dense)
Compression ratio: 9.09:1
```

**Sparse Octree (SVDAG - for reference)**:
```
With DAG compression (shared subtrees):
Typical scenes: 20-50:1 compression vs dense
Our scene: ~3-5 MB for 256³ (deferred to future work)
```

---

## GPU Linearization Strategy

### Challenge

GPU shaders need **contiguous memory** and **offset-based indexing** (no pointers).

CPU uses **pointer-based structures** for flexibility.

### Solution: Flat Buffer with Offset Pointers

```cpp
// GPU buffer layout (SSBO - Storage Buffer)
struct GPUOctreeBuffer {
    uint32_t nodeCount;           // Total nodes in buffer
    uint32_t brickCount;          // Total bricks in buffer
    uint32_t maxDepth;            // Maximum octree depth (8)
    uint32_t padding;             // Align to 16 bytes

    OctreeNode nodes[];           // Dynamic array of nodes
    // Followed by:
    // VoxelBrick bricks[];       // Dynamic array of bricks
};
```

**Shader Access**:
```glsl
layout(set = 0, binding = 2) buffer OctreeBuffer {
    uint nodeCount;
    uint brickCount;
    uint maxDepth;
    uint padding;

    OctreeNode nodes[];  // Variable-length array
} octree;

// Access node
OctreeNode node = octree.nodes[nodeIndex];

// Access child
if ((node.childMask & (1 << childIdx)) != 0) {
    uint childOffset = node.childOffsets[childIdx];
    OctreeNode child = octree.nodes[childOffset];
}

// Access brick (need to calculate brick buffer start)
uint brickBufferStart = octree.nodeCount;  // Bricks start after nodes
uint brickIndex = brickBufferStart + node.brickOffset;
// ... sample brick voxels
```

### Buffer Creation (CPU → GPU)

```cpp
class SparseVoxelOctree {
public:
    // CPU-side representation
    std::vector<OctreeNode> nodes;
    std::vector<VoxelBrick> bricks;

    // Linearize for GPU upload
    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;

        // Header
        uint32_t header[4] = {
            static_cast<uint32_t>(nodes.size()),
            static_cast<uint32_t>(bricks.size()),
            maxDepth,
            0  // padding
        };
        buffer.insert(buffer.end(),
                     reinterpret_cast<uint8_t*>(header),
                     reinterpret_cast<uint8_t*>(header + 4));

        // Nodes
        buffer.insert(buffer.end(),
                     reinterpret_cast<const uint8_t*>(nodes.data()),
                     reinterpret_cast<const uint8_t*>(nodes.data() + nodes.size()));

        // Bricks
        buffer.insert(buffer.end(),
                     reinterpret_cast<const uint8_t*>(bricks.data()),
                     reinterpret_cast<const uint8_t*>(bricks.data() + bricks.size()));

        return buffer;
    }
};

// Upload to GPU
std::vector<uint8_t> gpuData = octree.Serialize();
VkBuffer octreeBuffer = CreateStorageBuffer(gpuData.size());
UploadBufferData(octreeBuffer, gpuData.data(), gpuData.size());
```

---

## Octree Construction Algorithm

### Approach: Bottom-Up from Voxel Grid

**Input**: Dense 3D voxel grid (loaded from file or procedurally generated)
**Output**: Sparse octree with bricks

**Algorithm**:
```cpp
SparseVoxelOctree BuildOctree(const uint8_t* voxelGrid, uint32_t resolution) {
    SparseVoxelOctree octree;
    octree.maxDepth = 8;  // For 256³

    // Step 1: Create bricks (depth 5-8)
    CreateBricks(octree, voxelGrid, resolution);

    // Step 2: Build coarse octree (depth 0-4)
    BuildCoarseTree(octree, resolution);

    // Step 3: Optimize (remove empty branches)
    PruneEmptyNodes(octree);

    return octree;
}

void CreateBricks(SparseVoxelOctree& octree,
                  const uint8_t* voxelGrid,
                  uint32_t resolution) {
    uint32_t bricksPerAxis = resolution / 8;

    for (uint32_t bz = 0; bz < bricksPerAxis; ++bz) {
        for (uint32_t by = 0; by < bricksPerAxis; ++by) {
            for (uint32_t bx = 0; bx < bricksPerAxis; ++bx) {
                VoxelBrick brick;
                bool brickEmpty = true;

                // Fill brick from voxel grid
                for (uint32_t z = 0; z < 8; ++z) {
                    for (uint32_t y = 0; y < 8; ++y) {
                        for (uint32_t x = 0; x < 8; ++x) {
                            uint32_t voxelX = bx * 8 + x;
                            uint32_t voxelY = by * 8 + y;
                            uint32_t voxelZ = bz * 8 + z;

                            uint32_t idx = voxelZ * resolution * resolution +
                                          voxelY * resolution +
                                          voxelX;

                            brick.voxels[z][y][x] = voxelGrid[idx];
                            if (voxelGrid[idx] > 0) {
                                brickEmpty = false;
                            }
                        }
                    }
                }

                // Only store non-empty bricks
                if (!brickEmpty) {
                    uint32_t brickOffset = octree.bricks.size();
                    octree.bricks.push_back(brick);

                    // Create leaf node pointing to brick
                    OctreeNode leafNode;
                    memset(&leafNode, 0, sizeof(leafNode));
                    leafNode.brickOffset = brickOffset;
                    // ... attach to parent at depth 4
                }
            }
        }
    }
}

void BuildCoarseTree(SparseVoxelOctree& octree, uint32_t resolution) {
    // Recursive subdivision
    octree.nodes.resize(1);  // Root node
    SubdivideNode(octree, 0, 0, glm::ivec3(0), resolution);
}

void SubdivideNode(SparseVoxelOctree& octree,
                   uint32_t nodeIdx,
                   uint32_t depth,
                   glm::ivec3 origin,
                   uint32_t size) {
    if (depth >= 4) {
        // Reached brick level, link to brick if exists
        uint32_t brickX = origin.x / 8;
        uint32_t brickY = origin.y / 8;
        uint32_t brickZ = origin.z / 8;
        // ... find brick, set brickOffset
        return;
    }

    OctreeNode& node = octree.nodes[nodeIdx];
    uint32_t childSize = size / 2;

    for (uint32_t i = 0; i < 8; ++i) {
        glm::ivec3 childOrigin = origin + glm::ivec3(
            (i & 1) ? childSize : 0,
            (i & 2) ? childSize : 0,
            (i & 4) ? childSize : 0
        );

        // Check if child region contains any voxels
        if (RegionHasVoxels(childOrigin, childSize)) {
            uint32_t childIdx = octree.nodes.size();
            octree.nodes.emplace_back();

            node.childOffsets[i] = childIdx;
            node.childMask |= (1 << i);

            SubdivideNode(octree, childIdx, depth + 1, childOrigin, childSize);
        }
    }
}

void PruneEmptyNodes(SparseVoxelOctree& octree) {
    // Remove nodes with no children (empty branches)
    // Compact node array, update offsets
    // (Deferred to implementation - non-trivial pointer fixup)
}
```

---

## GPU Traversal Algorithm

### Shader Implementation (Compute Shader)

```glsl
// Replace 3D texture sampling with octree traversal
layout(set = 0, binding = 2) buffer OctreeBuffer {
    uint nodeCount;
    uint brickCount;
    uint maxDepth;
    uint padding;
    OctreeNode nodes[];
} octree;

// Octree traversal
bool TraverseOctree(vec3 rayOrigin, vec3 rayDir, out vec3 hitColor) {
    // Start at root node
    uint nodeIdx = 0;
    uint depth = 0;
    vec3 nodeOrigin = vec3(0.0);
    float nodeSize = 256.0;  // Grid resolution

    while (depth <= octree.maxDepth) {
        OctreeNode node = octree.nodes[nodeIdx];

        // Find which child octant the ray enters
        vec3 center = nodeOrigin + vec3(nodeSize * 0.5);
        ivec3 childOctant = ivec3(greaterThan(rayOrigin, center));
        uint childIdx = childOctant.x + childOctant.y * 2 + childOctant.z * 4;

        // Check if child exists
        if ((node.childMask & (1 << childIdx)) == 0) {
            return false;  // Empty region
        }

        // Check if this is a leaf (brick pointer)
        if ((node.leafMask & (1 << childIdx)) != 0) {
            // Reached brick level, sample brick
            return SampleBrick(node.brickOffset, rayOrigin, hitColor);
        }

        // Descend to child
        nodeIdx = node.childOffsets[childIdx];
        nodeSize *= 0.5;
        nodeOrigin += vec3(childOctant) * nodeSize;
        depth++;
    }

    return false;
}

bool SampleBrick(uint brickOffset, vec3 pos, out vec3 hitColor) {
    // pos is in world space [0, 256]
    // Convert to brick-local coords [0, 8]
    ivec3 localPos = ivec3(mod(pos, 8.0));

    // Access brick voxels (need to define brick layout in buffer)
    // This requires careful offset calculation
    uint brickBufferStart = octree.nodeCount * sizeof(OctreeNode) / 4;
    uint voxelOffset = brickBufferStart + brickOffset * 512 +
                      localPos.z * 64 + localPos.y * 8 + localPos.x;

    uint voxelValue = octree.nodes[voxelOffset];  // Reinterpret buffer as bytes

    if (voxelValue > 128) {
        hitColor = vec3(voxelValue / 255.0);
        return true;
    }

    return false;
}
```

**Note**: Actual implementation will need byte-level buffer access, which may require using `uint8_t` arrays or bitwise operations.

---

## Alternative: Morton Code Encoding (Deferred)

### Concept

Instead of pointer-based octree, use **Morton code** (Z-order curve) to linearize 3D coordinates.

```cpp
// Morton code: Interleave bits of x, y, z coordinates
uint64_t MortonEncode(uint32_t x, uint32_t y, uint32_t z) {
    return (Part1By2(z) << 2) | (Part1By2(y) << 1) | Part1By2(x);
}

uint32_t Part1By2(uint32_t n) {
    n = (n ^ (n << 16)) & 0x030000ff;
    n = (n ^ (n <<  8)) & 0x0300f00f;
    n = (n ^ (n <<  4)) & 0x030c30c3;
    n = (n ^ (n <<  2)) & 0x09249249;
    return n;
}

// Use Morton code as key in sparse map
std::unordered_map<uint64_t, VoxelData> voxels;
```

**Advantages**:
- Simple sparse storage (hash map)
- No explicit tree structure
- Easy to add/remove voxels

**Disadvantages**:
- No early termination (can't skip empty regions)
- Hash lookup on GPU is slow
- Doesn't support BlockWalk optimization (Phase L)

**Decision**: Defer to future work. Pointer-based octree better for research baseline.

---

## Serialization Format

### File Format (`.svo` extension)

```
Header (32 bytes):
  [0-3]:   Magic number ('SVO\0')
  [4-7]:   Version (uint32_t, 1)
  [8-11]:  Node count (uint32_t)
  [12-15]: Brick count (uint32_t)
  [16-19]: Max depth (uint32_t)
  [20-23]: Resolution (uint32_t, e.g., 256)
  [24-31]: Reserved (zeros)

Node Array (nodeCount × 36 bytes):
  OctreeNode nodes[nodeCount];

Brick Array (brickCount × 512 bytes):
  VoxelBrick bricks[brickCount];
```

### Load/Save API

```cpp
class SparseVoxelOctree {
public:
    void SaveToFile(const std::string& filepath) const;
    static SparseVoxelOctree LoadFromFile(const std::string& filepath);

    // For cache integration (Phase A)
    std::vector<uint8_t> Serialize() const;
    static SparseVoxelOctree Deserialize(const std::vector<uint8_t>& data);
};
```

---

## Integration with Phase H Implementation

### Phase H.1: Octree Construction (10-14h)

**Files to create**:
```
ResourceManagement/include/VoxelStructures/SparseVoxelOctree.h
ResourceManagement/src/VoxelStructures/SparseVoxelOctree.cpp
ResourceManagement/include/VoxelStructures/VoxelBrick.h
```

**Implementation tasks**:
1. Define `OctreeNode` and `VoxelBrick` structs
2. Implement `BuildOctree()` from dense grid
3. Implement brick creation and linking
4. Implement coarse tree subdivision
5. Implement pruning/optimization

### Phase H.2: Voxel Data Generator (8-12h)

**Files to create**:
```
ResourceManagement/include/VoxelGenerator.h
ResourceManagement/src/VoxelGenerator.cpp
```

**Generator types**:
- Geometric primitives (sphere, cube, torus)
- Perlin noise terrain (organic shapes)
- Density-controlled (10%, 40%, 70%, 90% fill)

### Phase H.3: GPU Buffer Upload (6-8h)

**Files to create**:
```
RenderGraph/include/Nodes/VoxelBufferNodeConfig.h
RenderGraph/src/Nodes/VoxelBufferNode.cpp
```

**Implementation tasks**:
1. Serialize octree to flat buffer
2. Create VkBuffer (SSBO)
3. Upload via staging buffer
4. Transition to SHADER_READ layout

### Phase H.4: Shader Integration (4-6h)

**Files to modify**:
```
Shaders/VoxelRayMarch.comp
```

**Changes**:
1. Replace `sampler3D voxelGrid` with `buffer OctreeBuffer`
2. Implement octree traversal (replace DDA with tree walk)
3. Implement brick sampling
4. Test against 3D texture baseline (visual parity)

---

## Performance Considerations

### Memory Bandwidth

**3D Texture** (baseline):
```
Random access pattern: ~100-200 GB/s on modern GPUs
Texture cache helps, but sparse scenes waste bandwidth
```

**Octree Buffer** (Phase H):
```
Pointer-chasing: ~50-100 GB/s (worse than textures)
BUT: Skip empty regions entirely (major win for sparse scenes)
```

**BlockWalk** (Phase L):
```
Block-coherent traversal: ~150-250 GB/s (best of both worlds)
Trade pointer-chasing for block-level queries
```

### Traversal Complexity

**DDA (baseline)**:
- O(grid_resolution) worst case (256 steps for 256³)
- Every voxel checked sequentially

**Octree Traversal**:
- O(log(grid_resolution)) average case (8 steps for 256³)
- Early termination for empty regions
- Worst case: O(depth × ray_length) for dense scenes

### Cache Behavior

**3D Texture**:
- Hardware texture cache (32-64 KB L1, 256-512 KB L2)
- Excellent for coherent access patterns

**Octree Buffer**:
- Generic cache (same as texture L2)
- Worse locality due to pointer-chasing
- Brick map improves fine-level locality

---

## Testing Strategy

### Unit Tests

```cpp
// Test octree construction
TEST(SparseVoxelOctree, BuildFromDenseGrid) {
    uint8_t grid[256*256*256] = {0};
    // Set some voxels
    grid[128*256*256 + 128*256 + 128] = 255;  // Center voxel

    auto octree = BuildOctree(grid, 256);

    EXPECT_GT(octree.nodes.size(), 0);
    EXPECT_GT(octree.bricks.size(), 0);
}

// Test serialization
TEST(SparseVoxelOctree, SerializeDeserialize) {
    auto octree = CreateTestOctree();
    auto data = octree.Serialize();
    auto loaded = SparseVoxelOctree::Deserialize(data);

    EXPECT_EQ(octree.nodes.size(), loaded.nodes.size());
    EXPECT_EQ(octree.bricks.size(), loaded.bricks.size());
}

// Test GPU upload
TEST(VoxelBufferNode, UploadOctreeToGPU) {
    auto octree = CreateTestOctree();
    VoxelBufferNode node;
    node.SetInput(OCTREE_DATA, octree);
    node.Compile();

    auto buffer = node.Out(OCTREE_BUFFER);
    EXPECT_NE(buffer, VK_NULL_HANDLE);
    EXPECT_GE(GetBufferSize(buffer), octree.GetSerializedSize());
}
```

### Visual Tests

1. **Sphere Test**: 256³ grid with centered sphere (radius 64)
   - Verify octree traversal matches 3D texture output
   - Check edge cases (surface normals, interior/exterior)

2. **Terrain Test**: Perlin noise heightmap
   - Verify sparse regions skipped (performance)
   - Check smooth shading at different resolutions

3. **Sparse Test**: 10% density random voxels
   - Measure memory savings vs dense texture
   - Verify no visual artifacts from sparsity

### Performance Tests (Phase I)

```cpp
// Bandwidth comparison
BenchmarkResult BenchmarkVoxelStorage() {
    // Test 1: Dense 3D texture
    auto texResult = RenderScene(Create3DTexture(256), 300);

    // Test 2: Sparse octree
    auto octreeResult = RenderScene(CreateOctree(256, 0.1), 300);

    return {
        .texture_bandwidth_gb = texResult.bandwidth,
        .octree_bandwidth_gb = octreeResult.bandwidth,
        .memory_saved_mb = texResult.vram_mb - octreeResult.vram_mb
    };
}
```

---

## Future Enhancements (Post-Research)

### SVDAG Compression (Deferred)

**Concept**: Share identical subtrees (Directed Acyclic Graph instead of Tree)

**Memory Savings**: 10-50× compression for structured scenes

**Complexity**: Significant - requires hash-based subtree matching, pointer fixup

**When**: After baseline research complete (Phase N+)

### Streaming (Phase M+)

**Concept**: Load/unload octree chunks based on camera frustum

**Implementation**: Chunk-based octree (multiple root nodes)

**When**: For large-scale scenes (512³+), not needed for 256³ baseline

### Dynamic Updates (Phase L.3)

**Concept**: Modify individual voxels without full rebuild

**Implementation**: Incremental brick updates, partial tree rebuild

**When**: Research Phase L (dynamic scene update testing)

---

## Open Questions

### Q1: Should we support multiple resolutions (32³, 64³, 128³, 256³)?

**Answer**: Yes, for research test matrix. Use same octree structure, vary max depth.
- 32³: depth 5 (brick level only)
- 64³: depth 6
- 128³: depth 7
- 256³: depth 8 (full tree)
- 512³: depth 9 (future work)

### Q2: How to handle voxel attributes (color, material)?

**Answer**: Start with grayscale (uint8_t). Extend to RGB (3 bytes) or RGBA (4 bytes) in Phase H.3.

### Q3: Should bricks be compressed (empty voxel runs)?

**Answer**: No, keep bricks dense for baseline. Compression adds complexity without research value.

### Q4: How to handle voxel editing (add/remove voxels)?

**Answer**: Full rebuild for Phase H. Incremental updates in Phase L.3 if needed for dynamic tests.

---

## Summary

**Design Choice**: Hybrid octree (pointer-based coarse + brick map fine)

**Rationale**:
- Memory efficient for sparse scenes (9:1 compression)
- GPU-friendly linearization (flat buffer)
- Supports BlockWalk optimization (Phase L)
- Baseline complexity manageable (defer SVDAG/streaming)

**Implementation Plan**:
1. Phase H.1: Build octree from dense grid (10-14h)
2. Phase H.2: Procedural voxel generation (8-12h)
3. Phase H.3: GPU buffer upload (6-8h)
4. Phase H.4: Shader integration (4-6h)
5. **Total**: 28-40 hours for complete voxel infrastructure

**Alternative Approach: ECS Integration** (See `ECS-Octree-Integration-Analysis.md`):
- **Gaia-ECS**: Archetype-based Entity Component System for data-oriented voxel storage
- **Benefits**: 3-6× faster iteration, 2-3× memory savings, cache-friendly SoA layout
- **Trade-off**: Added complexity (25-40h additional work)
- **Decision**: Implement baseline octree first, consider ECS as Phase N+1 optimization
- **Research Value**: Independent data layout comparison (AoS vs SoA) orthogonal to pipeline choice

**Next Steps**:
1. Implement `SparseVoxelOctree` class (H.1)
2. Create test scenes (H.2)
3. Integrate with compute shader (H.4)
4. Validate against 3D texture baseline
5. (Optional Future) Implement ECS variant for extended research

**Status**: Design complete ✅ - Ready for Phase H implementation after Phase F
**Related Documents**:
- `ECS-Octree-Integration-Analysis.md` - Gaia-ECS integration analysis and future roadmap
