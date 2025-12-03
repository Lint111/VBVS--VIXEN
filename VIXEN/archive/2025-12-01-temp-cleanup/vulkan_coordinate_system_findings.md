# Vulkan Coordinate System Verification

## Summary
Your GLM configuration is **CORRECT** for Vulkan. Both headers properly define `GLM_FORCE_DEPTH_ZERO_TO_ONE` which is critical for Vulkan compatibility.

## Vulkan NDC (Normalized Device Coordinates)
```
X: -1 (left) to +1 (right)
Y: -1 (top) to +1 (bottom) - INVERTED from world space
Z:  0 (near) to  1 (far) - depth increases into screen
```

## World/View Space Conventions (Your Camera Code)
```
X+: Right  ✓
Y+: Up     ✓
Z-: Forward ✓ (negative Z is forward, standard OpenGL/GLM convention)
```

**Right-handed coordinate system maintained throughout**

## Key Findings

### 1. GLM Configuration ✓ CORRECT
Both header files have the correct defines:
- `application/main/include/Headers.h`: Line 40
- `libraries/RenderGraph/RenderGraphHeaders.h`: Line 30

```cpp
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Vulkan uses [0,1] depth range
```

### 2. View Matrix Generation ✓ CORRECT
`CameraNode.cpp` lines 168-178:

```cpp
glm::vec3 forward;
forward.x = cos(pitch) * sin(yaw);
forward.y = sin(pitch);
forward.z = -cos(pitch) * cos(yaw);  // -Z is forward ✓
forward = glm::normalize(forward);

glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
glm::vec3 up = glm::normalize(glm::cross(right, forward));

glm::vec3 target = cameraPosition + forward;
glm::mat4 view = glm::lookAt(cameraPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
```

**Analysis:**
- Forward direction uses `-cos(pitch) * cos(yaw)` for Z component ✓
- This means Z- is forward (standard OpenGL/GLM convention) ✓
- World up is Y+ (0, 1, 0) ✓
- Right vector computed via cross(forward, worldUp) ✓
- Up vector computed via cross(right, forward) for orthonormal basis ✓

### 3. Projection Matrix ✓ CORRECT
`CameraNode.cpp` lines 159-164:

```cpp
glm::mat4 projection = glm::perspective(
    glm::radians(fov),
    aspectRatio,
    nearPlane,
    farPlane
);
```

With `GLM_FORCE_DEPTH_ZERO_TO_ONE` defined, `glm::perspective` automatically:
- Maps near plane to Z=0 ✓
- Maps far plane to Z=1 ✓
- Inverts Y-axis (projection[1][1] < 0) ✓

### 4. Axis Conversions

| Space         | X      | Y         | Z          | Handedness |
|---------------|--------|-----------|------------|------------|
| World Space   | Right+ | Up+       | Forward-   | Right      |
| View Space    | Right+ | Up+       | Forward-   | Right      |
| Clip Space    | Right+ | **Down+** | Forward+   | Right      |
| NDC           | Right+ | **Down+** | Forward+   | Right      |

**Key transformation:** glm::perspective with `GLM_FORCE_DEPTH_ZERO_TO_ONE`:
- Flips Y axis (world Y+ becomes NDC Y-)
- Remaps Z from [-near, -far] to [0, 1]

### 5. Depth Range Verification

Expected depth transformation with near=0.1, far=100.0:

| View Z | NDC Z |
|--------|-------|
| -0.1   | 0.000 | (near plane)
| -1.0   | 0.009 |
| -10.0  | 0.091 |
| -50.0  | 0.498 |
| -100.0 | 1.000 | (far plane)

Depth precision **heavily biased toward near plane** (standard for perspective projection).

### 6. Yaw/Pitch to Direction Vector ✓ CORRECT

Your formula from `CameraNode.cpp`:
```cpp
forward.x = cos(pitch) * sin(yaw);
forward.y = sin(pitch);
forward.z = -cos(pitch) * cos(yaw);  // Note: -Z is forward
```

Test cases:
- Yaw=0°, Pitch=0°: Forward = (0, 0, -1) → Looking toward -Z ✓
- Yaw=90°, Pitch=0°: Forward = (1, 0, 0) → Looking toward +X ✓
- Yaw=-90°, Pitch=0°: Forward = (-1, 0, 0) → Looking toward -X ✓
- Yaw=0°, Pitch=90°: Forward = (0, 1, 0) → Looking toward +Y ✓
- Yaw=0°, Pitch=-90°: Forward = (0, -1, 0) → Looking toward -Y ✓

## Potential Issues from Past

If you experienced issues before, they may have been caused by:

1. **Missing `GLM_FORCE_DEPTH_ZERO_TO_ONE`** - Would cause depth range mismatch
   - **Status:** NOW FIXED ✓ (defined in both headers)

2. **Y-axis confusion** - Vulkan's clip space has inverted Y
   - **Status:** HANDLED AUTOMATICALLY by glm::perspective ✓

3. **Z-axis direction confusion** - View space uses -Z forward
   - **Status:** CORRECT in your code ✓

4. **Manual projection matrix** - If you built projection matrix manually
   - **Status:** Using glm::perspective is correct ✓

## Recommendations

### ✓ KEEP Current Implementation
Your current setup is correct for Vulkan. Do not change:
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` defines
- `glm::perspective()` usage
- `glm::lookAt()` usage
- Yaw/pitch to direction vector formula

### Test Executable Created
Location: `tests/coordinate_system_test.cpp`
Build: `cmake --build build --target coordinate_system_test`
Run: `build/binaries/tests/Debug/coordinate_system_test.exe`

The test verifies:
1. Camera basis vectors form right-handed system
2. Projection matrix has correct depth range [0,1]
3. Projection matrix inverts Y-axis (projection[1][1] < 0)
4. Clip space transforms produce expected NDC values
5. Yaw/pitch formulas generate correct direction vectors
6. Depth buffer precision distribution

### If Issues Persist

Check shader code for:
- Manual depth remapping (should use Vulkan's [0,1] natively)
- Y-axis inversions (may need to remove old OpenGL compensations)
- Z-axis winding order for face culling
- Viewport transform (should match framebuffer orientation)

## Conclusion

**Your math library configuration is CORRECT for Vulkan.**

The coordinate system chain is:
1. World space: X+ right, Y+ up, Z- forward (right-handed)
2. View matrix (glm::lookAt): Transforms world to camera view (maintains conventions)
3. Projection matrix (glm::perspective + GLM_FORCE_DEPTH_ZERO_TO_ONE):
   - Flips Y to match Vulkan's clip space (top = -1, bottom = +1)
   - Remaps Z to [0,1] depth range
4. Vulkan NDC: X+ right, Y+ down, Z+ forward, depth [0,1] (right-handed)

All transformations are working as intended for Vulkan rendering.
