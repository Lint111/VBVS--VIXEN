---
name: gpu-shader-debug
description: GPU shader debugging via CPU-side mirror classes. Use when debugging GLSL compute/fragment/vertex shaders, ray marching issues, voxel traversal bugs, or any GPU-side rendering problems. Creates 1:1 C++ mirror of shader for step-through debugging.
allowed-tools: Bash, Read, Edit, Write, Grep, Glob, Task
---

# GPU Shader Debug Skill

Debug GLSL shaders by creating equivalent C++ CPU-side implementations that mirror the shader 1:1, enabling proper step-through debugging, unit testing, and systematic bug fixing.

## Core Principle

**GPU debugging is hard. CPU debugging is easy.**

This skill bridges that gap by:
1. Creating a C++ class that exactly mirrors the GLSL shader
2. Writing unit tests against the C++ version
3. Using the red-yellow-green cycle to fix bugs
4. Applying fixes back to the GLSL shader

## Workflow Overview

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ GLSL Shader │ ──> │  C++ Mirror │ ──> │ Unit Tests  │
│ (buggy)     │     │  (1:1 copy) │     │ (debuggable)│
└─────────────┘     └─────────────┘     └─────────────┘
       │                   │                   │
       │                   ▼                   │
       │           ┌─────────────┐             │
       │           │ Debug & Fix │ <───────────┘
       │           │ (CPU-side)  │
       │           └─────────────┘
       │                   │
       ▼                   ▼
┌─────────────┐     ┌─────────────┐
│ GLSL Fixed  │ <── │ Apply Same  │
│             │     │ Fix to GLSL │
└─────────────┘     └─────────────┘
```

## Phase 1: Shader Analysis

### 1.1 Identify Shader Structure

Read the GLSL shader and extract:

```glsl
// Example: VoxelRayMarch.comp

// Uniforms/Push constants -> class members
layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    vec3 cameraPos;
    float maxDistance;
} pc;

// SSBOs -> reference parameters or pointers
layout(set = 0, binding = 0) buffer VoxelData {
    uint nodes[];
} voxelBuffer;

// Functions -> methods
vec3 rayMarch(vec3 origin, vec3 direction) { ... }
bool intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) { ... }

// Main -> execute() method
void main() { ... }
```

### 1.2 Map GLSL Types to C++

| GLSL Type | C++ Type | Header |
|-----------|----------|--------|
| `vec2` | `glm::vec2` | `<glm/glm.hpp>` |
| `vec3` | `glm::vec3` | `<glm/glm.hpp>` |
| `vec4` | `glm::vec4` | `<glm/glm.hpp>` |
| `mat4` | `glm::mat4` | `<glm/glm.hpp>` |
| `ivec3` | `glm::ivec3` | `<glm/glm.hpp>` |
| `uvec3` | `glm::uvec3` | `<glm/glm.hpp>` |
| `uint` | `uint32_t` | `<cstdint>` |
| `int` | `int32_t` | `<cstdint>` |
| `float` | `float` | - |
| `bool` | `bool` | - |
| `sampler2D` | Custom texture class | Project-specific |

### 1.3 Map GLSL Functions to C++

| GLSL | C++ (GLM) |
|------|-----------|
| `mix(a, b, t)` | `glm::mix(a, b, t)` |
| `clamp(x, lo, hi)` | `glm::clamp(x, lo, hi)` |
| `dot(a, b)` | `glm::dot(a, b)` |
| `normalize(v)` | `glm::normalize(v)` |
| `length(v)` | `glm::length(v)` |
| `floor(x)` | `glm::floor(x)` |
| `ceil(x)` | `glm::ceil(x)` |
| `min(a, b)` | `glm::min(a, b)` |
| `max(a, b)` | `glm::max(a, b)` |
| `step(edge, x)` | `glm::step(edge, x)` |
| `smoothstep(a,b,x)` | `glm::smoothstep(a, b, x)` |
| `fract(x)` | `glm::fract(x)` |
| `mod(x, y)` | `glm::mod(x, y)` |
| `abs(x)` | `glm::abs(x)` |
| `sign(x)` | `glm::sign(x)` |
| `pow(x, y)` | `glm::pow(x, y)` |
| `exp(x)` | `glm::exp(x)` |
| `log(x)` | `glm::log(x)` |
| `sqrt(x)` | `glm::sqrt(x)` |
| `inversesqrt(x)` | `glm::inversesqrt(x)` |
| `cross(a, b)` | `glm::cross(a, b)` |
| `reflect(I, N)` | `glm::reflect(I, N)` |
| `refract(I,N,eta)` | `glm::refract(I, N, eta)` |
| `bitfieldExtract` | Custom implementation |
| `floatBitsToUint` | `glm::floatBitsToUint` |
| `uintBitsToFloat` | `glm::uintBitsToFloat` |

## Phase 2: Create C++ Mirror Class

### 2.1 File Structure

```
libraries/ShaderMirrors/
├── include/
│   └── ShaderMirrors/
│       └── VoxelRayMarchMirror.h
├── src/
│   └── VoxelRayMarchMirror.cpp
└── tests/
    └── test_voxel_ray_march_mirror.cpp
```

### 2.2 Mirror Class Template

```cpp
// VoxelRayMarchMirror.h
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <span>

/**
 * @brief CPU-side mirror of VoxelRayMarch.comp
 * @shader shaders/VoxelRayMarch.comp
 * @sync-hash <SHA256 of shader at creation time>
 *
 * IMPORTANT: This class must stay 1:1 synchronized with the GLSL shader.
 * Any fix applied here MUST be applied to the shader as well.
 */
class VoxelRayMarchMirror {
public:
    // === Push Constants (mirrors layout(push_constant)) ===
    struct PushConstants {
        glm::mat4 invViewProj;
        glm::vec3 cameraPos;
        float maxDistance;
    };

    // === Buffer Bindings ===
    // Set via setBuffers() before execute()
    void setVoxelData(std::span<const uint32_t> nodes);
    void setOutputImage(std::span<glm::vec4> output, uint32_t width, uint32_t height);

    // === Execution ===
    // Mirrors void main() - call per "invocation"
    glm::vec4 execute(glm::uvec3 globalInvocationId, const PushConstants& pc);

    // === Debug Accessors ===
    // Expose intermediate values for debugging
    struct DebugInfo {
        glm::vec3 rayOrigin;
        glm::vec3 rayDirection;
        int iterationCount;
        float hitDistance;
        bool hit;
    };
    DebugInfo getLastDebugInfo() const { return m_debugInfo; }

private:
    // === Shader Functions (mirrors GLSL functions) ===
    // Keep signatures identical to GLSL

    glm::vec3 rayMarch(glm::vec3 origin, glm::vec3 direction);
    bool intersectAABB(glm::vec3 ro, glm::vec3 rd, glm::vec3 bmin, glm::vec3 bmax,
                       float& tmin, float& tmax);
    uint32_t getNode(uint32_t index);
    glm::vec3 computeNormal(glm::vec3 hitPos);

    // === State ===
    std::span<const uint32_t> m_voxelNodes;
    std::span<glm::vec4> m_output;
    uint32_t m_outputWidth = 0;
    uint32_t m_outputHeight = 0;

    // Push constants stored for access in helper functions
    PushConstants m_pc;

    // Debug state
    mutable DebugInfo m_debugInfo;
};
```

### 2.3 Implementation Rules

```cpp
// VoxelRayMarchMirror.cpp

// RULE 1: Function bodies must match GLSL exactly
// Copy-paste from shader, then translate syntax

// GLSL:
// vec3 rayMarch(vec3 origin, vec3 direction) {
//     float t = 0.0;
//     for (int i = 0; i < 256; i++) {
//         vec3 p = origin + direction * t;
//         if (hitVoxel(p)) return p;
//         t += 0.01;
//     }
//     return vec3(-1.0);
// }

// C++ Mirror:
glm::vec3 VoxelRayMarchMirror::rayMarch(glm::vec3 origin, glm::vec3 direction) {
    float t = 0.0f;
    for (int i = 0; i < 256; i++) {
        glm::vec3 p = origin + direction * t;
        if (hitVoxel(p)) return p;
        t += 0.01f;
    }
    return glm::vec3(-1.0f);
}

// RULE 2: Add debug hooks without changing logic
glm::vec3 VoxelRayMarchMirror::rayMarch(glm::vec3 origin, glm::vec3 direction) {
    m_debugInfo.rayOrigin = origin;      // DEBUG HOOK
    m_debugInfo.rayDirection = direction; // DEBUG HOOK
    m_debugInfo.iterationCount = 0;       // DEBUG HOOK

    float t = 0.0f;
    for (int i = 0; i < 256; i++) {
        m_debugInfo.iterationCount++;     // DEBUG HOOK
        glm::vec3 p = origin + direction * t;
        if (hitVoxel(p)) {
            m_debugInfo.hit = true;       // DEBUG HOOK
            m_debugInfo.hitDistance = t;  // DEBUG HOOK
            return p;
        }
        t += 0.01f;
    }
    m_debugInfo.hit = false;              // DEBUG HOOK
    return glm::vec3(-1.0f);
}
```

## Phase 3: Create Test Scenarios

### 3.1 Test File Template

```cpp
// test_voxel_ray_march_mirror.cpp

#include <gtest/gtest.h>
#include "ShaderMirrors/VoxelRayMarchMirror.h"

/**
 * @file test_voxel_ray_march_mirror.cpp
 * @shader shaders/VoxelRayMarch.comp
 * @purpose Debug GPU shader via CPU-side testing
 */

class VoxelRayMarchMirrorTest : public ::testing::Test {
protected:
    VoxelRayMarchMirror shader;
    VoxelRayMarchMirror::PushConstants pc;
    std::vector<uint32_t> voxelData;

    void SetUp() override {
        // Setup default push constants
        pc.invViewProj = glm::mat4(1.0f);
        pc.cameraPos = glm::vec3(0, 0, -5);
        pc.maxDistance = 100.0f;
    }

    // Helper: Create simple test voxel data
    void setupSingleVoxel(glm::ivec3 pos) {
        // Create minimal SVO with single voxel
        voxelData = createTestSVO(pos);
        shader.setVoxelData(voxelData);
    }
};

// === Basic Functionality Tests ===

TEST_F(VoxelRayMarchMirrorTest, RayHitsCenteredVoxel) {
    setupSingleVoxel({0, 0, 0});

    // Ray from camera looking at origin
    auto result = shader.execute({512, 512, 0}, pc);
    auto debug = shader.getLastDebugInfo();

    EXPECT_TRUE(debug.hit);
    EXPECT_GT(debug.hitDistance, 0.0f);
    EXPECT_LT(debug.hitDistance, pc.maxDistance);
}

TEST_F(VoxelRayMarchMirrorTest, RayMissesEmptySpace) {
    setupSingleVoxel({100, 100, 100}); // Voxel far off to side

    // Ray from camera looking at origin (should miss)
    auto result = shader.execute({512, 512, 0}, pc);
    auto debug = shader.getLastDebugInfo();

    EXPECT_FALSE(debug.hit);
}

// === Edge Case Tests ===

TEST_F(VoxelRayMarchMirrorTest, RayAtVoxelBoundary) {
    setupSingleVoxel({0, 0, 0});

    // Ray that grazes voxel edge
    pc.cameraPos = glm::vec3(0.5f, 0.5f, -5.0f);
    auto result = shader.execute({512, 512, 0}, pc);
    auto debug = shader.getLastDebugInfo();

    // Document expected behavior at boundaries
    // This test catches boundary precision issues
}

TEST_F(VoxelRayMarchMirrorTest, MaxIterationReached) {
    // Setup scenario where ray never hits but reaches max iterations
    voxelData.clear(); // Empty scene
    shader.setVoxelData(voxelData);

    auto result = shader.execute({512, 512, 0}, pc);
    auto debug = shader.getLastDebugInfo();

    EXPECT_FALSE(debug.hit);
    EXPECT_EQ(debug.iterationCount, 256); // Max iterations
}

// === Regression Tests ===

TEST_F(VoxelRayMarchMirrorTest, Issue42_NegativeCoordinates) {
    // Regression test for issue #42: negative coordinates caused overflow
    setupSingleVoxel({-1, -1, -1});

    pc.cameraPos = glm::vec3(-1, -1, -5);
    auto result = shader.execute({512, 512, 0}, pc);
    auto debug = shader.getLastDebugInfo();

    EXPECT_TRUE(debug.hit);
    // Should not crash or produce NaN
    EXPECT_FALSE(std::isnan(debug.hitDistance));
}
```

## Phase 4: Debug-Fix-Apply Cycle

### 4.1 Red Phase (Test Fails)

```
$ ./build/libraries/ShaderMirrors/tests/Debug/test_voxel_ray_march_mirror.exe \
    --gtest_filter=*NegativeCoordinates

FAILED: Expected hit=true, got hit=false
Debug info:
  rayOrigin: (-1, -1, -5)
  rayDirection: (0, 0, 1)
  iterationCount: 256 (max reached)
  hit: false
```

### 4.2 Debug in IDE

- Set breakpoint in `VoxelRayMarchMirror::rayMarch()`
- Step through with test input
- Identify bug: unsigned comparison fails for negative coords

### 4.3 Fix in C++ Mirror

```cpp
// Before (buggy):
uint32_t index = uint32_t(pos.x) + uint32_t(pos.y) * stride;

// After (fixed):
int32_t ix = int32_t(glm::floor(pos.x));
int32_t iy = int32_t(glm::floor(pos.y));
if (ix < 0 || iy < 0) return 0; // Handle negative
uint32_t index = uint32_t(ix) + uint32_t(iy) * stride;
```

### 4.4 Verify Green

```
$ ./build/libraries/ShaderMirrors/tests/Debug/test_voxel_ray_march_mirror.exe \
    --gtest_filter=*NegativeCoordinates

PASSED
```

### 4.5 Apply to GLSL

```glsl
// Before (buggy):
uint index = uint(pos.x) + uint(pos.y) * stride;

// After (fixed - same logic as C++):
int ix = int(floor(pos.x));
int iy = int(floor(pos.y));
if (ix < 0 || iy < 0) return 0u; // Handle negative
uint index = uint(ix) + uint(iy) * stride;
```

### 4.6 Update Sync Hash

```cpp
// In VoxelRayMarchMirror.h, update:
// @sync-hash <new SHA256 of modified shader>
```

## Phase 5: Validation

### 5.1 Visual Comparison

After applying fix to GLSL:
1. Run application with GPU shader
2. Compare output to CPU mirror output
3. Results should match (within float precision)

### 5.2 Automated Comparison Test

```cpp
TEST_F(VoxelRayMarchMirrorTest, MatchesGPUOutput) {
    // Load known-good GPU output
    auto gpuOutput = loadReferenceImage("gpu_output.png");

    // Run CPU mirror on same input
    auto cpuOutput = runFullFrame(shader, pc, 1024, 1024);

    // Compare
    float maxDiff = compareImages(gpuOutput, cpuOutput);
    EXPECT_LT(maxDiff, 0.001f); // Allow tiny float differences
}
```

## Sync Maintenance

### Keeping Mirror in Sync

When GLSL shader changes:

1. **Check sync hash**
   ```bash
   sha256sum shaders/VoxelRayMarch.comp
   # Compare with @sync-hash in mirror header
   ```

2. **If different, update mirror:**
   - Diff the shader changes
   - Apply same changes to C++ mirror
   - Update tests if needed
   - Update sync hash

### Automation Helper

```bash
# scripts/check_shader_sync.py
# Verifies all shader mirrors are in sync with their GLSL sources
python scripts/check_shader_sync.py --check
python scripts/check_shader_sync.py --update-hashes
```

## When to Use This Skill

- Debugging compute shader algorithms (ray marching, SVO traversal)
- Investigating visual artifacts with unknown cause
- Testing shader edge cases systematically
- Validating shader math/logic before GPU deployment
- Performance comparison (CPU baseline vs GPU)
- When printf debugging in shaders isn't sufficient

## Integration with Other Skills

- Use **red-green-test-cycle** for the test fixing workflow
- Use **bug-hunter** for initial issue investigation
- Use **coding-partner** for implementation guidance
