# Shader Build Tool

Build-time shader compiler and SDI generator for the VIXEN ShaderManagement library.

## Overview

The `shader_tool` allows you to:
- Compile shaders at build time instead of runtime
- Generate SPIRV Descriptor Interface (SDI) headers for type-safe shader access
- Build central SDI registry for optimized compilation
- Process multiple shaders in batch from a config file

This enables:
- **Faster runtime startup** - No shader compilation on app launch
- **Build-time error detection** - Shader errors caught during compilation
- **Pre-baked SDI headers** - Immediate IDE autocomplete for shader descriptors
- **Optimized builds** - Only recompile changed shaders

## Building the Tool

The tool is built automatically as part of the ShaderManagement library:

```bash
cd VIXEN/ShaderManagement
mkdir build && cd build
cmake ..
make
```

The `shader_tool` executable will be in `build/bin/shader_tool`.

## Command Line Usage

### Compile Graphics Shader

```bash
shader_tool compile shader.vert shader.frag \
    --name MyShader \
    --output-dir ./generated
```

This will:
1. Compile `shader.vert` and `shader.frag` to SPIRV
2. Perform SPIRV reflection
3. Generate SDI header `./generated/sdi/MyShader_SDI.h`
4. Save bundle to `./generated/MyShader.json`

### Compile Compute Shader

```bash
shader_tool compile-compute compute.comp \
    --name MyCompute \
    --output-dir ./generated
```

### Build Central Registry

```bash
shader_tool build-registry \
    shader1.json shader2.json shader3.json \
    --output ./generated/SDI_Registry.h
```

This generates a central registry header that includes only the registered shaders, reducing compilation time.

### Batch Processing

Create a config file `shaders.json`:

```json
{
  "shaders": [
    {
      "name": "BasicShader",
      "stages": ["shaders/basic.vert", "shaders/basic.frag"],
      "pipeline": "graphics"
    },
    {
      "name": "ComputeShader",
      "stages": ["shaders/compute.comp"],
      "pipeline": "compute"
    },
    {
      "name": "MeshShader",
      "stages": ["shaders/mesh.mesh", "shaders/mesh.frag"],
      "pipeline": "mesh"
    }
  ],
  "buildRegistry": true
}
```

Then run:

```bash
shader_tool batch shaders.json --output-dir ./generated
```

This will compile all shaders and generate the central registry.

## CMake Integration

The recommended way to use the shader tool is through CMake functions.

### Setup

Include the utilities in your `CMakeLists.txt`:

```cmake
# Find ShaderManagement
find_package(ShaderManagement REQUIRED)

# Include shader tool utilities
include(ShaderToolUtils)
```

### Example 1: Single Shader Bundle

```cmake
add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    VERTEX shaders/shader.vert
    FRAGMENT shaders/shader.frag
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders
)

# Add dependency to your target
add_executable(MyApp main.cpp)
add_dependencies(MyApp MyShader_Bundle)
```

### Example 2: Compute Shader

```cmake
add_shader_bundle(ComputeShader_Bundle
    PROGRAM_NAME "ComputeShader"
    COMPUTE shaders/compute.comp
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders
)
```

### Example 3: Central Registry

```cmake
# Create multiple shader bundles
add_shader_bundle(Shader1_Bundle
    PROGRAM_NAME "Shader1"
    VERTEX shaders/s1.vert
    FRAGMENT shaders/s1.frag
)

add_shader_bundle(Shader2_Bundle
    PROGRAM_NAME "Shader2"
    VERTEX shaders/s2.vert
    FRAGMENT shaders/s2.frag
)

# Build central registry from all bundles
add_shader_registry(ShaderRegistry
    BUNDLES Shader1_Bundle Shader2_Bundle
    OUTPUT ${CMAKE_BINARY_DIR}/generated/SDI_Registry.h
)

# Add registry dependency to your target
add_executable(MyApp main.cpp)
add_dependencies(MyApp ShaderRegistry)

# Include generated headers
target_include_directories(MyApp PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)
```

Now in your C++ code:

```cpp
#include <SDI_Registry.h>  // Includes all registered shader SDIs

// Type-safe descriptor access
auto binding = SDI::Shader1::Descriptors::MainTexture;
// binding.set = 0
// binding.binding = 1
// binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
```

### Example 4: Batch Processing

```cmake
add_shader_batch(AllShaders
    CONFIG shaders.json
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders
)
```

## Advanced Options

### Custom SDI Namespace

```cmake
add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    VERTEX shader.vert
    FRAGMENT shader.frag
    SDI_NAMESPACE "CustomSDI"  # Default: "SDI"
)
```

### Disable SDI Generation

```cmake
add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    VERTEX shader.vert
    FRAGMENT shader.frag
    NO_SDI  # Don't generate SDI header
)
```

### Verbose Output

```cmake
add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    VERTEX shader.vert
    FRAGMENT shader.frag
    VERBOSE  # Print detailed compilation info
)
```

### Dependencies

If your shaders include other files, add them as dependencies:

```cmake
add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    VERTEX shader.vert
    FRAGMENT shader.frag
    DEPENDS common.glsl utilities.glsl
)
```

## Workflow Example

### Project Structure

```
MyProject/
├── CMakeLists.txt
├── src/
│   └── main.cpp
├── shaders/
│   ├── basic.vert
│   ├── basic.frag
│   ├── compute.comp
│   └── shaders.json
└── build/
    └── generated/
        ├── shaders/
        │   ├── BasicShader.json
        │   ├── ComputeShader.json
        │   └── ...
        └── sdi/
            ├── BasicShader_SDI.h
            ├── ComputeShader_SDI.h
            └── SDI_Registry.h
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyProject)

find_package(ShaderManagement REQUIRED)
include(ShaderToolUtils)

# Batch compile all shaders
add_shader_batch(AllShaders
    CONFIG shaders/shaders.json
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated
    VERBOSE
)

# Application
add_executable(MyApp src/main.cpp)
add_dependencies(MyApp AllShaders)

target_include_directories(MyApp PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(MyApp PRIVATE ShaderManagement)
```

### shaders/shaders.json

```json
{
  "shaders": [
    {
      "name": "BasicShader",
      "stages": ["shaders/basic.vert", "shaders/basic.frag"],
      "pipeline": "graphics"
    },
    {
      "name": "ComputeShader",
      "stages": ["shaders/compute.comp"],
      "pipeline": "compute"
    }
  ],
  "buildRegistry": true
}
```

### src/main.cpp

```cpp
#include <ShaderManagement/ShaderLibrary.h>
#include <SDI_Registry.h>
#include <iostream>

int main() {
    // SDI headers are pre-generated and available at compile time
    std::cout << "Basic Shader Descriptor Set: "
              << SDI::BasicShader::Descriptors::MainTexture.set << "\n";

    // Load pre-compiled shader bundles
    ShaderDataBundle basicShader = LoadBundleFromFile("generated/shaders/BasicShader.json");

    // Use in renderer...
    return 0;
}
```

### Build Process

```bash
mkdir build && cd build
cmake ..
make

# Output:
# [1/5] Compiling shader bundle: BasicShader
#   Compile time: 45ms
#   Reflect time: 12ms
#   SDI gen time: 8ms
#   Total time: 65ms
# [2/5] Compiling shader bundle: ComputeShader
#   ...
# [3/5] Building shader registry: ./generated/SDI_Registry.h
#   Registered: BasicShader (UUID: ...)
#   Registered: ComputeShader (UUID: ...)
#   Total shaders registered: 2
# [4/5] Building MyApp
# [5/5] Linking MyApp

./MyApp
```

## Benefits

### Development Time

1. **Immediate Error Detection** - Shader errors caught during build, not at runtime
2. **IDE Autocomplete** - SDI headers provide full IntelliSense for shader descriptors
3. **Type Safety** - Compile-time validation that shader bindings match C++ code
4. **Fast Iteration** - Only recompile changed shaders, not entire project

### Runtime Performance

1. **Faster Startup** - No shader compilation on app launch
2. **Smaller Binaries** - Shader compiler not embedded in application
3. **Pre-validated SPIRV** - Shaders already validated during build
4. **Optimized Registry** - Only includes active shaders, reducing compilation overhead

### Production Benefits

1. **Reproducible Builds** - Shaders compiled with exact tool version
2. **Build Artifacts** - SPIRV and SDI headers can be archived/versioned
3. **CI/CD Integration** - Shader errors fail the build pipeline
4. **No Runtime Dependencies** - No need for glslc/glslangValidator at runtime

## Troubleshooting

### shader_tool not found

Make sure `shader_tool` is built:

```bash
cd VIXEN/ShaderManagement
mkdir build && cd build
cmake ..
make shader_tool
```

Add to PATH or set explicitly:

```cmake
set(SHADER_TOOL_EXECUTABLE "/path/to/shader_tool")
```

### Shaders not recompiling

Clean the build directory or touch the shader source files:

```bash
touch shaders/*.vert shaders/*.frag
make
```

### Include path issues

Make sure generated directories are added to include path:

```cmake
target_include_directories(MyApp PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_BINARY_DIR}/generated/sdi
)
```

## See Also

- [ShaderManagement Library Documentation](../README.md)
- [SPIRV Descriptor Interface (SDI) Guide](../docs/SDI_Guide.md)
- [Hot-Reload System](../docs/HotReload.md)
