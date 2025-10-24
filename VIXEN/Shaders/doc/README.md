# VulkanShader Library Documentation

## Overview

The **VulkanShader** library is a comprehensive shader management system for Vulkan applications. It provides a modern, flexible, and feature-rich API for loading, compiling, and managing shaders in your Vulkan projects.

## Features

### Core Features
- **Multi-Stage Support**: Support for all Vulkan shader stages (vertex, fragment, geometry, tessellation control/evaluation, compute)
- **Flexible Loading**: Load shaders from source code, SPIR-V binaries, or files
- **Builder Pattern API**: Intuitive, chainable API for shader configuration
- **Backward Compatibility**: Legacy API maintained for existing code

### Advanced Features
- **Shader Caching**: Automatic SPIR-V caching system to speed up compilation
- **Hot Reloading**: Detect and reload shader changes at runtime
- **Preprocessor Support**: Define macros and preprocessor symbols for shader variants
- **Include System**: Use `#include` directives in GLSL shaders
- **Custom Entry Points**: Specify custom entry point function names
- **Optimization Control**: Enable/disable SPIR-V optimization and debug info
- **Thread Safety**: All operations are thread-safe with mutex protection
- **Comprehensive Logging**: Integration with the project's logger system
- **Shader Reflection**: Framework for shader introspection (extensible with SPIRV-Reflect)

## Quick Start

### Basic Example

```cpp
#include "VulkanShader.h"

// Create a shader with vertex and fragment stages
VulkanShader shader;
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/basic.vert")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/basic.frag")
      .Build();

// Use the shader in a pipeline
pipeline.CreatePipeline(/* ... */, &shader);

// Clean up when done
shader.DestroyShader();
```

### With Caching and Defines

```cpp
VulkanShader shader;
shader.AddDefine("USE_TEXTURES", "1")
      .AddDefine("MAX_LIGHTS", "8")
      .EnableCache("./shader_cache")
      .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/advanced.vert")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/advanced.frag")
      .Build();
```

### Compute Shader Example

```cpp
VulkanShader computeShader;
computeShader.AddStageFromFile(VK_SHADER_STAGE_COMPUTE_BIT, "shaders/particle_sim.comp")
             .EnableCache()
             .Build();
```

### Hot Reloading

```cpp
VulkanShader shader;
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/test.vert")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/test.frag")
      .Build();

// In your update loop
if (shader.HasSourceChanged()) {
    shader.HotReload();
    // Rebuild pipeline with updated shader
}
```

## Documentation Structure

- **[API.md](API.md)** - Complete API reference
- **[EXAMPLES.md](EXAMPLES.md)** - Detailed usage examples
- **[FEATURES.md](FEATURES.md)** - In-depth feature explanations
- **[MIGRATION_GUIDE.md](MIGRATION_GUIDE.md)** - Migrating from legacy API
- **[ADVANCED_FEATURES.md](ADVANCED_FEATURES.md)** - Advanced usage patterns

## Compilation Requirements

### With GLSL Support
To enable runtime GLSL to SPIR-V compilation, build with the `AUTO_COMPILE_GLSL_TO_SPV` flag:

```cmake
add_definitions(-DAUTO_COMPILE_GLSL_TO_SPV)
```

This requires linking against:
- glslang
- SPIRV
- glslang-default-resource-limits

### Pre-compiled SPIR-V Only
If you only use pre-compiled `.spv` files, the flag is not required and no glslang dependencies are needed.

## Common Patterns

### Creating Shader Variants

```cpp
// Create multiple variants with different defines
VulkanShader shaderLowQuality;
shaderLowQuality.AddDefine("QUALITY_LEVEL", "0")
                .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
                .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
                .Build();

VulkanShader shaderHighQuality;
shaderHighQuality.AddDefine("QUALITY_LEVEL", "2")
                 .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
                 .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
                 .Build();
```

### Loading from Memory

```cpp
std::string vertexSource = R"(
    #version 450
    layout(location = 0) in vec3 inPosition;
    void main() {
        gl_Position = vec4(inPosition, 1.0);
    }
)";

VulkanShader shader;
shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertexSource)
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
      .Build();
```

### Using SPIR-V Directly

```cpp
// Load pre-compiled SPIR-V
std::vector<uint32_t> vertSpirv = LoadSpirvFile("shader.vert.spv");
std::vector<uint32_t> fragSpirv = LoadSpirvFile("shader.frag.spv");

VulkanShader shader;
shader.AddStageSPV(VK_SHADER_STAGE_VERTEX_BIT, vertSpirv)
      .AddStageSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragSpirv)
      .Build();
```

## Error Handling

The library uses the project's logger system for error reporting. Set a logger to receive detailed error messages:

```cpp
auto shaderLogger = std::make_shared<Logger>("MyShader");
shader.SetLogger(shaderLogger);
shader.Build();

// Check logs
if (!shader.IsInitialized()) {
    std::cout << shaderLogger->ExtractLogs() << std::endl;
}
```

## Performance Tips

1. **Enable Caching**: Use `EnableCache()` to avoid recompiling shaders on every run
2. **Pre-compile Shaders**: For production, compile to SPIR-V offline and load `.spv` files
3. **Disable Debug Info**: Set `enableDebugInfo = false` in compile options for release builds
4. **Reuse Shader Objects**: Create shader objects once and reuse them across multiple pipelines

## Thread Safety

All VulkanShader operations are thread-safe. You can safely:
- Build shaders on background threads
- Hot reload from any thread
- Query shader status from multiple threads

```cpp
// Safe to use from multiple threads
std::thread t1([&]() { shader1.Build(); });
std::thread t2([&]() { shader2.Build(); });
t1.join();
t2.join();
```

## Troubleshooting

### Shader Compilation Fails
- Check that `AUTO_COMPILE_GLSL_TO_SPV` is defined if using GLSL
- Verify your GLSL syntax is correct
- Check logger output for detailed error messages
- Ensure glslang libraries are properly linked

### Include Files Not Found
- Add include paths using `ShaderCompileOptions::includePaths`
- Verify file paths are correct and files exist
- Use relative paths from the shader file location

### Hot Reload Not Working
- Ensure files were loaded using `AddStageFromFile()`
- Call `HasSourceChanged()` regularly to check for modifications
- Verify file system supports modification time queries

## Contributing

When extending the shader library:
1. Maintain backward compatibility with legacy API
2. Add comprehensive logging for all operations
3. Ensure thread safety with proper mutex usage
4. Update documentation for new features
5. Add examples demonstrating new functionality

## License

This shader library is part of the VIXEN Vulkan framework project.

## See Also

- [Vulkan Specification](https://www.khronos.org/vulkan/)
- [GLSL Specification](https://www.khronos.org/opengl/wiki/OpenGL_Shading_Language)
- [SPIR-V Specification](https://www.khronos.org/spir/)
- [glslang Documentation](https://github.com/KhronosGroup/glslang)
