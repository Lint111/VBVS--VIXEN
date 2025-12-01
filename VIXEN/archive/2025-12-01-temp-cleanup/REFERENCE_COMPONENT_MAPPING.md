# Reference Implementation Component Mapping

**Source:** `C:\Users\liory\Downloads\source-archive\efficient-sparse-voxel-octrees\trunk\src\octree`
**Target:** `C:\cpp\VBVS--VIXEN\VIXEN\libraries\SVO`

---

## 1. Core Traversal Algorithm

### Reference: `cuda/Raycast.inl`

**Lines 88-358:** Main `castRay()` device function

**Algorithm Breakdown:**

| Lines | Component | VIXEN Target | Status |
|-------|-----------|--------------|--------|
| 90-98 | Epsilon handling (avoid div-by-zero) | `LaineKarrasOctree.cpp:430-438` | ❌ Missing |
| 100-109 | Parametric plane coefficients (tx, ty, tz) | `LaineKarrasOctree.cpp:440-448` | ❌ Missing |
| 114-117 | XOR octant mirroring | `LaineKarrasOctree.cpp:450-453` | ❌ Missing |
| 121-125 | t_min/t_max initialization | `LaineKarrasOctree.cpp:455-459` | ✅ Have (basic) |
| 129-138 | Root voxel initialization | `LaineKarrasOctree.cpp:461-469` | ⚠️ Different |
| 143-153 | Main loop + iteration counter | `LaineKarrasOctree.cpp:471-641` | ⚠️ Different |
| 157-161 | Child descriptor fetch | `LaineKarrasOctree.cpp:485-492` | ✅ Have |
| 166-169 | t_corner calculation | `LaineKarrasOctree.cpp:494-497` | ❌ Missing |
| 174-182 | Child mask check + LOD termination | `LaineKarrasOctree.cpp:499-509` | ⚠️ Partial |
| 188-220 | **CONTOUR INTERSECTION** | `LaineKarrasOctree.cpp:511-540` | ❌ **CRITICAL MISSING** |
| 225-274 | Descend + Push stack | `LaineKarrasOctree.cpp:542-584` | ⚠️ Different logic |
| 277-290 | Advance ray | `LaineKarrasOctree.cpp:586-604` | ⚠️ Different logic |
| 294-327 | Pop stack | `LaineKarrasOctree.cpp:606-633` | ❌ Missing |
| 342-346 | Undo mirroring | `LaineKarrasOctree.cpp:635-639` | ❌ Missing |

**Priority Actions:**

1. **CRITICAL:** Port contour intersection (lines 188-220) - enables surface detail
2. **CRITICAL:** Port XOR mirroring (lines 114-117) - 2× speedup (no branching)
3. **HIGH:** Port parametric planes (lines 100-109) - correct t-value calculation
4. **HIGH:** Port stack pop (lines 294-327) - fixes backtracking bugs
5. **MEDIUM:** Port undo mirroring (lines 342-346) - correct final position

---

### Reference: `cuda/Util.inl`

**Lines 30-173:** Device math utilities

| Function | Purpose | VIXEN Equivalent | Notes |
|----------|---------|------------------|-------|
| `fmaxf3(a,b,c)` | 3-way max | `std::max({a,b,c})` | ✅ GLM has this |
| `fminf3(a,b,c)` | 3-way min | `std::min({a,b,c})` | ✅ GLM has this |
| `normalize(v)` | Vector normalization | `glm::normalize(v)` | ✅ Use GLM |
| `cross(a,b)` | Cross product | `glm::cross(a,b)` | ✅ Use GLM |
| `fromABGR(u32)` | Color unpacking | `SVOTypes.cpp:decodeColor()` | ⚠️ Different format |
| `toABGR(float4)` | Color packing | `SVOTypes.cpp:encodeColor()` | ⚠️ Different format |

**Action:** Replace CUDA vector ops with GLM equivalents (no new code needed)

---

## 2. Attribute Compression

### Reference: `cuda/AttribLookup.inl`

**Lines 57-77:** DXT color decompression

```cuda
// Reference algorithm:
__device__ inline float3 decodeDXTColor(U64 block, int texelIdx) {
    U32 head = (U32)block;                    // Color endpoints (RGB565)
    U32 bits = (U32)(block >> 32);            // 2-bit indices

    F32 c0 = c_dxtColorCoefs[(bits >> (texelIdx * 2)) & 3];
    F32 c1 = 1.0f / (F32)(1 << 24) - c0;

    return make_float3(
        c0 * (F32)(head << 16) + c1 * (F32)head,
        c0 * (F32)(head << 21) + c1 * (F32)(head << 5),
        c0 * (F32)(head << 27) + c1 * (F32)(head << 11)
    );
}
```

**VIXEN Target:** `libraries/SVO/src/Compression.cpp` (new file)

```cpp
glm::vec3 decodeDXTColor(uint64_t block, int texelIdx) {
    // Direct C++ translation
    uint32_t head = static_cast<uint32_t>(block);
    uint32_t bits = static_cast<uint32_t>(block >> 32);

    float c0 = DXT_COLOR_COEFS[(bits >> (texelIdx * 2)) & 3];
    float c1 = 1.0f / static_cast<float>(1 << 24) - c0;

    return glm::vec3(
        c0 * static_cast<float>(head << 16) + c1 * static_cast<float>(head),
        c0 * static_cast<float>(head << 21) + c1 * static_cast<float>(head << 5),
        c0 * static_cast<float>(head << 27) + c1 * static_cast<float>(head << 11)
    );
}
```

---

**Lines 88-107:** DXT normal decompression

```cuda
__device__ inline float3 decodeDXTNormal(U64 blockA, U64 blockB, int texelIdx) {
    U32 headBase = (U32)blockA;
    U32 headUV   = (U32)blockB;
    U32 bitsU    = (U32)(blockA >> 32);
    U32 bitsV    = (U32)(blockB >> 32);

    int shift = texelIdx * 2;
    F32 cu = c_dxtNormalCoefs[(bitsU >> shift) & 3];
    F32 cv = c_dxtNormalCoefs[(bitsV >> shift) & 3];

    cu *= __int_as_float(((headUV & 15) + (127 + 3 - 13)) << 23);
    cv *= __int_as_float((((headUV >> 16) & 15) + (127 + 3 - 13)) << 23);

    float3 base = decodeRawNormal(headBase);
    return make_float3(
        base.x + cu * (F32)(S32)(headUV << 16) + cv * (F32)(S32)headUV,
        base.y + cu * (F32)(S32)(headUV << 20) + cv * (F32)(S32)(headUV << 4),
        base.z + cu * (F32)(S32)(headUV << 24) + cv * (F32)(S32)(headUV << 8)
    );
}
```

**VIXEN Target:** Same `Compression.cpp`

**Dependencies:**
- `decodeRawNormal()` (lines 40-52) - point-on-cube encoding
- `c_dxtNormalCoefs` constants (lines 81-86)

---

**Lines 133-186:** Palette attribute lookup (VOXELATTRIB_PALETTE mode)

**Purpose:** Fetch color/normal from parent voxel if current voxel has no attribute

**Algorithm:**
1. Check if current voxel has attribute (via palette bitmask)
2. If not, traverse up tree until attribute found
3. Return parent's attribute

**VIXEN Status:** ❌ Not implemented (currently assume all voxels have attributes)

**Action:** Implement in `LaineKarrasOctree::getVoxelData()` - low priority (optimization)

---

**Lines 194-311:** Corner interpolation (VOXELATTRIB_CORNER mode)

**Purpose:** Trilinear interpolation from 8 corner attributes for smoother gradients

**Algorithm:**
1. Find voxel with corner attributes (may need to go up tree)
2. Determine which of 27 subcubes hit point is in (3×3×3 grid)
3. Fetch 8 corner attributes
4. Compute lerp weights based on fractional position
5. Interpolate color + normal

**VIXEN Status:** ❌ Not implemented (currently return nearest voxel)

**Action:** Implement in Phase 3 (DXT compression) - improves visual quality

---

**Lines 318-344:** DXT block lookup (VOXELATTRIB_DXT mode)

**Purpose:** Fetch and decode DXT-compressed attribute

**Algorithm:**
1. Find page header from node pointer (align to 8KB boundary)
2. Navigate to block info
3. Find attribute attachment
4. Fetch DXT block (8 or 16 bytes)
5. Decode using texel index

**VIXEN Status:** ❌ Not implemented (no compression yet)

**Action:** Implement in Phase 3 (after DXT encoder written)

---

## 3. File I/O and Serialization

### Reference: `io/OctreeFile.hpp`

**Lines 42-149:** Main OctreeFile class

**Features:**
- Multi-object support (array of octree objects)
- Slice-based paging (8KB slices loaded on-demand)
- Clustered file format (compressed blocks)
- Async prefetch for streaming

**VIXEN Status:**
- Have: Simple `serialize()/deserialize()` in `Serialization.cpp`
- Missing: Multi-object, paging, compression, async I/O

**Decision:** ❌ Do NOT adopt initially
- Our use case: single object, fits in memory
- Reference is for massive out-of-core datasets (>100 GB)
- Can adopt later if needed

---

**Lines 153-251:** OctreeSlice structure

**Purpose:** Represents one 8KB block of octree data

**Key fields:**
- `cubePos` - 3D position in octree (fixed-point)
- `cubeScale` - log2 size of slice
- `nodeScale` - log2 size of nodes within slice
- `childEntries` - array of child slice IDs (or -1 if leaf, -2 if split)
- `nodeSplitPtr` - bitfield of which nodes are split
- `nodeValidMaskPtr` - bitfield of which children exist

**VIXEN Equivalent:** ❌ No slicing (flat node array)

**Action:** Phase 4 (page headers) - add slicing for cache optimization

---

**Lines 254-351:** File format documentation (comments)

**Format overview:**
- Based on clustered file (LZ4-compressed 64KB chunks)
- Stores meshes in binary format
- SliceStatePacked (2 bits per slice: Unused/Unbuilt/Complete)
- Object metadata (transforms, attachment types)

**Action:** Reference for future `.svo` file format design

---

### Reference: `io/OctreeRuntime.hpp`

**Lines 40-80:** Runtime constants and enums

| Constant | Value | Purpose | VIXEN Equivalent |
|----------|-------|---------|------------------|
| `PageBytesLog2` | 13 | 8KB pages | ❌ No paging |
| `PageBytes` | 8192 | Page size | ❌ N/A |
| `PageSize` | 2048 | Dwords per page | ❌ N/A |
| `TrunksPerBlock` | 512 | Node batch size | ❌ Flat array |

**Action:** Adopt in Phase 4 (page headers)

---

**Lines 82-217:** OctreeRuntime class

**Purpose:** GPU-side octree representation with page management

**Key methods:**

| Method | Purpose | VIXEN Equivalent |
|--------|---------|------------------|
| `getRootNodeCPU()` | Get CPU pointer to root | `m_octree->root` |
| `getRootNodeCuda()` | Get GPU pointer to root | `getGPUBuffers()` |
| `findSlices()` | LOD selection for view frustum | ❌ Not needed (single LOD) |
| `loadSlice()` | Stream slice from disk to GPU | ❌ All in memory |
| `unloadSlice()` | Evict slice to free memory | ❌ No streaming |

**Action:** Not needed unless we support >10GB octrees (unlikely)

---

**Lines 286-333:** Data format documentation (comments)

**GPU memory layout:**
```
Block (aligned to 8KB):
  [PageHeader (2 dwords)]
  [Nodes interleaved with FarPtrs]
  [PagePadding if needed]
  ...
  [BlockInfo (4 dwords)]
  [BlockAttachInfo array]
  [Attachment data pool]
```

**Node format (2 dwords = 64 bits):**
```
Dword 0:
  bits 17-31: childPtr (15-bit offset)
  bit  16:    farBit (near vs far pointer)
  bits 8-15:  validMask (8-bit child existence)
  bits 0-7:   nonLeafMask (8-bit leaf flags)

Dword 1:
  bits 8-31:  contourPtr (24-bit offset)
  bits 0-7:   contourMask (8-bit contour flags)
```

**VIXEN Current Format (ChildDescriptor):**
```cpp
struct ChildDescriptor {
    uint8_t validMask;
    uint8_t leafMask;
    bool farBit;
    uint32_t childPointer;  // 15-bit
    uint32_t brickOffset;   // Unused
};
```

**Action:** ✅ Our format matches reference! Just need to pack correctly.

---

## 4. Builder Components

### Reference: `build/BuilderMesh.hpp`

**Lines 55-83:** Triangle structure

**Fields:**

| Field | Type | Purpose | VIXEN Equivalent |
|-------|------|---------|------------------|
| `p, pu, pv` | `Vec3f` | Triangle vertices (v0, v1-v0, v2-v0) | ✅ `InputMesh::triangles` |
| `plo, phi` | `Vec3f` | AABB bounds | ✅ Computed in `SVOBuilder` |
| `n, nu, nv` | `Vec3f` | Normals (interpolated) | ✅ Have |
| `nlo, nhi` | `Vec3f` | Normal bounds | ❌ Missing |
| `color` | `Vec4f` | Material diffuse | ✅ Have |
| `geomNormal` | `Vec3f` | Face normal | ✅ Have |
| `area` | `F32` | Triangle area | ✅ Have |
| `average` | `AttribFilter::Value` | Average color/normal | ✅ Have in `AttributeIntegrator` |
| `boundaryMask` | `U8` | Edge flags | ❌ Missing (for crack prevention) |
| `colorTex` | `TextureSampler*` | Texture sampler | ❌ Missing (use solid color) |
| `dispTri` | `DisplacedTriangle*` | Displacement map | ❌ Missing (no displacement) |

**Action:** Add `nlo, nhi, boundaryMask` to `InputMesh::Triangle` - improves quality

---

**Lines 85-97:** Batch structure

**Purpose:** Group triangles into cache-friendly batches (1024 triangles each)

**Benefits:**
- LRU cache (keep hot batches expanded, collapse cold batches)
- Multi-threaded access (lock mask prevents race conditions)
- Memory efficiency (don't expand all triangles upfront)

**VIXEN Status:** ❌ No batching (load all triangles upfront)

**Action:** Low priority - only needed for >100K triangles

---

**Lines 99-127:** `getTri()` with caching

**Algorithm:**
1. Check if triangle's batch is locked by this thread → fast path
2. If not locked, acquire lock and expand batch
3. Use LRU policy to collapse least-recently-used batch
4. Return triangle reference

**VIXEN Status:** ❌ Direct array access (no caching)

**Action:** Not needed (our models fit in memory)

---

### Reference: `build/BuilderMesh.cpp`

**Lines 50-500:** Construction and subdivision logic

**Key functions:**

| Function | Lines | Purpose | VIXEN Equivalent |
|----------|-------|---------|------------------|
| `BuilderMesh()` | 52-200 | Load mesh, compute bounds, batch triangles | `SVOBuilder::build()` entry |
| `subdivideTriangles()` | 202-350 | Recursively split triangles for displacement | ❌ No displacement |
| `constructBatch()` | 352-450 | Build triangle batch with textures/normals | `SVOBuilder::subdivideNode()` |
| `expandBatch()` | 452-480 | Uncompress batch (fetch textures, compute normals) | ❌ No batching |
| `collapseBatch()` | 482-500 | Free batch memory | ❌ N/A |

**Action:** Reference for batch system (if needed in future)

---

### Reference: `build/ContourShaper.hpp/cpp`

**Lines 67-128:** HullShaper class (contour construction)

**Algorithm (from `HullShaper::setVoxel()`):**

1. **Collect candidate planes** (from triangle normals)
   ```cpp
   for (each triangle in voxel) {
       addPlane(triangle.area, triangle.normal);
   }
   ```

2. **Build convex hull** (greedy polyhedron construction)
   ```cpp
   ConvexPolyhedron hull;
   for (each plane) {
       hull.clip(plane);
   }
   ```

3. **Find best parallel plane pair** (maximize separation)
   ```cpp
   float maxThickness = 0;
   int bestPlane = -1;
   for (each plane) {
       float thickness = hull.computeThickness(plane.normal);
       if (thickness > maxThickness) {
           maxThickness = thickness;
           bestPlane = plane;
       }
   }
   ```

4. **Encode contour** (32-bit format)
   ```cpp
   contour = encodeContour(bestPlane.normal, maxThickness);
   ```

**VIXEN Status:** ⚠️ `ContourBuilder.cpp` has greedy algorithm but no convex hull

**Action:** Phase 4 - add convex hull computation (improves contour quality)

---

**Lines 99-128:** Refinement check

**Purpose:** Decide if voxel needs subdivision based on contour error

**Algorithm:**
```cpp
bool HullShaper::needToRefine() {
    // Compute maximum deviation of geometry from contour planes
    float maxDeviation = 0;
    for (each triangle) {
        for (each vertex) {
            float deviation = abs(dot(vertex - voxelCenter, contour.normal) - contour.thickness/2);
            maxDeviation = max(maxDeviation, deviation);
        }
    }
    return maxDeviation > m_maxDeviation;
}
```

**VIXEN Status:** ⚠️ `SVOBuilder` uses different termination criteria (geometric error + color variance)

**Action:** Add contour-based termination to `SVOBuilder::shouldSubdivide()`

---

## 5. Summary Table

### Must Adopt (CRITICAL)

| Component | Reference File | VIXEN Target | Priority | Estimated Effort |
|-----------|----------------|--------------|----------|------------------|
| Parametric plane traversal | `cuda/Raycast.inl:100-109` | `LaineKarrasOctree.cpp:440-448` | P0 | 2 hours |
| XOR octant mirroring | `cuda/Raycast.inl:114-117` | `LaineKarrasOctree.cpp:450-453` | P0 | 1 hour |
| Contour intersection | `cuda/Raycast.inl:188-220` | `LaineKarrasOctree.cpp:511-540` | P0 | 4 hours |
| Stack pop logic | `cuda/Raycast.inl:294-327` | `LaineKarrasOctree.cpp:606-633` | P0 | 3 hours |
| DXT color decode | `cuda/AttribLookup.inl:65-76` | `Compression.cpp` (new) | P0 | 2 hours |
| DXT normal decode | `cuda/AttribLookup.inl:88-106` | `Compression.cpp` (new) | P0 | 3 hours |

**Total Critical Path: ~15 hours (2 days)**

---

### Should Adopt (HIGH)

| Component | Reference File | VIXEN Target | Priority | Estimated Effort |
|-----------|----------------|--------------|----------|------------------|
| Page header structure | `io/OctreeRuntime.hpp:286-297` | `SVOTypes.h` | P1 | 4 hours |
| Convex hull contours | `build/ContourShaper.cpp:150-280` | `ContourBuilder.cpp` | P1 | 8 hours |
| Corner interpolation | `cuda/AttribLookup.inl:195-310` | `LaineKarrasOctree.cpp` | P1 | 6 hours |
| GPU buffer packing | `io/OctreeRuntime.cpp:200-400` | `LaineKarrasOctree::getGPUBuffers()` | P1 | 5 hours |

**Total High Priority: ~23 hours (3 days)**

---

### Nice to Have (MEDIUM)

| Component | Reference File | VIXEN Target | Priority | Estimated Effort |
|-----------|----------------|--------------|----------|------------------|
| Triangle batching | `build/BuilderMesh.cpp:352-500` | `SVOBuilder.cpp` | P2 | 12 hours |
| Palette lookup | `cuda/AttribLookup.inl:135-186` | `LaineKarrasOctree.cpp` | P2 | 4 hours |
| Refinement checks | `build/ContourShaper.cpp:99-128` | `SVOBuilder.cpp` | P2 | 3 hours |

**Total Medium Priority: ~19 hours (2.5 days)**

---

### Skip for Now (LOW/DEFERRED)

| Component | Reference File | Reason |
|-----------|----------------|--------|
| Multi-object support | `io/OctreeFile.hpp` | Single object sufficient |
| Slice-based paging | `io/OctreeFile.hpp:153-251` | All data fits in memory |
| Displacement mapping | `build/DisplacementMap.cpp` | Not needed for our use case |
| Async prefetch | `io/OctreeFile.cpp` | Not streaming from disk |
| Ambient occlusion | `AmbientProcessor.cpp` | Future feature |
| Texture sampling | `build/TextureSampler.cpp` | Use solid colors initially |

---

## Next Actions

**Week 1: Core Traversal**
1. ✅ Read `cuda/Raycast.inl` in detail
2. Port parametric planes (2 hrs)
3. Port XOR mirroring (1 hr)
4. Port contour intersection (4 hrs)
5. Port stack pop (3 hrs)
6. Test with cube scene (2 hrs)
7. Benchmark (1 hr)

**Total: 13 hours (1.5 days)**

---

**Week 2: GPU Integration**
1. Study CUDA→GLSL translation patterns (3 hrs)
2. Port `Raycast.inl` to `OctreeTraversal.comp.glsl` (8 hrs)
3. Implement `getGPUBuffers()` (5 hrs)
4. Test GPU ray caster (4 hrs)

**Total: 20 hours (2.5 days)**

---

**Week 3: DXT Compression**
1. Implement DXT color encode/decode (5 hrs)
2. Implement DXT normal encode/decode (6 hrs)
3. Update attribute integrator (4 hrs)
4. Test compression quality (3 hrs)

**Total: 18 hours (2.25 days)**

---

**Week 4: Polish**
1. Add page headers (4 hrs)
2. Enhance contour builder (8 hrs)
3. Add corner interpolation (6 hrs)
4. Write tests (4 hrs)

**Total: 22 hours (2.75 days)**

---

**GRAND TOTAL: 73 hours (9 days)**

Buffer: 3 days for debugging/documentation

**Final estimate: 2 weeks (12 days)**

---

**Document Status:** COMPLETE
**Ready to implement:** ✅ YES
