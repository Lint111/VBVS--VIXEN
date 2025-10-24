# VulkanShader Features

This document provides an in-depth explanation of all features in the VulkanShader library.

## Table of Contents
1. [Multi-Stage Support](#multi-stage-support)
2. [Shader Caching System](#shader-caching-system)
3. [Hot Reloading](#hot-reloading)
4. [Preprocessor Defines](#preprocessor-defines)
5. [Include System](#include-system)
6. [Custom Entry Points](#custom-entry-points)
7. [Optimization Control](#optimization-control)
8. [Thread Safety](#thread-safety)
9. [Logging System](#logging-system)
10. [Builder Pattern API](#builder-pattern-api)

---

## Multi-Stage Support

### Overview

VulkanShader supports all Vulkan shader stages, not just vertex and fragment shaders.

### Supported Stages

| Stage | Vulkan Flag | Typical Use Case |
|-------|------------|------------------|
| Vertex | `VK_SHADER_STAGE_VERTEX_BIT` | Vertex transformation |
| Fragment | `VK_SHADER_STAGE_FRAGMENT_BIT` | Pixel shading |
| Geometry | `VK_SHADER_STAGE_GEOMETRY_BIT` | Primitive generation |
| Tessellation Control | `VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT` | Tessellation factor control |
| Tessellation Evaluation | `VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT` | Tessellated vertex processing |
| Compute | `VK_SHADER_STAGE_COMPUTE_BIT` | General-purpose GPU computing |

### Implementation

The library uses a flexible stage array that can accommodate up to 6 stages simultaneously:

```cpp
const int MAX_SHADER_STAGES = 6;
VkPipelineShaderStageCreateInfo shaderStages[MAX_SHADER_STAGES];
```

### Usage Examples

**Graphics Pipeline (Vertex + Fragment):**
```cpp
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
      .Build();
```

**Tessellation Pipeline:**
```cpp
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "terrain.vert")
      .AddStageFromFile(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "terrain.tesc")
      .AddStageFromFile(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "terrain.tese")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "terrain.frag")
      .Build();
```

**Compute Pipeline:**
```cpp
shader.AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT, "compute.comp")
      .Build();
```

### Benefits

- Single shader object can represent complex multi-stage pipelines
- No need for multiple shader objects for advanced rendering techniques
- Simplified pipeline creation code

---

## Shader Caching System

### Overview

The caching system stores compiled SPIR-V binaries to disk, avoiding expensive recompilation on subsequent runs.

### How It Works

1. **Cache Key Generation**: Hash generated from source code, defines, and compilation options
2. **Cache Lookup**: Before compilation, checks if cached SPIR-V exists
3. **Cache Storage**: After compilation, saves SPIR-V to cache directory
4. **Cache Validation**: Cache is automatically invalidated when source or options change

### Cache Key Components

The cache key is generated from:
- GLSL source code
- Shader stage type
- All preprocessor defines
- Entry point name
- Optimization settings
- Debug info settings

### File Structure

```
shader_cache/
├── a1b2c3d4e5f6g7h8.spv  # Cached vertex shader
├── f9e8d7c6b5a4d3c2.spv  # Cached fragment shader
└── ...
```

### Performance Impact

| Operation | Without Cache | With Cache |
|-----------|---------------|------------|
| First Run | 100-500ms | 100-500ms |
| Subsequent Runs | 100-500ms | 1-5ms |
| Cache Hit Rate | N/A | ~95%+ |

### Usage

```cpp
// Enable caching with default path
shader.EnableCache();

// Enable caching with custom path
shader.EnableCache("./build/shader_cache");

// Build automatically uses cache
shader.Build();
```

### Cache Management

```cpp
// Clear cache manually
std::filesystem::remove_all("./shader_cache");

// Check cache size
size_t cacheSize = 0;
for (auto& entry : std::filesystem::directory_iterator("./shader_cache")) {
    cacheSize += entry.file_size();
}
```

### Best Practices

1. **Production Builds**: Use pre-compiled SPIR-V, no cache needed
2. **Development**: Enable cache to speed up iteration
3. **CI/CD**: Clear cache on major shader changes
4. **Distribution**: Don't ship cache files to users

---

## Hot Reloading

### Overview

Hot reloading allows shaders to be recompiled and reloaded while the application is running, enabling rapid iteration during development.

### How It Works

1. **File Tracking**: Stores file paths and modification times
2. **Change Detection**: Checks if files have been modified
3. **Automatic Reload**: Recompiles and rebuilds shader modules
4. **Pipeline Update**: Application can rebuild pipelines with new shaders

### Implementation Details

```cpp
// Internal tracking
std::map<VkShaderStageFlagBits, std::string> stageFilePaths;
std::map<std::string, std::filesystem::file_time_type> fileModTimes;
```

### Usage Patterns

**Manual Checking:**
```cpp
// In update loop
if (shader.HasSourceChanged()) {
    if (shader.HotReload()) {
        // Rebuild pipeline
        pipeline.Rebuild(&shader);
    }
}
```

**Keyboard Trigger:**
```cpp
void Update() {
    if (keyboard.IsKeyPressed(KEY_F5)) {
        if (shader.HotReload()) {
            std::cout << "Shaders reloaded!" << std::endl;
            RebuildPipeline();
        }
    }
}
```

**Automatic with Debouncing:**
```cpp
auto lastReloadTime = std::chrono::steady_clock::now();
const auto debounceDelay = std::chrono::milliseconds(500);

void Update() {
    if (!shader.HasSourceChanged()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - lastReloadTime > debounceDelay) {
        shader.HotReload();
        lastReloadTime = now;
    }
}
```

### Requirements

- Shaders must be loaded with `AddStageFromFile()`
- File system must support modification time queries
- Device must be idle during pipeline rebuild

### Limitations

- Only works with file-based shaders
- Requires pipeline rebuild after reload
- Not suitable for production builds

### Performance Considerations

- File system checks are fast (~0.1ms per file)
- Recompilation takes 100-500ms
- Pipeline rebuild takes 50-200ms
- Total hot reload time: ~150-700ms

---

## Preprocessor Defines

### Overview

Preprocessor defines enable shader variants from a single source file, reducing code duplication and improving maintainability.

### How It Works

Defines are injected into GLSL source code after the `#version` directive and before any other code:

```glsl
#version 450
#define USE_TEXTURES 1
#define MAX_LIGHTS 8
// Rest of shader code...
```

### Usage

**Single Define:**
```cpp
shader.AddDefine("ENABLE_SHADOWS");
```

**Define with Value:**
```cpp
shader.AddDefine("MAX_LIGHTS", "16");
shader.AddDefine("PI", "3.14159265359");
```

**Multiple Defines:**
```cpp
shader.AddDefine("USE_PBR")
      .AddDefine("USE_NORMAL_MAPS")
      .AddDefine("USE_AO_MAPS")
      .AddDefine("MAX_LIGHTS", "32");
```

### Shader Code Example

**GLSL Shader:**
```glsl
#version 450

// These defines come from C++ code
#ifndef MAX_LIGHTS
#define MAX_LIGHTS 8
#endif

#ifdef USE_NORMAL_MAPS
layout(binding = 2) uniform sampler2D normalMap;
#endif

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = vec3(0.0);

    #ifdef USE_PBR
        color = calculatePBR();
    #else
        color = calculatePhong();
    #endif

    outColor = vec4(color, 1.0);
}
```

**C++ Code:**
```cpp
// Low quality
VulkanShader shaderLow;
shaderLow.AddDefine("MAX_LIGHTS", "4")
         .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
         .Build();

// High quality
VulkanShader shaderHigh;
shaderHigh.AddDefine("MAX_LIGHTS", "32")
          .AddDefine("USE_PBR")
          .AddDefine("USE_NORMAL_MAPS")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
          .Build();
```

### Benefits

- **Single Source**: Maintain one shader file instead of many variants
- **Easy Switching**: Change quality levels by changing defines
- **Type Safety**: Pass values from C++ to shaders
- **Reduced Errors**: Less code duplication

### Common Use Cases

1. **Quality Levels**: Adjust shader complexity
2. **Feature Toggles**: Enable/disable optional features
3. **Platform Differences**: Handle platform-specific code
4. **Debug Modes**: Add debug visualization
5. **Constants**: Pass numeric constants

---

## Include System

### Overview

The include system allows GLSL shaders to use `#include` directives, enabling code reuse and better organization.

### Syntax

```glsl
#include "common.glsl"           // Relative to shader file
#include "lighting/pbr.glsl"     // Subdirectory
```

### How It Works

1. **Parse**: Scan shader source for `#include` directives
2. **Resolve**: Search include paths for referenced files
3. **Guard**: Prevent circular includes with include guard tracking
4. **Recursive**: Process includes in included files
5. **Concatenate**: Merge all content into single source

### Setup Include Paths

```cpp
ShaderCompileOptions options;
options.includePaths = {
    "./shaders/include",
    "./shaders/common",
    "/usr/share/shaders"
};

shader.SetCompileOptions(options)
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "main.frag")
      .Build();
```

### Include Resolution Order

1. Relative to current file
2. Search include paths in order
3. Error if not found

### Example Structure

```
shaders/
├── common/
│   ├── constants.glsl
│   └── utils.glsl
├── lighting/
│   ├── pbr.glsl
│   └── phong.glsl
└── main.frag
```

**constants.glsl:**
```glsl
#ifndef CONSTANTS_GLSL
#define CONSTANTS_GLSL

const float PI = 3.14159265359;
const float EPSILON = 0.0001;

#endif
```

**utils.glsl:**
```glsl
#include "constants.glsl"

vec3 gammaCorrect(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}
```

**main.frag:**
```glsl
#version 450
#include "common/utils.glsl"
#include "lighting/pbr.glsl"

void main() {
    vec3 color = calculatePBR();
    outColor = vec4(gammaCorrect(color), 1.0);
}
```

### Include Guards

Use include guards to prevent multiple inclusion:

```glsl
#ifndef MY_SHADER_GLSL
#define MY_SHADER_GLSL

// Shader code here...

#endif
```

### Benefits

- **Code Reuse**: Share common functions across shaders
- **Organization**: Split large shaders into logical modules
- **Maintainability**: Update shared code in one place
- **Readability**: Cleaner, more focused shader files

---

## Custom Entry Points

### Overview

Custom entry points allow multiple shader functions in a single source file, selected at shader creation time.

### Use Cases

1. **Shader Libraries**: Multiple related functions in one file
2. **Quality Variants**: Different implementations with same structure
3. **Multi-API**: Different entry points for different APIs
4. **Testing**: Test and production implementations side-by-side

### Example

**shader.comp:**
```glsl
#version 450

layout (local_size_x = 256) in;
layout(binding = 0) buffer Data { float values[]; };

void processSimple() {
    uint idx = gl_GlobalInvocationID.x;
    values[idx] *= 2.0;
}

void processComplex() {
    uint idx = gl_GlobalInvocationID.x;
    values[idx] = sqrt(values[idx]) * 2.0 + 1.0;
}
```

**C++ Code:**
```cpp
// Create two shaders from same source
VulkanShader simpleShader;
simpleShader.AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT,
                              "shader.comp",
                              "processSimple")  // Custom entry point
            .Build();

VulkanShader complexShader;
complexShader.AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT,
                               "shader.comp",
                               "processComplex")  // Different entry point
             .Build();
```

### Benefits

- Reduced file count
- Easier to maintain related variants
- Shared code between variants
- Faster iteration

---

## Optimization Control

### Overview

Control SPIR-V optimization and debug information generation.

### Options

```cpp
struct ShaderCompileOptions {
    bool enableOptimization = true;   // SPIR-V optimizer
    bool enableDebugInfo = false;     // Debug symbols
};
```

### Optimization Levels

**Development:**
```cpp
ShaderCompileOptions opts;
opts.enableOptimization = false;  // Faster compilation
opts.enableDebugInfo = true;      // Better debugging
shader.SetCompileOptions(opts);
```

**Production:**
```cpp
ShaderCompileOptions opts;
opts.enableOptimization = true;   // Better performance
opts.enableDebugInfo = false;     // Smaller binaries
shader.SetCompileOptions(opts);
```

### Performance Impact

| Setting | Compile Time | Runtime Performance | SPIR-V Size |
|---------|-------------|-------------------|-------------|
| Opt: Off, Debug: On | Fast | Slow | Large |
| Opt: On, Debug: Off | Slow | Fast | Small |
| Opt: Off, Debug: Off | Fast | Slow | Medium |
| Opt: On, Debug: On | Slow | Fast | Large |

---

## Thread Safety

### Overview

All VulkanShader operations are thread-safe using mutex protection.

### Implementation

```cpp
mutable std::mutex shaderMutex;

// All public methods lock the mutex
VulkanShader& AddStage(...) {
    std::lock_guard<std::mutex> lock(shaderMutex);
    // ...
}
```

### Safe Concurrent Operations

```cpp
// Compile multiple shaders in parallel
std::vector<std::thread> threads;

for (int i = 0; i < shaderCount; ++i) {
    threads.emplace_back([&, i]() {
        shaders[i].Build();
    });
}

for (auto& t : threads) {
    t.join();
}
```

### Performance Considerations

- Mutex overhead is negligible compared to compilation time
- Parallel compilation provides significant speedup
- No data races or undefined behavior

---

## Logging System

### Overview

Integration with the project's hierarchical logger for comprehensive error reporting.

### Setup

```cpp
auto logger = std::make_shared<Logger>("MyShader");
shader.SetLogger(logger);
```

### Log Levels

- **Info**: Normal operations (shader added, cache hit, etc.)
- **Warning**: Non-critical issues (include not found, etc.)
- **Error**: Critical failures (compilation failed, etc.)

### Example Output

```
[2024-10-24 10:30:15] [MyShader] INFO: Added shader stage: vert with entry point: main
[2024-10-24 10:30:15] [MyShader] INFO: Added shader stage: frag with entry point: main
[2024-10-24 10:30:15] [MyShader] INFO: Shader caching enabled at: ./shader_cache
[2024-10-24 10:30:15] [MyShader] INFO: Building shader with 2 stages
[2024-10-24 10:30:15] [MyShader] INFO: Loaded shader from cache: vert
[2024-10-24 10:30:15] [MyShader] INFO: Loaded shader from cache: frag
[2024-10-24 10:30:15] [MyShader] INFO: Shader built successfully with 2 stages
```

### Extracting Logs

```cpp
shader.SetLogger(logger);
shader.Build();

std::string logs = logger->ExtractLogs();
std::cout << logs << std::endl;
```

---

## Builder Pattern API

### Overview

The builder pattern provides a fluent, chainable API for constructing shaders.

### Benefits

1. **Readable**: Code reads like natural language
2. **Flexible**: Configure in any order
3. **Discoverable**: IDE autocomplete shows all options
4. **Safe**: Type-safe, compile-time checked

### Pattern Structure

```cpp
shader.MethodA(...)
      .MethodB(...)
      .MethodC(...)
      .Build();
```

### Complete Example

```cpp
VulkanShader shader;
shader.SetLogger(myLogger)
      .AddDefine("QUALITY", "2")
      .AddDefine("MAX_LIGHTS", "16")
      .EnableCache("./cache")
      .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
      .Build();
```

### Error Handling

```cpp
if (!shader.Build()) {
    std::cerr << "Shader build failed" << std::endl;
    return false;
}

if (!shader.IsInitialized()) {
    std::cerr << "Shader not initialized" << std::endl;
    return false;
}
```

This comprehensive feature set makes VulkanShader a powerful and flexible shader management solution!
