# Hardware Ray Tracing Pipeline Design - Vulkan RTX

**Phase**: K (Hardware Ray Tracing Pipeline) Preparation
**Purpose**: Third pipeline variant using VK_KHR_ray_tracing_pipeline
**Status**: Research and design complete
**Date**: November 2, 2025

---

## Overview

This document specifies the **hardware-accelerated ray tracing pipeline** for voxel rendering using Vulkan's RTX extensions. This is the third pipeline variant for comparative analysis.

### Pipeline Comparison

| Pipeline | Phase | API | Hardware | Complexity |
|----------|-------|-----|----------|------------|
| **Compute Shader** | G | Compute | Shader cores | Low |
| **Fragment Shader** | J | Graphics | Rasterizer + Shader cores | Low |
| **Hardware RT** | K | Ray Tracing | RT cores + Shader cores | **High** |
| **Hybrid** | L | Mixed | All units | Very High |

**Research Value**: Hardware RT uses dedicated ray tracing cores (RTX GPUs) → Different performance characteristics, bandwidth patterns, and scalability.

---

## Required Vulkan Extensions

### Core Extensions

```cpp
// Device extensions (must be supported)
const std::vector<const char*> RTX_EXTENSIONS = {
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,     // Acceleration structures (BLAS/TLAS)
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,       // Ray tracing pipeline
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,   // Async acceleration structure builds
    VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,           // Pipeline libraries
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,      // Device addresses for BLAS/TLAS
    VK_KHR_SPIRV_1_4_EXTENSION_NAME                   // SPIRV 1.4 (required for RT shaders)
};
```

### Extension Availability Check

```cpp
bool CheckRTXSupport(VkPhysicalDevice physicalDevice) {
    // Check extension support
    uint32_t extCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availableExts.data());

    std::set<std::string> requiredExts(RTX_EXTENSIONS.begin(), RTX_EXTENSIONS.end());
    for (const auto& ext : availableExts) {
        requiredExts.erase(ext.extensionName);
    }

    if (!requiredExts.empty()) {
        std::cerr << "Missing RTX extensions:" << std::endl;
        for (const auto& ext : requiredExts) {
            std::cerr << "  - " << ext << std::endl;
        }
        return false;
    }

    // Check feature support
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &rtFeatures
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &asFeatures
    };

    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    return asFeatures.accelerationStructure && rtFeatures.rayTracingPipeline;
}
```

---

## Acceleration Structure Design for Voxels

### Challenge: Voxels as Ray Tracing Geometry

**Problem**: Hardware RT is designed for triangle meshes, voxels are axis-aligned bounding boxes (AABBs).

**Solutions**:
1. **Triangle representation**: Convert each voxel to 12 triangles (cube) → Massive geometry overhead
2. **AABB geometry**: Use `VK_GEOMETRY_TYPE_AABBS_KHR` → Custom intersection shader
3. **Hybrid**: Octree BLAS with AABB leaves → Best balance

**Chosen Approach**: **AABB Geometry with Custom Intersection Shader** (Option 2)

---

### Bottom-Level Acceleration Structure (BLAS)

**Representation**: Each voxel = one AABB primitive

#### BLAS Geometry Data

```cpp
struct VoxelAABB {
    glm::vec3 min;  // Voxel corner (e.g., (x, y, z))
    glm::vec3 max;  // Opposite corner (e.g., (x+1, y+1, z+1))
};

// For 128³ grid with 50% density → 1,048,576 AABBs
std::vector<VoxelAABB> BuildAABBGeometry(const VoxelGrid& grid) {
    std::vector<VoxelAABB> aabbs;
    uint32_t res = grid.GetResolution();

    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t y = 0; y < res; ++y) {
            for (uint32_t z = 0; z < res; ++z) {
                if (!grid.IsEmpty(x, y, z)) {
                    aabbs.push_back({
                        .min = glm::vec3(x, y, z),
                        .max = glm::vec3(x + 1, y + 1, z + 1)
                    });
                }
            }
        }
    }

    return aabbs;
}
```

#### BLAS Creation

```cpp
VkAccelerationStructureKHR CreateVoxelBLAS(
    VkDevice device,
    VkCommandBuffer cmd,
    const std::vector<VoxelAABB>& aabbs,
    const std::vector<glm::u8vec4>& voxelColors)
{
    // 1. Upload AABB data to GPU buffer
    VkBuffer aabbBuffer = CreateAndUploadBuffer(device, aabbs.data(),
        aabbs.size() * sizeof(VoxelAABB), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

    // 2. Upload voxel color data (for intersection shader)
    VkBuffer colorBuffer = CreateAndUploadBuffer(device, voxelColors.data(),
        voxelColors.size() * sizeof(glm::u8vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    // 3. Geometry description
    VkAccelerationStructureGeometryAabbsDataKHR aabbData = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
        .data = {GetBufferDeviceAddress(device, aabbBuffer)},  // Device address
        .stride = sizeof(VoxelAABB)
    };

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_AABBS_KHR,
        .geometry = {.aabbs = aabbData},
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR  // Voxels are opaque (no transparency)
    };

    // 4. Build info
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,  // Optimize for traversal
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry
    };

    // 5. Query build sizes
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    uint32_t primitiveCount = static_cast<uint32_t>(aabbs.size());
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &primitiveCount, &sizeInfo);

    // 6. Create BLAS buffer
    VkBuffer blasBuffer = CreateBuffer(device, sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

    // 7. Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = blasBuffer,
        .offset = 0,
        .size = sizeInfo.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };

    VkAccelerationStructureKHR blas;
    vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &blas);

    // 8. Build BLAS (on GPU)
    VkBuffer scratchBuffer = CreateBuffer(device, sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    buildInfo.dstAccelerationStructure = blas;
    buildInfo.scratchData = {GetBufferDeviceAddress(device, scratchBuffer)};

    VkAccelerationStructureBuildRangeInfoKHR buildRange = {
        .primitiveCount = primitiveCount,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };

    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRange = &buildRange;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pBuildRange);

    // 9. Barrier (wait for build completion)
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    return blas;
}
```

---

### Top-Level Acceleration Structure (TLAS)

**Purpose**: Transforms and instances BLAS (for dynamic scenes, multiple voxel grids)

**For Single Static Grid**: TLAS contains one instance of BLAS with identity transform

```cpp
VkAccelerationStructureKHR CreateVoxelTLAS(
    VkDevice device,
    VkCommandBuffer cmd,
    VkAccelerationStructureKHR blas)
{
    // 1. Instance description
    VkAccelerationStructureInstanceKHR instance = {
        .transform = {  // Identity matrix (row-major)
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f
        },
        .instanceCustomIndex = 0,  // User data (unused for single grid)
        .mask = 0xFF,              // Ray mask (all rays hit this instance)
        .instanceShaderBindingTableRecordOffset = 0,
        .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference = GetAccelerationStructureDeviceAddress(device, blas)
    };

    // 2. Upload instance to GPU
    VkBuffer instanceBuffer = CreateAndUploadBuffer(device, &instance, sizeof(instance),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

    // 3. Geometry description
    VkAccelerationStructureGeometryInstancesDataKHR instancesData = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
        .data = {GetBufferDeviceAddress(device, instanceBuffer)}
    };

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = {.instances = instancesData}
    };

    // 4. Build TLAS (similar to BLAS, but type = TOP_LEVEL)
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry
    };

    // 5-9: Same as BLAS creation (query sizes, create buffer, build, barrier)
    // ... (omitted for brevity)

    return tlas;
}
```

---

## Ray Tracing Shader Stages

### Shader Stage Overview

| Shader | Purpose | Invocation | Output |
|--------|---------|------------|--------|
| **Ray Generation** (.rgen) | Generate primary rays | Per-pixel | Trace rays, write color |
| **Closest Hit** (.rchit) | Shading at ray-geometry hit | Per ray hit | Final color contribution |
| **Miss** (.rmiss) | Background when ray misses | Per ray miss | Background color |
| **Intersection** (.rint) | Custom AABB intersection test | Per AABB traversal | Hit or miss |
| **Any Hit** (.rahit) | Transparency/early termination | Per potential hit | Accept or reject hit |

**For Voxels**: rgen + rint + rchit + rmiss (no any-hit, voxels are opaque)

---

### 1. Ray Generation Shader (rgen)

```glsl
#version 460
#extension GL_EXT_ray_tracing : require

// Output image
layout(set = 0, binding = 0, rgba8) uniform image2D outputImage;

// Acceleration structure
layout(set = 0, binding = 1) uniform accelerationStructureEXT topLevelAS;

// Camera data
layout(set = 0, binding = 2) uniform CameraData {
    mat4 invProjection;
    mat4 invView;
    vec3 cameraPos;
    uint gridResolution;
} camera;

// Ray payload (data passed to hit/miss shaders)
layout(location = 0) rayPayloadEXT vec3 hitColor;

void main() {
    ivec2 pixelCoords = ivec2(gl_LaunchIDEXT.xy);
    ivec2 screenSize = ivec2(gl_LaunchSizeEXT.xy);

    // Generate ray (same as compute/fragment shaders)
    vec2 ndc = (vec2(pixelCoords) + vec2(0.5)) / vec2(screenSize) * 2.0 - 1.0;

    vec4 clipNear = vec4(ndc, -1.0, 1.0);
    vec4 clipFar = vec4(ndc, 1.0, 1.0);

    vec4 viewNear = camera.invProjection * clipNear;
    vec4 viewFar = camera.invProjection * clipFar;

    viewNear /= viewNear.w;
    viewFar /= viewFar.w;

    vec4 worldNear = camera.invView * viewNear;
    vec4 worldFar = camera.invView * viewFar;

    vec3 rayOrigin = camera.cameraPos;
    vec3 rayDir = normalize(worldFar.xyz - worldNear.xyz);

    // Trace ray
    float tMin = 0.001;
    float tMax = 1000.0;
    uint rayFlags = gl_RayFlagsOpaqueEXT;  // Voxels are opaque
    uint cullMask = 0xFF;
    uint sbtRecordOffset = 0;
    uint sbtRecordStride = 0;
    uint missIndex = 0;

    traceRayEXT(
        topLevelAS,       // Acceleration structure
        rayFlags,         // Ray flags
        cullMask,         // Cull mask (0xFF = hit all instances)
        sbtRecordOffset,  // SBT record offset
        sbtRecordStride,  // SBT record stride
        missIndex,        // Miss shader index
        rayOrigin,        // Ray origin
        tMin,             // Min distance
        rayDir,           // Ray direction
        tMax,             // Max distance
        0                 // Payload location
    );

    // Write result (hitColor set by closest hit or miss shader)
    imageStore(outputImage, pixelCoords, vec4(hitColor, 1.0));
}
```

---

### 2. Intersection Shader (rint)

**Purpose**: Test if ray intersects voxel AABB

```glsl
#version 460
#extension GL_EXT_ray_tracing : require

// Hit attribute (passed to closest hit shader)
hitAttributeEXT vec2 hitAttrib;  // Unused for voxels (no barycentric coords)

void main() {
    // AABB bounds are in object space (voxel = [0,1]³)
    vec3 aabbMin = vec3(0.0);
    vec3 aabbMax = vec3(1.0);

    // Ray in object space
    vec3 rayOrigin = gl_ObjectRayOriginEXT;
    vec3 rayDir = gl_ObjectRayDirectionEXT;

    // Ray-AABB intersection (slab method)
    vec3 invDir = 1.0 / rayDir;
    vec3 t0 = (aabbMin - rayOrigin) * invDir;
    vec3 t1 = (aabbMax - rayOrigin) * invDir;

    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);

    float tNear = max(max(tMin.x, tMin.y), tMin.z);
    float tFar = min(min(tMax.x, tMax.y), tMax.z);

    // Report hit if intersection exists within ray range
    if (tNear <= tFar && tFar >= gl_RayTminEXT && tNear <= gl_RayTmaxEXT) {
        reportIntersectionEXT(tNear, 0);  // Hit kind = 0 (custom)
    }
}
```

---

### 3. Closest Hit Shader (rchit)

**Purpose**: Shade voxel at intersection point

```glsl
#version 460
#extension GL_EXT_ray_tracing : require

// Ray payload (output)
layout(location = 0) rayPayloadInEXT vec3 hitColor;

// Voxel color buffer (indexed by primitive ID)
layout(set = 0, binding = 3) buffer VoxelColors {
    uvec4 colors[];  // RGBA8 packed as uint
} voxelColors;

// Calculate voxel normal from hit position
vec3 calculateVoxelNormal(vec3 hitPos) {
    vec3 localPos = fract(hitPos);  // Position within voxel [0,1]
    vec3 absLocalPos = abs(localPos - 0.5);

    float maxComponent = max(max(absLocalPos.x, absLocalPos.y), absLocalPos.z);

    if (absLocalPos.x == maxComponent) return vec3(sign(localPos.x - 0.5), 0.0, 0.0);
    if (absLocalPos.y == maxComponent) return vec3(0.0, sign(localPos.y - 0.5), 0.0);
    return vec3(0.0, 0.0, sign(localPos.z - 0.5));
}

void main() {
    // Get voxel color from buffer (indexed by primitive ID)
    uint primitiveID = gl_PrimitiveID;
    uvec4 packedColor = voxelColors.colors[primitiveID];
    vec3 albedo = vec3(packedColor.rgb) / 255.0;

    // Calculate hit position
    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // Calculate normal
    vec3 normal = calculateVoxelNormal(hitPos);

    // Simple diffuse shading (same as compute/fragment)
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 ambient = vec3(0.2);

    hitColor = albedo * (ambient + diffuse * 0.8);
}
```

---

### 4. Miss Shader (rmiss)

**Purpose**: Return background color when ray misses all geometry

```glsl
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitColor;

void main() {
    // Background color (same as compute/fragment)
    hitColor = vec3(0.1, 0.1, 0.15);
}
```

---

## Ray Tracing Pipeline Creation

### Shader Module Loading

```cpp
VkShaderModule LoadRTShader(const char* path, VkShaderStageFlagBits stage) {
    // Load SPIRV bytecode
    std::vector<uint32_t> code = ReadSPIRV(path);

    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data()
    };

    VkShaderModule module;
    vkCreateShaderModule(device, &createInfo, nullptr, &module);
    return module;
}
```

### Pipeline Stages

```cpp
std::vector<VkPipelineShaderStageCreateInfo> rtStages = {
    // Ray generation
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        .module = LoadRTShader("raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR),
        .pName = "main"
    },
    // Miss
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
        .module = LoadRTShader("miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR),
        .pName = "main"
    },
    // Closest hit
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .module = LoadRTShader("closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
        .pName = "main"
    },
    // Intersection
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
        .module = LoadRTShader("intersection.rint.spv", VK_SHADER_STAGE_INTERSECTION_BIT_KHR),
        .pName = "main"
    }
};
```

### Shader Groups

```cpp
std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups = {
    // Group 0: Ray generation
    {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = 0,  // Index into rtStages
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR
    },
    // Group 1: Miss
    {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = 1,
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR
    },
    // Group 2: Hit (closest hit + intersection)
    {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR,  // AABB geometry
        .generalShader = VK_SHADER_UNUSED_KHR,
        .closestHitShader = 2,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = 3
    }
};
```

### Pipeline Creation

```cpp
VkRayTracingPipelineCreateInfoKHR pipelineInfo = {
    .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
    .stageCount = static_cast<uint32_t>(rtStages.size()),
    .pStages = rtStages.data(),
    .groupCount = static_cast<uint32_t>(shaderGroups.size()),
    .pGroups = shaderGroups.data(),
    .maxPipelineRayRecursionDepth = 1,  // No secondary rays (primary only)
    .layout = pipelineLayout
};

VkPipeline rtPipeline;
vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rtPipeline);
```

---

## Shader Binding Table (SBT)

**Purpose**: Maps shader groups to GPU memory for dynamic shader invocation

```cpp
struct ShaderBindingTable {
    VkBuffer buffer;
    VkDeviceMemory memory;

    VkStridedDeviceAddressRegionKHR raygenRegion;
    VkStridedDeviceAddressRegionKHR missRegion;
    VkStridedDeviceAddressRegionKHR hitRegion;
    VkStridedDeviceAddressRegionKHR callableRegion;  // Unused for voxels
};

ShaderBindingTable CreateSBT(VkDevice device, VkPipeline rtPipeline,
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtProps)
{
    uint32_t groupCount = 3;  // raygen, miss, hit
    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
    uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;

    // Get shader group handles
    std::vector<uint8_t> handles(groupCount * handleSize);
    vkGetRayTracingShaderGroupHandlesKHR(device, rtPipeline, 0, groupCount, handles.size(), handles.data());

    // Calculate sizes
    uint32_t raygenSize = AlignUp(handleSize, baseAlignment);
    uint32_t missSize = AlignUp(handleSize, baseAlignment);
    uint32_t hitSize = AlignUp(handleSize, baseAlignment);
    uint32_t totalSize = raygenSize + missSize + hitSize;

    // Create SBT buffer
    VkBuffer sbtBuffer = CreateBuffer(device, totalSize,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    // Map and copy handles
    uint8_t* mapped;
    vkMapMemory(device, sbtMemory, 0, totalSize, 0, (void**)&mapped);

    memcpy(mapped, handles.data() + 0 * handleSize, handleSize);  // Raygen
    memcpy(mapped + raygenSize, handles.data() + 1 * handleSize, handleSize);  // Miss
    memcpy(mapped + raygenSize + missSize, handles.data() + 2 * handleSize, handleSize);  // Hit

    vkUnmapMemory(device, sbtMemory);

    // Create address regions
    VkDeviceAddress sbtAddress = GetBufferDeviceAddress(device, sbtBuffer);

    return {
        .buffer = sbtBuffer,
        .memory = sbtMemory,
        .raygenRegion = {sbtAddress, raygenSize, raygenSize},
        .missRegion = {sbtAddress + raygenSize, missSize, missSize},
        .hitRegion = {sbtAddress + raygenSize + missSize, hitSize, hitSize},
        .callableRegion = {0, 0, 0}
    };
}
```

---

## Command Buffer Recording

```cpp
// Bind pipeline
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipeline);

// Bind descriptor sets (TLAS, output image, camera, voxel colors)
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout,
    0, 1, &descriptorSet, 0, nullptr);

// Trace rays
vkCmdTraceRaysKHR(cmd,
    &sbt.raygenRegion,
    &sbt.missRegion,
    &sbt.hitRegion,
    &sbt.callableRegion,
    screenWidth,   // Launch width
    screenHeight,  // Launch height
    1              // Launch depth
);
```

---

## Performance Considerations

### Advantages (Hardware RT)

1. **Dedicated RT cores**: NVIDIA RTX / AMD RDNA2+ have specialized hardware for BVH traversal
2. **Hardware BVH traversal**: Faster than software DDA (especially for sparse scenes)
3. **Automatic LOD**: Acceleration structure can skip empty regions efficiently
4. **Scalability**: Handles millions of primitives (1M+ voxels feasible)

### Disadvantages (Hardware RT)

1. **BLAS build overhead**: Must rebuild BLAS when voxels change (dynamic scenes)
2. **Memory overhead**: BLAS/TLAS storage (~2-3× voxel data size)
3. **AABB intersection cost**: Custom intersection shader adds overhead vs triangles
4. **API complexity**: More code than compute/fragment shaders

### Expected Performance

**Hypothesis**: Hardware RT will be **faster for sparse scenes** (10-50% density) due to efficient empty space skipping, but **comparable or slower for dense scenes** (90% density) due to AABB intersection overhead.

**Test**: Phase M will validate this hypothesis across all scene/resolution combinations.

---

## Integration with RenderGraph (Phase K)

### HardwareRTNode

**Inputs**:
- `CAMERA_BUFFER` (VkBuffer*) - Camera uniform data
- `VOXEL_GRID` (VoxelGrid*) - CPU voxel data (for BLAS construction)
- `COMMAND_BUFFER` (VkCommandBuffer*) - Recording target

**Outputs**:
- `OUTPUT_IMAGE` (VkImage*) - Rendered result
- `TLAS` (VkAccelerationStructureKHR*) - Built acceleration structure
- `COMPLETION_SEMAPHORE` (VkSemaphore*) - GPU sync primitive

**Lifecycle**:
1. **Compile**: Build BLAS/TLAS, create RT pipeline, generate SBT
2. **Execute**: Bind pipeline, trace rays
3. **Cleanup**: Destroy BLAS/TLAS, pipeline, SBT

---

## Validation Checklist

- [x] RTX extension availability check ✅
- [x] BLAS/TLAS design for AABB geometry ✅
- [x] Intersection shader for voxel AABBs ✅
- [x] Ray generation shader (screen-space ray generation) ✅
- [x] Closest hit shader (diffuse shading) ✅
- [x] Miss shader (background color) ✅
- [x] Shader binding table creation ✅
- [ ] Integration with RenderGraph (Phase K implementation)
- [ ] Performance profiling (Phase I + K)
- [ ] Comparison with compute/fragment results (Phase M)

---

## References

**Vulkan Specifications**:
- `VK_KHR_acceleration_structure` extension spec
- `VK_KHR_ray_tracing_pipeline` extension spec
- Vulkan Ray Tracing Tutorial (Nvidia, Sascha Willems)

**Shader Files** (to be created in Phase K):
- `Shaders/VoxelRT.rgen` - Ray generation
- `Shaders/VoxelRT.rint` - AABB intersection
- `Shaders/VoxelRT.rchit` - Closest hit
- `Shaders/VoxelRT.rmiss` - Miss

**Documentation**:
- `VoxelRayTracingResearch-TechnicalRoadmap.md` - Full research plan
- `PerformanceProfilerDesign.md` - Profiling system

**Research Papers** (from bibliography):
- [5] Voetter - Vulkan volumetric ray tracing (hardware RT discussion)
- [8] Novák - AABB-based voxel ray tracing

---

## Next Steps (Phase K Implementation)

After Phase G (Compute) and Phase J (Fragment) completion:

1. **Write RT shaders** (rgen, rint, rchit, rmiss)
2. **Implement HardwareRTNode** class
3. **Validate on simple test scene** (Cornell Box)
4. **Run benchmark suite** (45 configurations)
5. **Compare with compute/fragment** (analyze performance delta)

**Estimated Time**: 4-5 weeks (Phase K in roadmap - most complex pipeline)

**Deliverable**: Hardware RT pipeline + performance comparison document
