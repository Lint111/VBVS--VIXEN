# GPU Shader Debug Examples

## Example 1: VoxelRayMarch.comp Debug Session

### Scenario
Ray marching shader produces visual artifacts at certain viewing angles.

### Step 1: Create Mirror Class

**Analyze shader:**
```glsl
// shaders/VoxelRayMarch.comp
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant) uniform PC {
    mat4 invViewProj;
    vec3 cameraPos;
    float maxDist;
};

layout(set = 0, binding = 0) readonly buffer Nodes { uint data[]; } nodes;
layout(set = 0, binding = 1, rgba8) writeonly uniform image2D outImage;

vec3 getRayDir(vec2 uv) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = invViewProj * clip;
    return normalize(world.xyz / world.w - cameraPos);
}

bool traverseOctree(vec3 ro, vec3 rd, out float t, out vec3 normal) {
    // Complex traversal logic...
    int stack[24];
    int stackPtr = 0;
    // ...
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(pixel) + 0.5) / vec2(imageSize(outImage));

    vec3 rd = getRayDir(uv);
    float t;
    vec3 normal;

    if (traverseOctree(cameraPos, rd, t, normal)) {
        vec3 color = normal * 0.5 + 0.5;
        imageStore(outImage, pixel, vec4(color, 1.0));
    } else {
        imageStore(outImage, pixel, vec4(0.1, 0.1, 0.15, 1.0));
    }
}
```

**Generate mirror:**
```cpp
// VoxelRayMarchMirror.h
#pragma once
#include <glm/glm.hpp>
#include <span>
#include <array>

class VoxelRayMarchMirror {
public:
    static constexpr glm::uvec3 LOCAL_SIZE{8, 8, 1};

    struct PushConstants {
        glm::mat4 invViewProj;
        glm::vec3 cameraPos;
        float maxDist;
    };

    struct HitResult {
        bool hit;
        float t;
        glm::vec3 normal;
    };

    struct DebugInfo {
        glm::vec3 rayOrigin;
        glm::vec3 rayDir;
        int stackMaxDepth;
        int nodeVisits;
        std::array<uint32_t, 24> stackTrace;
    };

    void setNodes(std::span<const uint32_t> data) { m_nodes = data; }
    void setOutputSize(uint32_t w, uint32_t h) { m_width = w; m_height = h; }

    glm::vec4 execute(glm::uvec3 globalId, const PushConstants& pc);

    const DebugInfo& getDebugInfo() const { return m_debug; }

private:
    glm::vec3 getRayDir(glm::vec2 uv, const PushConstants& pc);
    HitResult traverseOctree(glm::vec3 ro, glm::vec3 rd);

    std::span<const uint32_t> m_nodes;
    uint32_t m_width = 0, m_height = 0;
    PushConstants m_pc;
    mutable DebugInfo m_debug;
};
```

### Step 2: Write Targeted Tests

```cpp
// test_voxel_ray_march_mirror.cpp

TEST_F(VoxelRayMarchMirrorTest, ArtifactAngle_Issue) {
    // Reproduce the exact failing case
    loadTestScene("cornell_box.svo");

    // The artifact appears at this specific view angle
    pc.cameraPos = glm::vec3(0.5f, 0.5f, -2.0f);
    pc.invViewProj = glm::inverse(
        glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) *
        glm::lookAt(pc.cameraPos, glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0, 1, 0))
    );

    // Pixel where artifact appears
    auto result = shader.execute({412, 389, 0}, pc);
    auto debug = shader.getDebugInfo();

    // Inspect what's happening
    std::cout << "Ray dir: " << glm::to_string(debug.rayDir) << "\n";
    std::cout << "Stack depth: " << debug.stackMaxDepth << "\n";
    std::cout << "Nodes visited: " << debug.nodeVisits << "\n";

    // The bug: stack overflow causes wrong result
    EXPECT_LT(debug.stackMaxDepth, 24); // Stack should never exceed 24
}
```

### Step 3: Debug with Breakpoints

Set breakpoint in `traverseOctree()`, run test, step through:

```
Iteration 1: node=0x80000001, stackPtr=1 ✓
Iteration 2: node=0x80000003, stackPtr=2 ✓
...
Iteration 23: node=0x80000017, stackPtr=23 ✓
Iteration 24: node=0x80000019, stackPtr=24 ← STACK FULL
Iteration 25: node=0x8000001B, stackPtr=25 ← OVERFLOW!
```

**Root cause found:** Missing bounds check on stack push.

### Step 4: Fix in C++ Mirror

```cpp
// Before:
stack[stackPtr++] = childIndex;

// After:
if (stackPtr < 24) {
    stack[stackPtr++] = childIndex;
} else {
    // Stack overflow - return miss to avoid corruption
    return HitResult{false, 0.0f, glm::vec3(0)};
}
```

### Step 5: Verify Fix

```
$ ./test_voxel_ray_march_mirror --gtest_filter=*ArtifactAngle*
PASSED
```

### Step 6: Apply to GLSL

```glsl
// Before:
stack[stackPtr++] = childIndex;

// After:
if (stackPtr < 24) {
    stack[stackPtr++] = childIndex;
} else {
    imageStore(outImage, pixel, vec4(1, 0, 1, 1)); // Magenta = error
    return;
}
```

---

## Example 2: ESVO Traversal Precision Bug

### Scenario
ESVO ray casting fails for very small voxels (deep octree levels).

### Create Focused Test

```cpp
TEST_F(ESVOMirrorTest, DeepTraversal_Precision) {
    // Create octree with maximum depth
    auto svo = createTestOctree(/*depth=*/23);
    shader.setNodes(svo.data());

    // Ray that should hit tiny voxel at level 23
    glm::vec3 target = glm::vec3(0.5000001f, 0.5000001f, 0.5000001f);
    pc.cameraPos = target - glm::vec3(0, 0, 1);

    auto result = shader.execute({512, 512, 0}, pc);
    auto debug = shader.getDebugInfo();

    EXPECT_TRUE(result.hit);
    EXPECT_EQ(debug.deepestLevel, 23);

    // Check for precision issues
    EXPECT_FALSE(std::isnan(result.t));
    EXPECT_FALSE(std::isinf(result.t));
    EXPECT_GT(result.t, 0.0f);
}
```

### Debug Output

```
deepestLevel: 18 (expected 23)
ray stuck at level 18, t=0.999999523
tMax computation: (0.5 - 0.5000001) / rayDir.x = -inf
```

**Root cause:** Division by near-zero ray direction component.

### Fix

```cpp
// C++ Mirror fix:
float invDirX = (glm::abs(rd.x) > 1e-8f) ? (1.0f / rd.x) : 1e8f * glm::sign(rd.x);

// Apply same to GLSL:
float invDirX = (abs(rd.x) > 1e-8) ? (1.0 / rd.x) : 1e8 * sign(rd.x);
```

---

## Example 3: DDA Voxel Traversal Off-by-One

### Scenario
Voxel grid traversal misses voxels at grid boundaries.

### Visual Debug Test

```cpp
TEST_F(DDAMirrorTest, BoundaryTraversal_Visual) {
    // Create 4x4x4 grid, all voxels filled
    auto grid = createFilledGrid(4, 4, 4);
    shader.setVoxelGrid(grid);

    // Collect all visited voxel coordinates
    std::set<glm::ivec3> visited;

    // Ray along diagonal
    glm::vec3 start(-0.5f, -0.5f, -0.5f);
    glm::vec3 end(4.5f, 4.5f, 4.5f);

    shader.traverseRay(start, end, [&](glm::ivec3 coord) {
        visited.insert(coord);
    });

    // Should visit exactly the diagonal voxels
    EXPECT_TRUE(visited.count({0, 0, 0}));
    EXPECT_TRUE(visited.count({1, 1, 1}));
    EXPECT_TRUE(visited.count({2, 2, 2}));
    EXPECT_TRUE(visited.count({3, 3, 3}));

    // Print visual grid
    printTraversalGrid(visited, 4);
}

void printTraversalGrid(const std::set<glm::ivec3>& visited, int size) {
    for (int z = 0; z < size; z++) {
        std::cout << "Z=" << z << ":\n";
        for (int y = size-1; y >= 0; y--) {
            for (int x = 0; x < size; x++) {
                bool hit = visited.count({x, y, z});
                std::cout << (hit ? "█" : "·");
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
}
```

**Output showing bug:**
```
Z=0:
█···
····
····
····

Z=1:
····   <- Missing (1,1,1)!
·█··
····
····
```

### Fix

```cpp
// Off-by-one in step calculation
// Before:
int stepX = (rd.x > 0) ? 1 : -1;
float tMaxX = ((stepX > 0 ? floor(pos.x) + 1 : floor(pos.x)) - pos.x) / rd.x;

// After:
int stepX = (rd.x >= 0) ? 1 : -1;  // >= instead of >
float tMaxX = ((stepX > 0 ? floor(pos.x) + 1.0f : ceil(pos.x) - 1.0f) - pos.x) / rd.x;
```

---

## Example 4: Parallel Comparison Test

After fixing CPU mirror, validate GPU output matches:

```cpp
TEST_F(VoxelRayMarchMirrorTest, GPU_CPU_Match) {
    loadTestScene("cornell_box.svo");
    setupCamera(/* standard test view */);

    // Run GPU shader
    auto gpuImage = renderWithGPU(1024, 1024, pc);

    // Run CPU mirror
    auto cpuImage = renderWithCPU(1024, 1024, pc);

    // Compare
    int mismatches = 0;
    float maxDiff = 0.0f;

    for (int y = 0; y < 1024; y++) {
        for (int x = 0; x < 1024; x++) {
            glm::vec4 gpu = gpuImage[y * 1024 + x];
            glm::vec4 cpu = cpuImage[y * 1024 + x];

            float diff = glm::length(gpu - cpu);
            maxDiff = glm::max(maxDiff, diff);

            if (diff > 0.01f) {
                mismatches++;
                if (mismatches < 10) {
                    std::cout << "Mismatch at (" << x << "," << y << "): "
                              << "GPU=" << glm::to_string(gpu) << " "
                              << "CPU=" << glm::to_string(cpu) << "\n";
                }
            }
        }
    }

    EXPECT_EQ(mismatches, 0) << "Max diff: " << maxDiff;
}
```
