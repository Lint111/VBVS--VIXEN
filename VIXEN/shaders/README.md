# Shader Assets

This directory contains all GLSL shader source files for the VIXEN project.

## Structure

```
shaders/
├── ComputeTest.comp      # Test compute shader
├── Draw.vert/frag        # Basic graphics pipeline
├── Fullscreen.vert       # Fullscreen quad
├── VoxelRayMarch.comp    # Voxel ray marching compute
└── VoxelRayMarch.frag    # Voxel ray marching fragment
```

## Usage

### In CMake

The shader directory is exposed via `VIXEN_SHADER_SOURCE_DIR` variable:

```cmake
add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    COMPUTE ${VIXEN_SHADER_SOURCE_DIR}/MyShader.comp
    OUTPUT_DIR ${APP_SHADER_OUTPUT_DIR}
)
```

### In Visual Studio

Shaders appear under the **"Assets/ShaderAssets"** solution folder, organized by type:
- Vertex Shaders
- Fragment Shaders
- Compute Shaders
- Geometry Shaders
- Tessellation Control/Evaluation
- Mesh/Task Shaders
- Ray Tracing Shaders
- Common (includes)

## Compilation

Shaders are compiled at **runtime** by applications. The application discovers and compiles shaders on-demand from this directory.

**Optional pre-compilation** for faster startup:
```cmake
# In application CMakeLists.txt (optional)
include(${CMAKE_SOURCE_DIR}/libraries/ShaderManagement/cmake/ShaderToolUtils.cmake)

add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    COMPUTE ${VIXEN_SHADER_SOURCE_DIR}/MyShader.comp
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/application/main/generated/shaders
)
add_dependencies(YourApp MyShader_Bundle)
```

## Adding New Shaders

1. Add your `.vert`/`.frag`/`.comp`/etc. file to this directory
2. Register the shader in your application code:
   ```cpp
   shaderLibNode->RegisterShaderBuilder([](int vulkanVer, int spirvVer) {
       ShaderBundleBuilder builder;
       builder.SetProgramName("MyShader")
              .AddStageFromFile(ShaderStage::Compute, "shaders/MyShader.comp", "main");
       return builder;
   });
   ```
3. That's it! Shader compiles at runtime automatically
4. (Optional) Add pre-compilation to CMakeLists.txt for faster startup

## Runtime Loading

Shaders are loaded from source at runtime using `RegisterShaderBuilder()`:

```cpp
shaderLibNode->RegisterShaderBuilder([](int vulkanVer, int spirvVer) {
    ShaderManagement::ShaderBundleBuilder builder;
    builder.SetProgramName("MyShader")
           .AddStageFromFile(ShaderStage::Compute, "shaders/MyShader.comp", "main");
    return builder;
});
```

The system searches common paths relative to the executable working directory.

## Shader Conventions

- **Naming**: `PascalCase.stage` (e.g., `VoxelRayMarch.comp`)
- **Entry point**: `main` (standard GLSL)
- **Version**: `#version 460` (Vulkan 1.2+)
- **Includes**: Place shared code in `.glsl` files

## IDE Integration

- **Syntax highlighting**: Install GLSL language extension for VS Code/Visual Studio
- **Validation**: Shaders are validated at build time using glslangValidator
- **Hot reload**: Runtime compilation supports shader hot-reloading during development
