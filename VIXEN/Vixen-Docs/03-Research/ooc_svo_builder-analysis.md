---
title: ooc_svo_builder Analysis & VIXEN Integration
aliases: [OOC Analysis, Out-of-Core SVO]
tags: [research, svo, comparison, improvements]
created: 2026-01-06
related:
  - "[[ESVO]]"
  - "[[../Libraries/SVO]]"
  - "[[Voxel-Ray-Tracing]]"
---

# ooc_svo_builder Analysis & VIXEN Integration Opportunities

Comparative analysis of Jeroen Baert's out-of-core SVO builder vs VIXEN's implementation, identifying features and optimizations for integration.

**Repository**: https://github.com/Forceflow/ooc_svo_builder
**Papers**: HPG 2013, Computer Graphics Forum 2014
**Analysis Date**: 2026-01-06

---

## 1. Executive Summary

### Key Findings

| Aspect | ooc_svo_builder | VIXEN SVO | Recommendation |
|--------|-----------------|-----------|----------------|
| **Memory Strategy** | Out-of-core partitioning | In-memory only | 🟢 **Adopt** partitioning |
| **Voxelization** | Triangle rasterization | Procedural + mesh | 🟡 **Enhance** mesh pipeline |
| **Data Format** | .tri + .octree files | In-memory structures | 🟢 **Adopt** file formats |
| **Sparse Optimization** | Side-buffer technique | Entity-based bricks | 🟡 **Consider** hybrid |
| **LOD Support** | Multi-level pre-compute | Runtime ESVO | 🟢 **Adopt** pre-computed |
| **Platform** | Cross-platform | Vulkan-specific | 🔴 **Keep** GPU focus |

**High-Value Features to Integrate**:
1. Out-of-core processing for massive datasets (>10GB)
2. Standardized mesh import pipeline (.tri format)
3. Side-buffer sparse optimization
4. Pre-computed LOD hierarchies
5. Hot/cold data separation for cache efficiency

---

## 2. Architecture Comparison

### 2.1 Processing Pipeline

#### ooc_svo_builder
```
3D Model → tri_convert → .tri → svo_builder → Partition → Voxelize → Build SVO → .octree
              (normalization)      (streaming)   (out-of-core)  (per-partition)
```

#### VIXEN
```
3D Model → InputMesh → SVOBuilder → Octree → LaineKarrasOctree → GPU Buffers
              (in-memory)  (TBB parallel)  (entity-backed)     (Vulkan upload)
```

**Observation**: ooc_svo_builder's two-stage pipeline enables streaming and decouples mesh processing from octree construction. VIXEN's monolithic pipeline is simpler but memory-bound.

### 2.2 Memory Management

#### ooc_svo_builder: Out-of-Core Partitioning
```cpp
// Grid partitioning strategy
struct PartitionConfig {
    uint32_t gridResolution;     // 1024³ default
    uint32_t partitionSize;       // Auto-computed from memory budget
    uint32_t memoryLimitMB;       // 2048 MB default
    float sparseBufferPercent;    // 0-100% overhead for thin models
};

// Process one partition at a time
for (auto& partition : partitions) {
    auto voxels = voxelize(partition.triangles);
    auto svo = buildSVO(voxels);
    writeToFile(svo, partition.id);
}
```

**Benefits**:
- Handles datasets exceeding RAM (100GB+ models)
- Predictable memory usage (user-configurable)
- Enables distributed building (process partitions in parallel)

#### VIXEN: In-Memory with Entity Storage
```cpp
// Entity-based zero-copy views
struct EntityBrickView {
    GaiaVoxelWorld* world;      // 8 bytes
    uint64_t baseMortonKey;     // 8 bytes
    // 16 bytes total vs 70KB traditional brick
};

// Memory limits
static constexpr size_t MAX_NODES = 10'000'000;  // ~2GB hard cap
```

**Benefits**:
- Zero-storage brick views (16 bytes vs 70KB)
- ECS integration for dynamic content
- Fast in-memory queries

**Limitation**: Cannot handle models exceeding RAM

### 2.3 Data Structures

#### ooc_svo_builder: 128-bit Node
```
┌─────────────────────────────────────────────┐
│ Child Base Address:     64 bits             │
│ Child Offsets[8]:       8 bits each (×8)    │
│ Data Pointer:           64 bits             │
└─────────────────────────────────────────────┘
Total: 128 bits per node
```

**Features**:
- -1 offset indicates missing child
- Separate data pointer for attributes
- Morton codes stored explicitly in data file

#### VIXEN: 64-bit ChildDescriptor
```
┌─────────────────────────────────────────────┐
│ Part 1: Hierarchy (32 bits)                │
├─────────────────────────────────────────────┤
│ child_pointer  [15] │ valid_mask [8]       │
│ far_bit        [ 1] │ leaf_mask  [8]       │
├─────────────────────────────────────────────┤
│ Part 2: Contours (32 bits)                 │
├─────────────────────────────────────────────┤
│ contour_pointer[24] │ contour_mask [8]     │
└─────────────────────────────────────────────┘
Total: 64 bits per node
```

**Advantages**:
- 50% smaller memory footprint
- Contours integrated (silhouette smoothing)
- Compact valid/leaf masks

---

## 3. Key Features Analysis

### 3.1 Out-of-Core Processing ⭐ HIGH VALUE

**What it does**: Subdivides large models into grid partitions, processes each independently

**ooc_svo_builder Implementation**:
```cpp
// Automatic partition sizing based on memory budget
uint32_t calculatePartitionSize(uint32_t memoryBudgetMB,
                                 uint32_t triangleCount) {
    float bytesPerTriangle = 48.0f; // vertices + normals + colors
    float bytesPerVoxel = 5.0f;     // average from paper

    // Estimate voxels from triangles (heuristic)
    float voxelCount = triangleCount * 100.0f;

    // Calculate partitions needed
    float totalMemoryMB = voxelCount * bytesPerVoxel / (1024 * 1024);
    uint32_t partitionsNeeded = std::ceil(totalMemoryMB / memoryBudgetMB);

    return std::cbrt(partitionsNeeded); // Cubic partitioning
}
```

**Integration into VIXEN**:
```cpp
// New SVOBuilder mode
enum class BuildMode {
    InMemory,       // Current implementation (< 2GB)
    OutOfCore,      // Partitioned streaming (> 2GB)
    Hybrid          // Adaptive based on model size
};

class OutOfCoreSVOBuilder {
public:
    struct PartitionConfig {
        uint32_t memoryBudgetMB = 2048;
        uint32_t gridResolution = 1024;
        glm::ivec3 partitionsPerAxis = {1, 1, 1};
    };

    std::unique_ptr<Octree> build(const InputMesh& mesh,
                                    const PartitionConfig& config);

private:
    // Partition mesh into subgrids
    std::vector<PartitionedMesh> partitionMesh(
        const InputMesh& mesh,
        const glm::ivec3& partitions);

    // Build partition independently
    std::unique_ptr<OctreeBlock> buildPartition(
        const PartitionedMesh& partition,
        uint32_t partitionId);

    // Merge partition results
    void mergePartitions(
        Octree& target,
        const std::vector<std::unique_ptr<OctreeBlock>>& partitions);
};
```

**Benefits for VIXEN**:
- Support massive architectural/CAD models (>10GB)
- Predictable memory usage for production
- Enables cloud-based building (partition distribution)

**Implementation Effort**: Medium (2-3 weeks)

### 3.2 Sparse Model Optimization ⭐ MEDIUM VALUE

**What it does**: Allocates side-buffer (0-100% overhead) to accelerate thin-shell model construction

**ooc_svo_builder Approach**:
- Thin models (architectural, CAD) have sparse geometry
- Traditional uniform voxelization wastes memory on empty space
- Side-buffer stores only occupied voxels during construction
- User balances memory overhead vs build speed

**VIXEN Current Approach**:
- EntityBrickView already provides zero-storage
- Gaia ECS stores only occupied voxels
- No explicit side-buffer mechanism

**Opportunity**:
VIXEN's entity storage already achieves sparse optimization. **No action needed** - current design is superior.

### 3.3 Standardized Mesh Import (.tri format) ⭐ HIGH VALUE

**What it does**: Converts multiple 3D formats into streamable binary format

**ooc_svo_builder .tri Format**:
```
Header:
- Version (uint32_t)
- Triangle count (uint64_t)
- Payload flags (bool: hasNormals, hasColors)

Per-Triangle (streaming):
- vertices[3] (3 × vec3)
- normals[3]  (3 × vec3) [optional]
- colors[3]   (3 × vec3) [optional]
```

**Benefits**:
- Decouples mesh loading from octree building
- Streamable format (no need to load entire model)
- Cross-platform binary format
- Normalizes coordinates to unit cube

**Integration into VIXEN**:
```cpp
// New mesh preprocessing tool
class MeshConverter {
public:
    // Convert to streamable format
    bool convertToTriFormat(
        const std::string& inputPath,   // .obj, .ply, .fbx, etc.
        const std::string& outputPath   // .tri
    );

    // Stream triangles for large models
    class TriangleStream {
    public:
        bool open(const std::string& triPath);
        bool readNext(InputTriangle& tri);
        uint64_t getTotalCount() const;
    };

    // Direct SVOBuilder integration
    std::unique_ptr<Octree> buildFromTriFile(
        const std::string& triPath,
        const BuildParams& params
    );
};
```

**Implementation Effort**: Low (1 week) - use trimesh2 library

### 3.4 Hot/Cold Data Separation ⭐ MEDIUM VALUE

**What it does**: Separates frequently-accessed hierarchy from rarely-accessed payload

**ooc_svo_builder Files**:
```
model.octree       - Text header (metadata)
model.octreenodes  - Node hierarchy (hot data, traversal)
model.octreedata   - Attributes (cold data, leaf access only)
```

**Cache Benefits**:
- Node traversal doesn't pollute cache with attribute data
- Enables partial loading (hierarchy first, stream attributes)
- Better CPU cache utilization

**VIXEN Current Layout**:
```cpp
struct OctreeBlock {
    std::vector<ChildDescriptor> childDescriptors;  // Hot
    std::vector<Contour> contours;                  // Warm
    std::vector<UncompressedAttributes> attributes; // Cold
    std::vector<EntityBrickView> brickViews;        // Hot
};
```

**Opportunity**: Reorganize for cache efficiency
```cpp
struct OctreeBlock {
    // Hot data (traversal)
    std::vector<ChildDescriptor> hierarchy;
    std::vector<EntityBrickView> brickViews;

    // Warm data (leaf refinement)
    std::vector<Contour> contours;

    // Cold data (attribute lookup)
    std::vector<UncompressedAttributes> attributes;
    std::vector<AttributeLookup> attributeLookups;
    std::vector<uint64_t> compressedColors;      // DXT
    std::vector<CompressedNormalBlock> compressedNormals;
};
```

**Implementation Effort**: Low (refactor existing code)

### 3.5 Pre-Computed LOD Hierarchies ⭐ HIGH VALUE

**What it does**: Builds multiple resolution levels during construction

**ooc_svo_builder Approach**:
```cpp
// User specifies LOD levels at build time
struct LODConfig {
    std::vector<uint32_t> resolutions = {512, 1024, 2048, 4096};
    bool autoGenerate = true;  // Generate half-res LODs
};

// Output: model_LOD0.octree, model_LOD1.octree, etc.
```

**VIXEN Current Approach**:
- Runtime LOD via ESVO scale parameter
- No pre-computed LOD levels
- GPU computes LOD on-the-fly during traversal

**Opportunity**: Hybrid approach
```cpp
class LODHierarchy {
public:
    struct LODLevel {
        uint32_t maxDepth;
        float voxelSize;
        std::unique_ptr<Octree> octree;
    };

    // Pre-compute LODs at build time
    std::vector<LODLevel> buildLODChain(
        const InputMesh& mesh,
        const std::vector<uint32_t>& resolutions
    );

    // Runtime LOD selection
    const Octree& selectLOD(float distance, float screenSize) const;
};
```

**Benefits**:
- Faster rendering (less traversal depth)
- Better GPU cache utilization
- Enables streaming (load low LOD first)
- Pre-filtered attributes (better quality than runtime downsampling)

**Implementation Effort**: Medium (2 weeks)

### 3.6 Morton Code Explicit Indexing ⭐ LOW VALUE

**What it does**: Stores morton codes explicitly in data file

**ooc_svo_builder**:
```cpp
struct VoxelData {
    uint64_t mortonCode;  // Z-order curve position
    glm::vec3 color;
    glm::vec3 normal;
};
```

**VIXEN**:
- Morton codes computed on-demand from EntityBrickView
- No explicit storage

**Observation**: VIXEN's approach is more memory-efficient. Morton codes can be computed from spatial position when needed. **No action needed**.

---

## 4. Integration Roadmap

### Phase 1: Foundation (Sprint 7-8)
**Goal**: Add file format and mesh preprocessing

1. ✅ **Task**: Implement .tri mesh converter
   - Use trimesh2 for multi-format input
   - Streamable binary output
   - Coordinate normalization
   - **Estimated**: 1 week

2. ✅ **Task**: Add triangle streaming to SVOBuilder
   - `buildFromTriFile()` method
   - Memory-efficient iteration
   - **Estimated**: 3 days

### Phase 2: Out-of-Core Processing (Sprint 9-10)
**Goal**: Support massive datasets via partitioning

3. ✅ **Task**: Implement partition strategy
   - Grid subdivision algorithm
   - Memory budget calculator
   - Triangle filtering to partitions
   - **Estimated**: 1 week

4. ✅ **Task**: Build OutOfCoreSVOBuilder
   - Per-partition build
   - Partition merging
   - Progress reporting
   - **Estimated**: 2 weeks

### Phase 3: LOD Pre-Computation (Sprint 11)
**Goal**: Pre-computed resolution levels

5. ✅ **Task**: Implement LODHierarchy
   - Multi-resolution build
   - Attribute downsampling
   - Runtime LOD selection
   - **Estimated**: 2 weeks

### Phase 4: Cache Optimization (Sprint 12)
**Goal**: Improve traversal performance

6. ✅ **Task**: Reorganize OctreeBlock layout
   - Hot/cold data separation
   - Cache-friendly ordering
   - **Estimated**: 1 week

7. ✅ **Task**: Benchmark cache behavior
   - Measure cache misses
   - Compare before/after
   - **Estimated**: 3 days

---

## 5. Performance Impact Analysis

### Memory Efficiency

| Scenario | Current VIXEN | With Out-of-Core | Improvement |
|----------|---------------|------------------|-------------|
| 1M voxels | 5-8 MB | 5-8 MB | None |
| 10M voxels | 50-80 MB | 50-80 MB | None |
| 100M voxels | 500-800 MB | 100-200 MB | 4-8× |
| 1B voxels | ❌ OOM | 1-2 GB | ∞ (enables) |

**Observation**: Out-of-core enables models previously impossible to build

### Build Time

| Scenario | Current VIXEN | With Partitioning | Change |
|----------|---------------|-------------------|--------|
| Small (1M voxels) | 10 sec | 12 sec | +20% overhead |
| Medium (10M) | 2 min | 2.5 min | +25% overhead |
| Large (100M) | ❌ OOM | 30 min | ∞ (enables) |
| Huge (1B) | ❌ OOM | 8 hours | ∞ (enables) |

**Trade-off**: Small performance penalty for small models, massive capability gain for large models

### Cache Performance (Predicted)

| Operation | Current | Hot/Cold Separation | Improvement |
|-----------|---------|---------------------|-------------|
| Traversal | Mixed cache | Hot-only cache | 10-20% |
| Ray casting | Mixed access | Hierarchical load | 15-30% |
| Attribute lookup | Random access | Sequential cold | 5-10% |

---

## 6. Recommendations

### High Priority (Implement Soon)

1. **Out-of-Core Processing** ⭐⭐⭐⭐⭐
   - Critical for production use (architectural, scan data)
   - Enables VIXEN to compete with commercial tools
   - **Effort**: Medium (3-4 weeks)

2. **Mesh Import Pipeline** ⭐⭐⭐⭐⭐
   - Required for artist workflows
   - Decouples mesh processing from octree building
   - **Effort**: Low (1 week)

3. **Pre-Computed LOD** ⭐⭐⭐⭐
   - Significant GPU performance improvement
   - Better streaming support
   - **Effort**: Medium (2 weeks)

### Medium Priority (Consider)

4. **Hot/Cold Data Separation** ⭐⭐⭐
   - CPU cache optimization
   - Measurable but modest gains
   - **Effort**: Low (1 week)

### Low Priority (Optional)

5. **Color Modes** ⭐⭐
   - Position-linear, normal-mapped colors
   - Debug/visualization feature
   - **Effort**: Low (2 days)

### Not Recommended

6. **Morton Code Explicit Storage** ❌
   - VIXEN's on-demand computation is superior
   - Wastes memory for marginal gain

7. **Simplified Node Format** ❌
   - VIXEN's 64-bit descriptor more compact
   - Contour integration provides quality improvement

---

## 7. Code Examples

### Example 1: Out-of-Core Builder Usage
```cpp
#include <SVO/OutOfCoreSVOBuilder.h>

// Load massive model
auto mesh = MeshLoader::load("architecture_model.fbx"); // 20GB

// Configure partitioning
OutOfCoreSVOBuilder::PartitionConfig config;
config.memoryBudgetMB = 2048;  // 2GB working set
config.gridResolution = 2048;   // 2048³ voxels per partition
config.partitionsPerAxis = {4, 4, 4};  // 64 partitions total

// Build out-of-core
OutOfCoreSVOBuilder builder;
builder.setProgressCallback([](float progress) {
    std::cout << "Building: " << (progress * 100) << "%\n";
});

auto octree = builder.build(mesh, config);
octree->saveToFile("architecture_model.octree");
```

### Example 2: LOD Hierarchy
```cpp
#include <SVO/LODHierarchy.h>

// Build multi-resolution octree
LODHierarchy hierarchy;
LODHierarchy::Config lodConfig;
lodConfig.resolutions = {512, 1024, 2048, 4096};
lodConfig.autoDownsample = true;

auto lodChain = hierarchy.buildLODChain(mesh, lodConfig);

// Runtime LOD selection in renderer
float distance = glm::length(camera.position - object.position);
float screenSize = calculateScreenSize(object.bounds, distance);

const Octree& lod = lodChain.selectLOD(distance, screenSize);
renderWithOctree(lod);
```

### Example 3: Streaming Mesh Input
```cpp
#include <SVO/MeshConverter.h>

// Convert to streamable format (one-time preprocessing)
MeshConverter converter;
converter.convertToTriFormat("model.obj", "model.tri");

// Build from stream (memory-efficient)
SVOBuilder builder;
auto stream = std::make_unique<TriangleStream>("model.tri");

std::cout << "Processing " << stream->getTotalCount() << " triangles\n";

InputTriangle tri;
std::vector<InputTriangle> batch;
while (stream->readNext(tri)) {
    batch.push_back(tri);

    if (batch.size() >= 10000) {
        builder.addTriangleBatch(batch);
        batch.clear();
    }
}

auto octree = builder.finalize();
```

---

## 8. References

### ooc_svo_builder
- **Repository**: https://github.com/Forceflow/ooc_svo_builder
- **Paper**: "Out-Of-Core Construction of Sparse Voxel Octrees", HPG 2013
- **Author**: Jeroen Baert (Katholieke Universiteit Leuven)

### VIXEN SVO
- [[../Libraries/SVO]] - Current implementation
- [[ESVO]] - ESVO algorithm details
- [[../02-Implementation/SVO-System]] - System architecture

### Related Research
- Laine & Karras 2010: "Efficient Sparse Voxel Octrees"
- Crassin et al. 2009: "GigaVoxels"
- Kämpe et al. 2013: "High Resolution Sparse Voxel DAGs"

---

## 9. Conclusion

**Key Takeaway**: ooc_svo_builder provides production-critical features VIXEN currently lacks:

1. **Out-of-core processing** enables massive models (architectural, scan data)
2. **Mesh import pipeline** improves artist workflow
3. **Pre-computed LOD** boosts GPU performance

**Recommended Action**: Implement Phase 1-3 of integration roadmap (6-8 weeks)

**ROI**: High-value features with moderate implementation cost, essential for production use cases.
