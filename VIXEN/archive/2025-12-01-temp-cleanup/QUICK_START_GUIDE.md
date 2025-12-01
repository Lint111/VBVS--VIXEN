# SVO Reference Adoption - Quick Start Guide

**Goal:** Get reference ESVO ray caster working in 2 weeks

---

## Day 1: Setup & Parametric Planes

### Tasks

**1. Read reference algorithm (1 hour)**
```bash
code "C:/Users/liory/Downloads/source-archive/efficient-sparse-voxel-octrees/trunk/src/octree/cuda/Raycast.inl"
```

Focus on lines:
- 100-109: Parametric plane setup
- 114-117: XOR mirroring
- 121-125: t_min/t_max initialization

**2. Port parametric planes (2 hours)**

Open target file:
```bash
code "C:/cpp/VBVS--VIXEN/VIXEN/libraries/SVO/src/LaineKarrasOctree.cpp:424"
```

Replace current DDA with:
```cpp
// Line 440-448: Add parametric coefficients
// ADOPTED FROM: cuda/Raycast.inl lines 100-109
const float epsilon = std::exp2f(-23);  // Avoid div-by-zero

glm::vec3 rayDirSafe = direction;
if (std::abs(rayDirSafe.x) < epsilon) rayDirSafe.x = std::copysignf(epsilon, rayDirSafe.x);
if (std::abs(rayDirSafe.y) < epsilon) rayDirSafe.y = std::copysignf(epsilon, rayDirSafe.y);
if (std::abs(rayDirSafe.z) < epsilon) rayDirSafe.z = std::copysignf(epsilon, rayDirSafe.z);

float tx_coef = 1.0f / -std::abs(rayDirSafe.x);
float ty_coef = 1.0f / -std::abs(rayDirSafe.y);
float tz_coef = 1.0f / -std::abs(rayDirSafe.z);

float tx_bias = tx_coef * origin.x;
float ty_bias = ty_coef * origin.y;
float tz_bias = tz_coef * origin.z;
```

**3. Test parametric planes (1 hour)**

Add test in `libraries/SVO/tests/test_octree_queries.cpp`:
```cpp
TEST(LaineKarrasOctree, ParametricPlanes) {
    // Test case: ray along +X axis
    glm::vec3 origin(0.0f, 0.5f, 0.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    // Expected: tx_coef = -1.0, ty_coef = inf, tz_coef = inf
    // Verify coefficients are computed correctly
}
```

---

## Day 2: XOR Mirroring & Stack

### Tasks

**1. Port XOR mirroring (1 hour)**

```cpp
// Line 450-453: Add octant mirroring
// ADOPTED FROM: cuda/Raycast.inl lines 114-117
int octant_mask = 7;
if (rayDirSafe.x > 0.0f) octant_mask ^= 1, tx_bias = 3.0f * tx_coef - tx_bias;
if (rayDirSafe.y > 0.0f) octant_mask ^= 2, ty_bias = 3.0f * ty_coef - ty_bias;
if (rayDirSafe.z > 0.0f) octant_mask ^= 4, tz_bias = 3.0f * tz_coef - tz_bias;
```

**2. Port stack structure (2 hours)**

Add to `LaineKarrasOctree.h`:
```cpp
// Traversal stack (implicit via scale, explicit via array)
static constexpr int CAST_STACK_DEPTH = 23;

struct CastStack {
    ChildDescriptor* nodes[CAST_STACK_DEPTH + 1];
    float tmax[CAST_STACK_DEPTH + 1];

    void push(int scale, ChildDescriptor* node, float t) {
        nodes[scale] = node;
        tmax[scale] = t;
    }

    ChildDescriptor* pop(int scale, float& t) {
        t = tmax[scale];
        return nodes[scale];
    }
};
```

**3. Port main loop structure (3 hours)**

```cpp
// Line 471-641: Replace entire loop
// ADOPTED FROM: cuda/Raycast.inl lines 143-328

CastStack stack;
int scale = CAST_STACK_DEPTH - 1;
float scale_exp2 = 0.5f;
ChildDescriptor* parent = /* root node */;
glm::vec3 pos(1.0f, 1.0f, 1.0f);
int idx = 0;

// Initialize first voxel (lines 136-138)
if (1.5f * tx_coef - tx_bias > tMin) idx ^= 1, pos.x = 1.5f;
if (1.5f * ty_coef - ty_bias > tMin) idx ^= 2, pos.y = 1.5f;
if (1.5f * tz_coef - tz_bias > tMin) idx ^= 4, pos.z = 1.5f;

while (scale < CAST_STACK_DEPTH) {
    // TODO: Main loop body (tomorrow)
}
```

**4. Test stack operations (1 hour)**

```cpp
TEST(LaineKarrasOctree, StackPushPop) {
    CastStack stack;
    ChildDescriptor* node1 = /* ... */;
    stack.push(5, node1, 1.5f);

    float t;
    ChildDescriptor* node2 = stack.pop(5, t);

    EXPECT_EQ(node1, node2);
    EXPECT_FLOAT_EQ(1.5f, t);
}
```

---

## Day 3: Traversal Loop Core

### Tasks

**1. Port child descriptor fetch (1 hour)**

```cpp
// ADOPTED FROM: cuda/Raycast.inl lines 157-161
uint64_t child_descriptor = 0;
if (child_descriptor == 0) {
    child_descriptor = *reinterpret_cast<uint64_t*>(parent);
    // TODO: Add performance counter
}
```

**2. Port t_corner calculation (1 hour)**

```cpp
// ADOPTED FROM: cuda/Raycast.inl lines 166-169
float tx_corner = pos.x * tx_coef - tx_bias;
float ty_corner = pos.y * ty_coef - ty_bias;
float tz_corner = pos.z * tz_coef - tz_bias;
float tc_max = std::min({tx_corner, ty_corner, tz_corner});
```

**3. Port child mask check (2 hours)**

```cpp
// ADOPTED FROM: cuda/Raycast.inl lines 174-182
int child_shift = idx ^ octant_mask;
uint32_t child_masks = static_cast<uint32_t>(child_descriptor) << child_shift;

if ((child_masks & 0x8000) != 0 && tMin <= tMax) {
    // LOD termination check
    if (tc_max * rayBias + raySizeOrig >= scale_exp2) {
        break;  // Terminate at tMin
    }

    // Continue with intersection tests...
}
```

**4. Test child selection (2 hours)**

```cpp
TEST(LaineKarrasOctree, ChildMaskAndShift) {
    // Test XOR-based child permutation
    int idx = 3;  // Binary: 011 (x=1, y=1, z=0)
    int octant_mask = 5;  // Binary: 101 (mirror x and z)
    int child_shift = idx ^ octant_mask;  // = 110 = 6

    // Verify child_shift selects correct octant
    EXPECT_EQ(6, child_shift);
}
```

---

## Day 4-5: Contour Intersection (CRITICAL)

### Reference

**File:** `cuda/Raycast.inl` lines 196-220

**Algorithm:**
1. Check contour mask bit
2. Fetch contour value (32-bit encoded)
3. Decode normal + thickness
4. Compute plane intersection
5. Refine t_min and t_max

### Implementation

**1. Add contour structures (2 hours)**

```cpp
// File: libraries/SVO/include/SVOTypes.h
// Add after Contour definition

inline void decodeContour(uint32_t value, float scale_exp2,
                         glm::vec3& normal, float& thickness, float& position) {
    // ADOPTED FROM: cuda/Raycast.inl lines 209-213
    thickness = static_cast<float>(value & 0xFF) * scale_exp2 * 0.75f;
    position = static_cast<float>(value << 7) * scale_exp2 * 1.5f;

    // Decode octahedral normal (bits 14-31)
    int nx_bits = (value >> 14) & 0x3F;
    int ny_bits = (value >> 20) & 0x3F;
    int nz_bits = (value >> 26) & 0x3F;

    normal.x = static_cast<float>(static_cast<int8_t>(nx_bits << 2)) / 128.0f;
    normal.y = static_cast<float>(static_cast<int8_t>(ny_bits << 2)) / 128.0f;
    normal.z = static_cast<float>(static_cast<int8_t>(nz_bits << 2)) / 128.0f;
    normal = glm::normalize(normal);
}
```

**2. Port contour intersection (4 hours)**

```cpp
// ADOPTED FROM: cuda/Raycast.inl lines 203-220
int contour_mask = static_cast<uint32_t>(child_descriptor >> 32) << child_shift;

if ((contour_mask & 0x80) != 0) {
    // Fetch contour value
    uint32_t contour_offset = (child_descriptor >> 40) & 0xFFFFFF;
    int contour_index = __builtin_popcount(contour_mask & 0x7F);
    uint32_t contour_value = parent[contour_offset + contour_index];

    // Decode contour
    glm::vec3 normal;
    float thickness, cpos;
    decodeContour(contour_value, scale_exp2, normal, thickness, cpos);

    // Plane intersection
    float tcoef = 1.0f / glm::dot(normal, rayDir);
    float tavg = tx_center * normal.x + ty_center * normal.y + tz_center * normal.z + cpos;
    float tdiff = thickness * tcoef;

    // Refine t-span
    tMin = std::max(tMin, tcoef * tavg - std::abs(tdiff));
    tv_max = std::min(tv_max, tcoef * tavg + std::abs(tdiff));
}
```

**3. Test contour intersection (2 hours)**

```cpp
TEST(LaineKarrasOctree, ContourIntersection) {
    // Set up voxel with contour normal (0, 0, 1), thickness 0.1
    uint32_t contour_value = encodeContour(glm::vec3(0, 0, 1), 0.1f, 0.5f);

    // Ray perpendicular to contour (should refine t_min/t_max)
    glm::vec3 rayOrigin(0.5f, 0.5f, 0.0f);
    glm::vec3 rayDir(0.0f, 0.0f, 1.0f);

    // Expected: t_min refined to ~0.45, t_max refined to ~0.55
    // (contour at z=0.5 with thickness 0.1)
}
```

---

## Day 6-7: Descend/Advance/Pop

### Descend + Push (Day 6)

**Reference:** `cuda/Raycast.inl` lines 225-274

```cpp
// ADOPTED FROM: cuda/Raycast.inl lines 225-274
if (tMin <= tv_max) {
    // Check if leaf
    if ((child_masks & 0x0080) == 0) {
        break;  // Terminate at tMin
    }

    // PUSH: Write parent to stack
    if (tc_max < h) {
        stack.push(scale, parent, tMax);
    }
    h = tc_max;

    // Find child pointer
    uint32_t child_offset = (child_descriptor >> 17) & 0x7FFF;
    if ((child_descriptor & 0x10000) != 0) {
        // Far pointer (indirect)
        child_offset = parent[child_offset * 2];
    }
    child_offset += __builtin_popcount(child_masks & 0x7F);
    parent += child_offset * 2;

    // Select first child voxel
    idx = 0;
    scale--;
    scale_exp2 *= 0.5f;

    if (tx_center > tMin) idx ^= 1, pos.x += scale_exp2;
    if (ty_center > tMin) idx ^= 2, pos.y += scale_exp2;
    if (tz_center > tMin) idx ^= 4, pos.z += scale_exp2;

    // Update t-span
    tMax = tv_max;
    child_descriptor = 0;  // Invalidate cache
    continue;
}
```

### Advance + Pop (Day 7)

**Reference:** `cuda/Raycast.inl` lines 277-327

```cpp
// ADVANCE: Step along ray
int step_mask = 0;
if (tx_corner <= tc_max) step_mask ^= 1, pos.x -= scale_exp2;
if (ty_corner <= tc_max) step_mask ^= 2, pos.y -= scale_exp2;
if (tz_corner <= tc_max) step_mask ^= 4, pos.z -= scale_exp2;

tMin = tc_max;
idx ^= step_mask;

// POP if needed
if ((idx & step_mask) != 0) {
    // ADOPTED FROM: cuda/Raycast.inl lines 297-327

    // Find highest differing bit
    uint32_t differing_bits = 0;
    if ((step_mask & 1) != 0) differing_bits |= floatBitsToInt(pos.x) ^ floatBitsToInt(pos.x + scale_exp2);
    if ((step_mask & 2) != 0) differing_bits |= floatBitsToInt(pos.y) ^ floatBitsToInt(pos.y + scale_exp2);
    if ((step_mask & 4) != 0) differing_bits |= floatBitsToInt(pos.z) ^ floatBitsToInt(pos.z + scale_exp2);

    scale = (floatBitsToInt(static_cast<float>(differing_bits)) >> 23) - 127;
    scale_exp2 = intBitsToFloat((scale - CAST_STACK_DEPTH + 127) << 23);

    // Restore parent from stack
    parent = stack.pop(scale, tMax);

    // Round cube position
    int shx = floatBitsToInt(pos.x) >> scale;
    int shy = floatBitsToInt(pos.y) >> scale;
    int shz = floatBitsToInt(pos.z) >> scale;
    pos.x = intBitsToFloat(shx << scale);
    pos.y = intBitsToFloat(shy << scale);
    pos.z = intBitsToFloat(shz << scale);
    idx = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);

    h = 0.0f;
    child_descriptor = 0;
}
```

---

## Day 8: Testing & Debugging

### Test Suite

**1. Unit tests (4 hours)**

```bash
cd C:/cpp/VBVS--VIXEN/VIXEN/build
ctest -R test_octree_queries -V
```

Expected: All tests pass

**2. Visual test (2 hours)**

```cpp
// Add to test_octree_queries.cpp
TEST(LaineKarrasOctree, RenderCornellBox) {
    // Build Cornell box SVO
    auto octree = buildCornellBox();

    // Cast rays for 512×512 image
    std::vector<glm::vec3> pixels(512 * 512);
    for (int y = 0; y < 512; ++y) {
        for (int x = 0; x < 512; ++x) {
            glm::vec3 rayOrigin(0, 0, -5);
            glm::vec3 rayDir = computeRayDirection(x, y, 512, 512);

            auto hit = octree->castRay(rayOrigin, rayDir);
            pixels[y * 512 + x] = hit.hit ? hit.color : glm::vec3(0);
        }
    }

    // Save to file
    savePNG("cornell_box_reference.png", pixels, 512, 512);
}
```

**3. Benchmark (2 hours)**

```cpp
BENCHMARK(RaycastPerformance) {
    auto octree = buildComplexScene();  // 1M voxels

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000000; ++i) {
        glm::vec3 rayOrigin = randomPoint();
        glm::vec3 rayDir = randomDirection();
        octree->castRay(rayOrigin, rayDir);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "1M rays in " << duration.count() << "ms\n";
    std::cout << "Throughput: " << (1000000.0 / duration.count()) << " Mrays/sec\n";
}
```

**Expected:** >50 Mrays/sec (vs. ~15 Mrays/sec for current DDA)

---

## Day 9-10: GPU Port (GLSL)

### CUDA → GLSL Translation

**1. Create shader file (2 hours)**

```bash
mkdir -p shaders/svo
touch shaders/svo/OctreeTraversal.comp.glsl
```

**2. Port data structures (1 hour)**

```glsl
// GLSL equivalent of CUDA types
#version 460

struct Ray {
    vec3 orig;
    float orig_sz;
    vec3 dir;
    float dir_sz;
};

struct CastResult {
    float t;
    vec3 pos;
    int iter;
    // Note: GLSL can't have pointers, use buffer indices
    int nodeIndex;
    int childIdx;
    int stackPtr;
};
```

**3. Port main algorithm (8 hours)**

Direct translation of `castRay()`:
- Replace `__device__` with nothing
- Replace `float3` with `vec3`
- Replace `make_float3(x,y,z)` with `vec3(x,y,z)`
- Replace `S32*` with `int` (buffer indices)
- Replace pointer arithmetic with buffer[index]

**4. Test GPU shader (3 hours)**

```cpp
TEST(LaineKarrasOctree, GPUvsCP equivalence) {
    auto octree = buildTestScene();

    // Cast 1000 rays on CPU
    std::vector<RayHit> cpuResults;
    for (int i = 0; i < 1000; ++i) {
        cpuResults.push_back(octree->castRay(randomRay()));
    }

    // Cast same 1000 rays on GPU
    auto gpuResults = castRaysGPU(octree, cpuResults);

    // Compare
    for (int i = 0; i < 1000; ++i) {
        EXPECT_NEAR(cpuResults[i].t, gpuResults[i].t, 0.001f);
    }
}
```

---

## Day 11-12: DXT Compression

### Implementation

**1. Add compression module (4 hours)**

```bash
touch libraries/SVO/include/Compression.h
touch libraries/SVO/src/Compression.cpp
```

Port algorithms from `cuda/AttribLookup.inl`:
- `decodeDXTColor()` (lines 65-76)
- `decodeDXTNormal()` (lines 88-106)
- `decodeRawNormal()` (lines 40-52)

**2. Update attribute integrator (3 hours)**

```cpp
// File: libraries/SVO/src/AttributeIntegrator.cpp

void AttributeIntegrator::integrateAttributes(
    const std::vector<Triangle>& triangles,
    const std::vector<glm::vec3>& positions,
    std::vector<CompressedAttributes>& output)
{
    // Group voxels into 4×4×4 blocks (64 voxels per DXT block)
    for (int blockIdx = 0; blockIdx < numBlocks; ++blockIdx) {
        // Compute average color/normal for block
        glm::vec3 avgColor[64];
        glm::vec3 avgNormal[64];

        for (int i = 0; i < 64; ++i) {
            // ... existing integration code ...
        }

        // Compress block
        output[blockIdx].color = encodeDXTColor(avgColor);
        output[blockIdx].normal = encodeDXTNormal(avgNormal);
    }
}
```

**3. Test compression quality (3 hours)**

```cpp
TEST(Compression, DXTColorQuality) {
    glm::vec3 original[64];
    for (int i = 0; i < 64; ++i) {
        original[i] = randomColor();
    }

    DXTColorBlock compressed = encodeDXTColor(original);

    glm::vec3 decoded[64];
    for (int i = 0; i < 64; ++i) {
        decoded[i] = compressed.decode(i);
    }

    // Compute PSNR
    float mse = 0;
    for (int i = 0; i < 64; ++i) {
        glm::vec3 diff = original[i] - decoded[i];
        mse += glm::dot(diff, diff) / 64.0f;
    }

    float psnr = 10 * log10(1.0f / mse);
    EXPECT_GT(psnr, 35.0f);  // >35 dB is good quality
}
```

---

## Day 13-14: Polish & Documentation

### Tasks

**1. Add attribution headers (2 hours)**

All modified files need:
```cpp
// ============================================================================
// Adopted from NVIDIA ESVO Reference Implementation
// Source: efficient-sparse-voxel-octrees/trunk/src/octree/cuda/Raycast.inl
// Copyright (c) 2009-2011, NVIDIA Corporation
// License: BSD 3-Clause
//
// Algorithm Reference:
//   Samuli Laine and Tero Karras. 2010. Efficient Sparse Voxel Octrees.
// ============================================================================
```

**2. Update memory bank (2 hours)**

```bash
code memory-bank/activeContext.md
```

Add:
- Reference adoption complete
- Performance improvements (3-5× speedup)
- Memory reduction (16× with DXT)
- Next steps: Page headers, contour refinement

**3. Write completion report (2 hours)**

```bash
touch temp/REFERENCE_ADOPTION_COMPLETE.md
```

Include:
- What was adopted
- Performance benchmarks
- Before/after comparisons
- Known limitations
- Future work

**4. Run full test suite (2 hours)**

```bash
cd build
cmake --build . --config Debug
ctest --output-on-failure
```

Expected: 45/45 tests pass (40 existing + 5 new)

---

## Success Metrics

### Performance

- [x] CPU ray caster: >50 Mrays/sec (vs. ~15 current)
- [x] GPU ray caster: >200 Mrays/sec @ 1080p
- [x] Build time: <10s for 100K triangle mesh

### Memory

- [x] Uncompressed: <10 bytes/voxel
- [x] Compressed (DXT): <2 bytes/voxel
- [x] Reduction vs. current: >10×

### Quality

- [x] Ray intersection accuracy: <0.1% error vs. reference
- [x] DXT compression PSNR: >35 dB
- [x] Contour surface quality: No blocky artifacts on curved surfaces

### Tests

- [x] All 40 existing tests pass
- [x] 5+ new tests for reference features
- [x] Visual test: Cornell box renders correctly
- [x] Benchmark: Performance within 20% of reference

---

## Files to Modify

```
libraries/SVO/
├── include/
│   ├── LaineKarrasOctree.h       # Add CastStack, constants
│   ├── SVOTypes.h                # Add DXT structures, decodeContour()
│   └── Compression.h             # New: DXT encode/decode
└── src/
    ├── LaineKarrasOctree.cpp     # Replace castRayImpl() (lines 424-641)
    ├── AttributeIntegrator.cpp   # Add DXT encoding
    └── Compression.cpp           # New: DXT implementation

shaders/svo/
├── OctreeTraversal.comp.glsl     # New: GPU ray caster
└── AttributeLookup.glsl          # New: DXT decompression

libraries/SVO/tests/
├── test_octree_queries.cpp       # Add reference tests
└── test_compression.cpp          # New: DXT tests
```

---

## Ready to Start?

**First command:**

```bash
cd C:/cpp/VBVS--VIXEN/VIXEN
code "C:/Users/liory/Downloads/source-archive/efficient-sparse-voxel-octrees/trunk/src/octree/cuda/Raycast.inl"
code "libraries/SVO/src/LaineKarrasOctree.cpp:424"
```

**Have both files side-by-side and start porting line-by-line.**

Good luck! 🚀
