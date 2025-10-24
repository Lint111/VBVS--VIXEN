# VulkanShader Usage Examples

This document provides comprehensive examples of using the VulkanShader library in various scenarios.

## Table of Contents
1. [Basic Usage](#basic-usage)
2. [Shader Variants](#shader-variants)
3. [Compute Shaders](#compute-shaders)
4. [Advanced Pipeline Stages](#advanced-pipeline-stages)
5. [Hot Reloading](#hot-reloading)
6. [Shader Includes](#shader-includes)
7. [Caching](#caching)
8. [Custom Entry Points](#custom-entry-points)
9. [Error Handling](#error-handling)
10. [Performance Optimization](#performance-optimization)

---

## Basic Usage

### Simple Vertex + Fragment Shader

```cpp
#include "VulkanShader.h"

void CreateBasicShader() {
    VulkanShader shader;

    // Load shaders from files
    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/basic.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/basic.frag")
          .Build();

    // Use in pipeline
    if (shader.IsInitialized()) {
        // Create pipeline with shader...
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.stageCount = shader.GetStageCount();
        pipelineInfo.pStages = shader.GetStages();
        // ...
    }

    // Cleanup
    shader.DestroyShader();
}
```

### Loading from String

```cpp
void CreateShaderFromString() {
    std::string vertexCode = R"(
        #version 450

        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inColor;
        layout(location = 0) out vec3 fragColor;

        void main() {
            gl_Position = vec4(inPosition, 1.0);
            fragColor = inColor;
        }
    )";

    std::string fragmentCode = R"(
        #version 450

        layout(location = 0) in vec3 fragColor;
        layout(location = 0) out vec4 outColor;

        void main() {
            outColor = vec4(fragColor, 1.0);
        }
    )";

    VulkanShader shader;
    shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertexCode)
          .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentCode)
          .Build();
}
```

### Using Pre-Compiled SPIR-V

```cpp
void CreateShaderFromSpirv() {
    // Load SPIR-V binaries
    auto vertSpirv = LoadBinaryFile("shaders/vert.spv");
    auto fragSpirv = LoadBinaryFile("shaders/frag.spv");

    VulkanShader shader;
    shader.AddStageSPV(VK_SHADER_STAGE_VERTEX_BIT, vertSpirv)
          .AddStageSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragSpirv)
          .Build();
}

// Helper function to load binary file
std::vector<uint32_t> LoadBinaryFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    return buffer;
}
```

---

## Shader Variants

### Quality Levels

```cpp
enum class ShaderQuality {
    LOW,
    MEDIUM,
    HIGH,
    ULTRA
};

VulkanShader CreateShaderForQuality(ShaderQuality quality) {
    VulkanShader shader;

    switch (quality) {
        case ShaderQuality::LOW:
            shader.AddDefine("QUALITY_LEVEL", "0")
                  .AddDefine("ENABLE_SHADOWS", "0");
            break;
        case ShaderQuality::MEDIUM:
            shader.AddDefine("QUALITY_LEVEL", "1")
                  .AddDefine("ENABLE_SHADOWS", "1")
                  .AddDefine("SHADOW_SAMPLES", "4");
            break;
        case ShaderQuality::HIGH:
            shader.AddDefine("QUALITY_LEVEL", "2")
                  .AddDefine("ENABLE_SHADOWS", "1")
                  .AddDefine("SHADOW_SAMPLES", "16")
                  .AddDefine("ENABLE_AO", "1");
            break;
        case ShaderQuality::ULTRA:
            shader.AddDefine("QUALITY_LEVEL", "3")
                  .AddDefine("ENABLE_SHADOWS", "1")
                  .AddDefine("SHADOW_SAMPLES", "32")
                  .AddDefine("ENABLE_AO", "1")
                  .AddDefine("ENABLE_REFLECTIONS", "1");
            break;
    }

    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/pbr.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/pbr.frag")
          .EnableCache()
          .Build();

    return shader;
}
```

### Feature Flags

```cpp
struct ShaderFeatures {
    bool useNormalMapping = false;
    bool useParallaxMapping = false;
    bool useEmissive = false;
    bool useMetallic = false;
    int maxLights = 8;
};

VulkanShader CreateMaterialShader(const ShaderFeatures& features) {
    VulkanShader shader;

    if (features.useNormalMapping) {
        shader.AddDefine("USE_NORMAL_MAP");
    }
    if (features.useParallaxMapping) {
        shader.AddDefine("USE_PARALLAX_MAP");
    }
    if (features.useEmissive) {
        shader.AddDefine("USE_EMISSIVE");
    }
    if (features.useMetallic) {
        shader.AddDefine("USE_METALLIC");
    }

    shader.AddDefine("MAX_LIGHTS", std::to_string(features.maxLights));

    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/material.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/material.frag")
          .EnableCache()
          .Build();

    return shader;
}

// Usage
ShaderFeatures features;
features.useNormalMapping = true;
features.useMetallic = true;
features.maxLights = 16;

auto shader = CreateMaterialShader(features);
```

---

## Compute Shaders

### Basic Compute Shader

```cpp
void CreateComputeShader() {
    VulkanShader computeShader;
    computeShader.AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/compute.comp")
                 .EnableCache()
                 .Build();

    // Use in compute pipeline
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stage = *computeShader.GetStages();
    // ...
}
```

### Particle System

```cpp
void CreateParticleComputeShader() {
    VulkanShader particleShader;

    particleShader.AddDefine("PARTICLE_COUNT", "10000")
                  .AddDefine("WORK_GROUP_SIZE", "256")
                  .AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/particles.comp")
                  .EnableCache()
                  .Build();

    // Dispatch compute work
    // vkCmdDispatch(commandBuffer, PARTICLE_COUNT / WORK_GROUP_SIZE, 1, 1);
}
```

### Image Processing

```cpp
void CreateImageFilterShader() {
    VulkanShader blurShader;

    blurShader.AddDefine("KERNEL_SIZE", "9")
              .AddDefine("SIGMA", "2.0")
              .AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/gaussian_blur.comp")
              .Build();

    VulkanShader sharpenShader;
    sharpenShader.AddDefine("SHARPEN_AMOUNT", "1.5")
                 .AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/sharpen.comp")
                 .Build();
}
```

---

## Advanced Pipeline Stages

### Geometry Shader

```cpp
void CreateGeometryShader() {
    VulkanShader shader;

    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/geometry.vert")
          .AddStageFromFile(VK_SHADER_STAGE_GEOMETRY_BIT, "shaders/geometry.geom")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/geometry.frag")
          .Build();
}
```

### Tessellation Pipeline

```cpp
void CreateTessellationShader() {
    VulkanShader shader;

    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/terrain.vert")
          .AddStageFromFile(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "shaders/terrain.tesc")
          .AddStageFromFile(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "shaders/terrain.tese")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/terrain.frag")
          .AddDefine("TESSELLATION_LEVEL", "64")
          .EnableCache()
          .Build();
}
```

### Wireframe Rendering with Geometry Shader

```cpp
void CreateWireframeShader() {
    VulkanShader shader;

    shader.AddDefine("WIREFRAME_WIDTH", "2.0")
          .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/wireframe.vert")
          .AddStageFromFile(VK_SHADER_STAGE_GEOMETRY_BIT, "shaders/wireframe.geom")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/wireframe.frag")
          .Build();
}
```

---

## Hot Reloading

### Basic Hot Reload in Game Loop

```cpp
class Application {
    VulkanShader shader;
    VulkanPipeline pipeline;

public:
    void Init() {
        shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/game.vert")
              .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/game.frag")
              .Build();

        CreatePipeline();
    }

    void Update() {
        // Check for shader changes
        if (shader.HasSourceChanged()) {
            std::cout << "Reloading shaders..." << std::endl;

            if (shader.HotReload()) {
                // Wait for device idle before rebuilding pipeline
                vkDeviceWaitIdle(device);

                // Rebuild pipeline with new shader
                pipeline.Destroy();
                CreatePipeline();

                std::cout << "Shader reload complete!" << std::endl;
            } else {
                std::cerr << "Shader reload failed!" << std::endl;
            }
        }

        // Regular update logic...
    }

private:
    void CreatePipeline() {
        pipeline.Create(/* ... */, &shader);
    }
};
```

### Hot Reload with Debouncing

```cpp
class ShaderHotReloader {
    VulkanShader& shader;
    VulkanPipeline& pipeline;
    std::chrono::steady_clock::time_point lastReloadTime;
    const std::chrono::milliseconds debounceDelay{500};

public:
    ShaderHotReloader(VulkanShader& s, VulkanPipeline& p)
        : shader(s), pipeline(p) {}

    void Update() {
        if (!shader.HasSourceChanged()) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastReload = now - lastReloadTime;

        if (timeSinceLastReload < debounceDelay) {
            return; // Too soon, debounce
        }

        std::cout << "Shader changed, reloading..." << std::endl;

        if (shader.HotReload()) {
            vkDeviceWaitIdle(device);
            pipeline.Rebuild(&shader);
            lastReloadTime = now;
            std::cout << "Reload successful!" << std::endl;
        }
    }
};
```

### Multi-Shader Hot Reload

```cpp
class ShaderManager {
    struct ShaderEntry {
        std::unique_ptr<VulkanShader> shader;
        std::function<void()> rebuildCallback;
    };

    std::vector<ShaderEntry> shaders;

public:
    void AddShader(std::unique_ptr<VulkanShader> shader,
                   std::function<void()> rebuildCallback) {
        shaders.push_back({std::move(shader), rebuildCallback});
    }

    void CheckForReloads() {
        for (auto& entry : shaders) {
            if (entry.shader->HasSourceChanged()) {
                if (entry.shader->HotReload()) {
                    entry.rebuildCallback();
                }
            }
        }
    }
};

// Usage
ShaderManager manager;

auto mainShader = std::make_unique<VulkanShader>();
mainShader->AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "main.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "main.frag")
          .Build();

manager.AddShader(std::move(mainShader), [&]() {
    RebuildMainPipeline();
});

// In update loop
manager.CheckForReloads();
```

---

## Shader Includes

### Basic Include Example

**common.glsl:**
```glsl
#ifndef COMMON_GLSL
#define COMMON_GLSL

const float PI = 3.14159265359;
const float EPSILON = 0.0001;

vec3 gammaCorrect(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}

#endif
```

**lighting.glsl:**
```glsl
#include "common.glsl"

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
};

vec3 calculateLighting(Light light, vec3 position, vec3 normal) {
    vec3 lightDir = normalize(light.position - position);
    float diff = max(dot(normal, lightDir), 0.0);
    return light.color * light.intensity * diff;
}
```

**main.frag:**
```glsl
#version 450
#include "lighting.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
    Light mainLight;
    mainLight.position = vec3(10, 10, 10);
    mainLight.color = vec3(1.0);
    mainLight.intensity = 1.0;

    vec3 color = calculateLighting(mainLight, fragPos, fragNormal);
    outColor = vec4(gammaCorrect(color), 1.0);
}
```

**C++ Code:**
```cpp
void CreateShaderWithIncludes() {
    ShaderCompileOptions options;
    options.includePaths = {"./shaders/include", "./shaders/common"};

    VulkanShader shader;
    shader.SetCompileOptions(options)
          .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/main.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/main.frag")
          .Build();
}
```

---

## Caching

### Enable Caching for Faster Startup

```cpp
void EnableShaderCaching() {
    VulkanShader shader;

    // Enable caching with custom path
    shader.EnableCache("./cache/shaders")
          .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/main.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/main.frag")
          .Build();

    // First run: Compiles GLSL and saves to cache
    // Subsequent runs: Loads from cache (much faster)
}
```

### Cache Management

```cpp
class ShaderCacheManager {
public:
    static void ClearCache(const std::string& cachePath = "./shader_cache") {
        std::filesystem::remove_all(cachePath);
        std::filesystem::create_directories(cachePath);
    }

    static size_t GetCacheSize(const std::string& cachePath = "./shader_cache") {
        size_t totalSize = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(cachePath)) {
            if (entry.is_regular_file()) {
                totalSize += entry.file_size();
            }
        }
        return totalSize;
    }

    static int GetCacheFileCount(const std::string& cachePath = "./shader_cache") {
        int count = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(cachePath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".spv") {
                count++;
            }
        }
        return count;
    }
};
```

---

## Custom Entry Points

### Multiple Entry Points in One Shader

**shader.comp:**
```glsl
#version 450

layout (local_size_x = 256) in;

layout(binding = 0) buffer Data {
    float values[];
};

void processLow() {
    // Low quality processing
    values[gl_GlobalInvocationID.x] *= 0.5;
}

void processHigh() {
    // High quality processing
    values[gl_GlobalInvocationID.x] =
        values[gl_GlobalInvocationID.x] * 0.7 +
        values[gl_GlobalInvocationID.x + 1] * 0.3;
}
```

**C++ Code:**
```cpp
void CreateShadersWithDifferentEntryPoints() {
    std::string shaderSource = LoadFile("shader.comp");

    // Create two shaders from same source with different entry points
    VulkanShader shaderLow;
    shaderLow.AddStage(VK_SHADER_STAGE_COMPUTE_BIT, shaderSource, "processLow")
             .Build();

    VulkanShader shaderHigh;
    shaderHigh.AddStage(VK_SHADER_STAGE_COMPUTE_BIT, shaderSource, "processHigh")
              .Build();
}
```

---

## Error Handling

### Comprehensive Error Checking

```cpp
bool CreateShaderSafely() {
    auto logger = std::make_shared<Logger>("ShaderCreation");

    VulkanShader shader;
    shader.SetLogger(logger);

    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/test.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/test.frag");

    if (!shader.Build()) {
        std::cerr << "Shader build failed!" << std::endl;
        std::cerr << "Logs:\n" << logger->ExtractLogs() << std::endl;
        return false;
    }

    if (!shader.IsInitialized()) {
        std::cerr << "Shader not properly initialized!" << std::endl;
        return false;
    }

    if (shader.GetStageCount() != 2) {
        std::cerr << "Expected 2 stages, got " << shader.GetStageCount() << std::endl;
        return false;
    }

    std::cout << "Shader created successfully!" << std::endl;
    return true;
}
```

---

## Performance Optimization

### Pre-compile for Production

```bash
# Compile shaders offline
glslangValidator -V shader.vert -o shader.vert.spv
glslangValidator -V shader.frag -o shader.frag.spv
```

```cpp
// Load pre-compiled SPIR-V in production
void LoadProductionShaders() {
    VulkanShader shader;
    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/shader.vert.spv")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/shader.frag.spv")
          .Build();
    // No compilation needed - instant loading!
}
```

### Parallel Shader Compilation

```cpp
void CompileShadersInParallel() {
    std::vector<std::unique_ptr<VulkanShader>> shaders;
    std::vector<std::thread> threads;

    // Shader definitions
    struct ShaderDef {
        std::string vertPath;
        std::string fragPath;
    };

    std::vector<ShaderDef> shaderDefs = {
        {"shaders/pbr.vert", "shaders/pbr.frag"},
        {"shaders/skybox.vert", "shaders/skybox.frag"},
        {"shaders/shadow.vert", "shaders/shadow.frag"},
        {"shaders/ui.vert", "shaders/ui.frag"}
    };

    // Compile in parallel
    for (const auto& def : shaderDefs) {
        threads.emplace_back([def]() {
            auto shader = std::make_unique<VulkanShader>();
            shader->AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, def.vertPath)
                  .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, def.fragPath)
                  .EnableCache()
                  .Build();
            // Store shader...
        });
    }

    // Wait for all
    for (auto& thread : threads) {
        thread.join();
    }
}
```

This completes the examples documentation with practical, real-world usage patterns!
