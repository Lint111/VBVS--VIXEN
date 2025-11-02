# Hybrid RTX Surface-Skin Architecture

**Concept**: Use RTX hardware for initial surface intersection, then switch to ray marching for material-specific traversal
**Purpose**: Combine strengths of hardware RT (fast surface finding) with flexibility of ray marching (complex materials)
**Innovation**: "Surface skin buffer" - sparse representation of world boundaries and material transitions
**Date**: November 2, 2025

---

## Core Concept

### The Problem with Pure Approaches

**Pure Ray Marching**:
- ✅ Flexible (handles any material: opaque, transparent, refractive, volumetric)
- ❌ Slow initial intersection (must test every voxel along ray)
- ❌ Wasted work in empty space

**Pure Hardware RT**:
- ✅ Fast intersection (BVH acceleration, dedicated hardware)
- ✅ Efficient empty space skipping
- ❌ Limited material flexibility (opaque geometry assumption)
- ❌ Complex shading requires multiple ray bounces (expensive)

---

## Hybrid Solution: RTX as "Stepping Stone"

**Pipeline**:
```
1. Dirty Voxel Data
   ↓ (CPU pre-process)
2. Extract Surface Skin Buffer
   ↓ (analyze neighbors)
3. Generate Virtual Geometry
   ↓ (planes/quads from normals)
4. Build RTX BLAS/TLAS
   ↓ (hardware acceleration structure)
5. RTX Initial Ray Trace
   ↓ (find first surface hit)
6. Material-Specific Ray Marching
   ↓ (opaque = done, transparent = continue, reflective = bounce)
7. Final Shading
```

---

## Step 1: Surface Skin Buffer Extraction

### Definition: Surface Voxel

A voxel is in the "surface skin" if:

1. **Has empty neighbors** → Boundary with air
2. **Neighbors have different material IDs** → Material transition
3. **At least one neighbor is non-opaque** → Affects light transport

```cpp
struct VoxelMaterial {
    uint8_t id;           // Material type (0 = air, 1 = stone, 2 = glass, etc.)
    bool isOpaque;        // Light blocking
    bool isReflective;    // Mirror-like
    bool isRefractive;    // Glass-like (transmits light)
    bool isVolumetric;    // Fog/smoke (participates in scattering)
};

bool IsSurfaceVoxel(const VoxelGrid& grid, glm::ivec3 pos) {
    VoxelMaterial center = grid.GetMaterial(pos);

    if (center.id == 0) return false;  // Air is never surface

    // Check 6-connected neighbors (±X, ±Y, ±Z)
    glm::ivec3 neighbors[6] = {
        {pos.x - 1, pos.y, pos.z}, {pos.x + 1, pos.y, pos.z},
        {pos.x, pos.y - 1, pos.z}, {pos.x, pos.y + 1, pos.z},
        {pos.x, pos.y, pos.z - 1}, {pos.x, pos.y, pos.z + 1}
    };

    for (const auto& nPos : neighbors) {
        VoxelMaterial neighbor = grid.GetMaterial(nPos);

        // Condition 1: Empty neighbor (air boundary)
        if (neighbor.id == 0) return true;

        // Condition 2: Material transition
        if (neighbor.id != center.id) {
            // Condition 3: At least one is non-opaque
            if (!center.isOpaque || !neighbor.isOpaque) {
                return true;
            }
        }
    }

    return false;  // Completely interior voxel (all neighbors identical opaque)
}
```

### Algorithm: Extract Surface Skin

```cpp
struct SurfaceSkinBuffer {
    std::vector<glm::ivec3> positions;    // World-space voxel positions
    std::vector<glm::u8vec3> colors;      // Voxel colors (for shading)
    std::vector<uint8_t> materialIDs;     // Material types
    std::vector<glm::vec3> normals;       // Calculated normals
};

SurfaceSkinBuffer ExtractSurfaceSkin(const VoxelGrid& grid) {
    SurfaceSkinBuffer skin;
    uint32_t res = grid.GetResolution();

    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t y = 0; y < res; ++y) {
            for (uint32_t z = 0; z < res; ++z) {
                glm::ivec3 pos(x, y, z);

                if (IsSurfaceVoxel(grid, pos)) {
                    skin.positions.push_back(pos);
                    skin.colors.push_back(grid.GetColor(pos));
                    skin.materialIDs.push_back(grid.GetMaterial(pos).id);
                    skin.normals.push_back(CalculateVoxelNormal(grid, pos));
                }
            }
        }
    }

    return skin;
}
```

### Compression Ratio

**Example**: Urban scene (512³, 90% density)
- **Full grid**: 512³ = 134 million voxels
- **Interior voxels**: 90% - 10% surface = 80% → 107 million voxels
- **Surface skin**: 20% → **27 million voxels**
- **Reduction**: **5× smaller** (107M → 27M)

**Benefit**: Only process surface voxels with RTX, ignore interior.

---

## Step 2: Normal Calculation

**Purpose**: Determine orientation of virtual geometry (planes/quads)

```cpp
glm::vec3 CalculateVoxelNormal(const VoxelGrid& grid, glm::ivec3 pos) {
    // Gradient-based normal (density difference in ±X, ±Y, ±Z)
    glm::vec3 gradient(0.0f);

    auto getDensity = [&](glm::ivec3 p) -> float {
        return grid.IsSolid(p) ? 1.0f : 0.0f;
    };

    gradient.x = getDensity({pos.x + 1, pos.y, pos.z}) - getDensity({pos.x - 1, pos.y, pos.z});
    gradient.y = getDensity({pos.x, pos.y + 1, pos.z}) - getDensity({pos.x, pos.y - 1, pos.z});
    gradient.z = getDensity({pos.x, pos.y, pos.z + 1}) - getDensity({pos.x, pos.y, pos.z - 1});

    if (glm::length(gradient) < 0.001f) {
        // Fallback: Use dominant face direction
        return CalculateDominantFaceNormal(grid, pos);
    }

    return glm::normalize(gradient);
}

glm::vec3 CalculateDominantFaceNormal(const VoxelGrid& grid, glm::ivec3 pos) {
    // Check which faces are exposed to air
    glm::ivec3 neighbors[6] = { /* ... */ };
    glm::vec3 faceNormals[6] = {
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
    };

    for (int i = 0; i < 6; ++i) {
        if (grid.GetMaterial(neighbors[i]).id == 0) {  // Air neighbor
            return faceNormals[i];
        }
    }

    return {0, 1, 0};  // Default: up
}
```

---

## Step 3: Virtual Geometry Generation

### Option A: Single Quad per Voxel (Simple)

**Geometry**: One axis-aligned quad facing the dominant normal

```cpp
struct VirtualQuad {
    glm::vec3 position;  // Center of quad
    glm::vec3 normal;    // Quad orientation
    glm::vec2 size;      // Quad dimensions (typically 1×1 for voxel)
    uint32_t materialID; // Index into material buffer
};

std::vector<VirtualQuad> GenerateQuads(const SurfaceSkinBuffer& skin) {
    std::vector<VirtualQuad> quads;

    for (size_t i = 0; i < skin.positions.size(); ++i) {
        quads.push_back({
            .position = glm::vec3(skin.positions[i]) + glm::vec3(0.5f),  // Voxel center
            .normal = skin.normals[i],
            .size = {1.0f, 1.0f},
            .materialID = skin.materialIDs[i]
        });
    }

    return quads;
}
```

**Triangle Conversion** (for RTX BLAS):
```cpp
std::vector<Triangle> QuadsToTriangles(const std::vector<VirtualQuad>& quads) {
    std::vector<Triangle> triangles;

    for (const auto& quad : quads) {
        // Calculate quad corners based on position + normal + size
        glm::vec3 right = glm::normalize(glm::cross(quad.normal, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::cross(right, quad.normal);

        glm::vec3 c0 = quad.position - right * 0.5f - up * 0.5f;
        glm::vec3 c1 = quad.position + right * 0.5f - up * 0.5f;
        glm::vec3 c2 = quad.position + right * 0.5f + up * 0.5f;
        glm::vec3 c3 = quad.position - right * 0.5f + up * 0.5f;

        // Two triangles per quad
        triangles.push_back({c0, c1, c2});  // Triangle 1
        triangles.push_back({c0, c2, c3});  // Triangle 2
    }

    return triangles;
}
```

**BLAS Size**:
- Surface voxels: 27 million (urban scene example)
- Triangles: 27M × 2 = **54 million triangles**
- Vertices: 54M × 3 × 12 bytes = **1.9 GB** (vertex buffer)
- **Still feasible** for modern GPUs (RTX 3080 has 10 GB VRAM)

---

### Option B: Greedy Meshing (Optimized)

**Goal**: Merge adjacent coplanar quads → Fewer triangles

```cpp
std::vector<VirtualQuad> GreedyMesh(const SurfaceSkinBuffer& skin) {
    // Group voxels by normal direction and material
    std::map<std::pair<glm::ivec3, uint8_t>, std::vector<glm::ivec3>> groups;

    for (size_t i = 0; i < skin.positions.size(); ++i) {
        glm::ivec3 quantizedNormal = QuantizeNormal(skin.normals[i]);  // E.g., {0,1,0} for up
        groups[{quantizedNormal, skin.materialIDs[i]}].push_back(skin.positions[i]);
    }

    std::vector<VirtualQuad> quads;

    // For each group, merge into largest possible rectangles
    for (auto& [key, voxels] : groups) {
        auto [normal, materialID] = key;

        // Greedy rectangle merging (2D problem in plane perpendicular to normal)
        auto mergedQuads = MergeRectangles(voxels, normal);

        for (const auto& quad : mergedQuads) {
            quads.push_back({
                .position = quad.center,
                .normal = glm::vec3(normal),
                .size = quad.size,
                .materialID = materialID
            });
        }
    }

    return quads;
}
```

**Compression**: 27M voxels → **~5M quads** (greedy meshing reduces by 5-10×)
- Triangles: 5M × 2 = **10 million triangles**
- **10× reduction** vs naïve approach

---

## Step 4: RTX Acceleration Structure

### BLAS Creation (Triangle Geometry)

```cpp
VkAccelerationStructureKHR CreateSurfaceSkinBLAS(
    VkDevice device,
    VkCommandBuffer cmd,
    const std::vector<Triangle>& triangles)
{
    // Upload triangle vertex data
    VkBuffer vertexBuffer = CreateAndUploadBuffer(device, triangles.data(),
        triangles.size() * sizeof(Triangle),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

    // Geometry description (TRIANGLES, not AABBs like voxel BLAS)
    VkAccelerationStructureGeometryTrianglesDataKHR triangleData = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData = {GetBufferDeviceAddress(device, vertexBuffer)},
        .vertexStride = sizeof(glm::vec3),
        .maxVertex = static_cast<uint32_t>(triangles.size() * 3),
        .indexType = VK_INDEX_TYPE_NONE_KHR  // Non-indexed (each 3 vertices = triangle)
    };

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,  // Standard triangles
        .geometry = {.triangles = triangleData},
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR  // Opaque surface (for now)
    };

    // Build BLAS (same as HardwareRTDesign.md, but with triangle geometry)
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry
    };

    // ... (query sizes, allocate, build, barrier) - see HardwareRTDesign.md

    return blas;
}
```

**Key Difference**: Using **triangle geometry** (standard RTX), not custom AABB intersection.

**Advantage**: Faster than AABB (no custom intersection shader), native hardware acceleration.

---

## Step 5: Hybrid Ray Tracing Shader

### Ray Generation Shader (.rgen)

```glsl
#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0, rgba8) uniform image2D outputImage;
layout(set = 0, binding = 1) uniform accelerationStructureEXT surfaceSkinTLAS;
layout(set = 0, binding = 2) uniform sampler3D fullVoxelGrid;  // Original voxel data
layout(set = 0, binding = 3) buffer MaterialBuffer { Material materials[]; };

// Ray payload: Initial hit data
layout(location = 0) rayPayloadEXT HitPayload {
    vec3 hitPos;
    vec3 hitNormal;
    uint materialID;
    bool hit;
} payload;

void main() {
    vec3 rayOrigin = camera.pos;
    vec3 rayDir = CalculateRayDir();

    // **STEP 1: RTX Trace to Surface Skin**
    payload.hit = false;
    traceRayEXT(
        surfaceSkinTLAS,
        gl_RayFlagsOpaqueEXT,
        0xFF,    // Cull mask
        0,       // SBT offset
        0,       // SBT stride
        0,       // Miss index
        rayOrigin,
        0.001,   // tMin
        rayDir,
        1000.0,  // tMax
        0        // Payload location
    );

    vec4 finalColor;

    if (!payload.hit) {
        // Ray missed all geometry
        finalColor = vec4(BACKGROUND_COLOR, 1.0);
    } else {
        // **STEP 2: Material-Specific Continuation**
        Material mat = materials[payload.materialID];

        if (mat.isOpaque) {
            // Opaque surface - done, just shade
            finalColor = ShadeOpaque(payload.hitPos, payload.hitNormal, mat);
        } else if (mat.isReflective) {
            // Reflective - bounce ray
            vec3 reflectDir = reflect(rayDir, payload.hitNormal);
            finalColor = TraceReflection(payload.hitPos, reflectDir);
        } else if (mat.isRefractive) {
            // Refractive (glass) - march through volume
            finalColor = MarchRefractiveVolume(payload.hitPos, rayDir, mat);
        } else if (mat.isVolumetric) {
            // Volumetric (fog/smoke) - march with scattering
            finalColor = MarchVolumetricMedia(payload.hitPos, rayDir, mat);
        }
    }

    imageStore(outputImage, ivec2(gl_LaunchIDEXT.xy), finalColor);
}
```

---

### Closest Hit Shader (.rchit)

```glsl
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT HitPayload payload;

// Triangle attributes
hitAttributeEXT vec2 barycentrics;

// Material data (per-triangle)
layout(set = 0, binding = 4) buffer TriangleMaterials { uint materialIDs[]; };

void main() {
    // Calculate hit position
    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // Get triangle geometry normal
    vec3 normal = CalculateTriangleNormal();  // From vertex data or primitive ID

    // Set payload
    payload.hitPos = hitPos;
    payload.hitNormal = normal;
    payload.materialID = materialIDs[gl_PrimitiveID];  // Material from triangle metadata
    payload.hit = true;
}
```

---

### Material-Specific Continuation Functions

#### Opaque Material (Simple Shading)

```glsl
vec4 ShadeOpaque(vec3 pos, vec3 normal, Material mat) {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 albedo = mat.color.rgb;
    return vec4(albedo * (vec3(0.2) + diffuse * 0.8), 1.0);
}
```

#### Refractive Material (Glass - Volume Marching)

```glsl
vec4 MarchRefractiveVolume(vec3 entryPos, vec3 rayDir, Material mat) {
    // Refract ray at surface boundary
    float eta = 1.0 / mat.indexOfRefraction;  // Air to glass
    vec3 refractedDir = refract(rayDir, payload.hitNormal, eta);

    // March through volume until exiting
    vec3 pos = entryPos + refractedDir * 0.01;  // Small offset
    float t = 0.0;

    while (t < MAX_DISTANCE) {
        // Sample voxel grid to check if still inside glass
        vec3 uvw = pos / float(gridResolution);
        vec4 voxel = texture(fullVoxelGrid, uvw);
        uint voxelMat = uint(voxel.a * 255.0);  // Material ID from alpha

        if (voxelMat != mat.id) {
            // Exited glass, refract back to air
            vec3 exitNormal = CalculateVoxelNormal(pos);
            vec3 exitDir = refract(refractedDir, -exitNormal, mat.indexOfRefraction);

            // Trace secondary ray
            return TraceSecondaryRay(pos, exitDir);
        }

        // Absorption (Beer's law)
        vec3 absorption = exp(-mat.absorptionCoeff * t);

        pos += refractedDir * STEP_SIZE;
        t += STEP_SIZE;
    }

    return vec4(mat.color.rgb * absorption, 1.0);
}
```

#### Volumetric Material (Fog - Participating Media)

```glsl
vec4 MarchVolumetricMedia(vec3 startPos, vec3 rayDir, Material mat) {
    vec3 pos = startPos;
    vec3 accumulatedColor = vec3(0.0);
    float accumulatedAlpha = 0.0;
    float t = 0.0;

    while (t < MAX_DISTANCE && accumulatedAlpha < 0.99) {
        // Sample density from voxel grid
        vec3 uvw = pos / float(gridResolution);
        float density = texture(fullVoxelGrid, uvw).r;

        // In-scattering (simplified single-scattering)
        vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
        float scatter = max(dot(-rayDir, lightDir), 0.0);  // Phase function

        vec3 inScatter = mat.color.rgb * density * scatter * mat.scatterCoeff;
        float extinction = density * (mat.absorptionCoeff + mat.scatterCoeff);

        // Accumulate contribution
        float transmittance = exp(-extinction * STEP_SIZE);
        accumulatedColor += inScatter * (1.0 - accumulatedAlpha) * STEP_SIZE;
        accumulatedAlpha += (1.0 - transmittance) * (1.0 - accumulatedAlpha);

        pos += rayDir * STEP_SIZE;
        t += STEP_SIZE;
    }

    // Blend with background
    vec4 background = TraceSecondaryRay(pos, rayDir);
    return vec4(accumulatedColor + background.rgb * (1.0 - accumulatedAlpha), 1.0);
}
```

---

## Step 6: Material System

### Material Definition

```cpp
struct Material {
    glm::vec3 color;              // Base color
    uint8_t id;                   // Material type ID

    // Flags
    bool isOpaque;
    bool isReflective;
    bool isRefractive;
    bool isVolumetric;

    // Refractive properties
    float indexOfRefraction;      // 1.0 = air, 1.5 = glass, 2.4 = diamond
    float absorptionCoeff;        // Attenuation (Beer's law)

    // Volumetric properties
    float scatterCoeff;           // In-scattering strength
    float density;                // Fog density
};

// Example materials
Material CreateGlass() {
    return {
        .color = {0.9, 0.9, 1.0},
        .id = 2,
        .isOpaque = false,
        .isRefractive = true,
        .indexOfRefraction = 1.5,
        .absorptionCoeff = 0.1
    };
}

Material CreateFog() {
    return {
        .color = {0.7, 0.7, 0.8},
        .id = 3,
        .isOpaque = false,
        .isVolumetric = true,
        .scatterCoeff = 0.5,
        .density = 0.3
    };
}
```

---

## Performance Analysis

### Advantages

**vs Pure Ray Marching**:
- **RTX initial intersection**: ~10× faster than DDA for first hit (hardware BVH)
- **Empty space skipping**: Free (BVH handles automatically)
- **Reduced traversal steps**: Only march *after* hitting surface (not entire ray)

**vs Pure Hardware RT**:
- **Material flexibility**: Handles glass, fog, reflections (not just opaque)
- **Volume rendering**: Participating media (impossible with pure RT)
- **Hybrid approach**: Best of both worlds

### Expected Performance

| Scene (Complexity) | Pure Ray March | Pure HW RT | Hybrid (This) | Speedup |
|--------------------|----------------|------------|---------------|---------|
| Cornell (10%, opaque) | 16 ms | **4 ms** | 5 ms | **3.2×** vs march |
| Cave (50%, mixed) | 28 ms | N/A (transparent) | **12 ms** | **2.3×** vs march |
| Urban (90%, glass) | 45 ms | N/A (refractive) | **18 ms** | **2.5×** vs march |

**Hypothesis**: Hybrid saves 50-70% of ray marching time by using RTX for initial hit.

---

## Implementation Roadmap

### Phase 1: Surface Skin Extraction (CPU)
- Implement `IsSurfaceVoxel()` and `ExtractSurfaceSkin()`
- Add material system (opaque/reflective/refractive flags)
- Generate virtual quads with normals

### Phase 2: Greedy Meshing Optimization
- Implement rectangle merging
- Reduce triangle count (5-10× compression)

### Phase 3: RTX Integration
- Build BLAS with triangle geometry
- Write hybrid ray tracing shaders (rgen + rchit)
- Implement opaque material shading

### Phase 4: Advanced Materials
- Add refractive material marching (glass)
- Add volumetric media (fog/smoke)
- Add reflective materials (mirrors)

### Phase 5: Benchmarking
- Compare with pure ray marching (Phase G/J)
- Compare with pure hardware RT (Phase K)
- Measure bandwidth, frame time, traversal steps

**Estimated Time**: 5-7 weeks (advanced hybrid pipeline)

---

## Research Value

**Hybrid as 4th/5th Pipeline Variant**:
1. Compute shader ray marching
2. Fragment shader ray marching
3. Hardware RT (pure, AABBs)
4. **Hybrid RTX + ray marching** ← This approach
5. GigaVoxels streaming (optional)

**Comparative Questions**:
- Does RTX overhead (BLAS build, SBT) offset marching savings?
- What's the optimal surface skin density for performance?
- How does greedy meshing compression affect BVH traversal?
- Does material complexity (refractive vs opaque) change the trade-off?

---

## References

**Techniques**:
- **Surface extraction**: Marching cubes, dual contouring (adapted for voxels)
- **Greedy meshing**: Minecraft-style quad merging
- **Hybrid rendering**: NVIDIA OptiX mixed-mode rendering

**VIXEN Documents**:
- `HardwareRTDesign.md` - Pure RTX acceleration structures
- `VoxelRayMarch-Integration-Guide.md` - Pure ray marching baseline
- `BibliographyOptimizationTechniques.md` - Traversal optimizations

**Research Papers** (from bibliography):
- [5] Voetter - Vulkan volumetric ray tracing (hybrid approaches)
- [16] Derin - BlockWalk (surface coherence)

---

## Next Steps

1. **Implement surface skin extraction** (CPU pre-process)
2. **Add material system** (opaque/refractive/volumetric flags)
3. **Generate virtual geometry** (quads → triangles)
4. **Build triangle BLAS** (RTX acceleration)
5. **Write hybrid shaders** (RTX first hit → material-specific continuation)
6. **Benchmark vs baselines** (pure march, pure RT)

**Deliverable**: Hybrid pipeline + material flexibility + performance analysis

**Innovation**: First known voxel renderer to combine RTX hardware with flexible material ray marching
