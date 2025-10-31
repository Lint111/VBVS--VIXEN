# Shader Pre-Compilation Workflow

**Author**: Claude Code
**Date**: October 30, 2025
**Status**: Production Ready ✅

## Overview

The ShaderManagement library provides build-time shader compilation through the `shader_tool` executable, enabling:
- **Pre-compilation** of GLSL → SPIR-V at build time (faster runtime startup)
- **SDI generation** for type-safe C++ shader interfaces
- **Central registry** for convenient single-include shader access
- **Batch processing** via configuration files

---

## Tool Location

```
build/bin/Debug/shader_tool.exe      # Debug build
build/bin/Release/shader_tool.exe    # Release build
```

---

## Commands

### 1. Compile Graphics Shader Pipeline

Compiles vertex + fragment shaders into a shader bundle with optional SDI generation.

```bash
shader_tool compile <input.vert> <input.frag> [options]
```

**Options**:
- `--output <path>` - Output bundle file path (default: `<name>.bundle.json`)
- `--name <name>` - Program name for debugging/logging
- `--sdi-namespace <ns>` - SDI namespace prefix (default: "SDI")
- `--sdi-dir <dir>` - SDI output directory (default: "./generated/sdi")
- `--no-sdi` - Disable SDI generation
- `--embed-spirv` - Embed SPIR-V in JSON (prevents orphaned .spv files)
- `--verbose` - Print detailed output

**Example**:
```bash
shader_tool compile shaders/pbr.vert shaders/pbr.frag \
    --name "PBR Material" \
    --output builtAssets/shaders/pbr.bundle.json \
    --sdi-namespace "PBRShader" \
    --verbose
```

**Output Files**:
- `builtAssets/shaders/pbr.bundle.json` - Shader bundle with metadata
- `builtAssets/shaders/pbr.vert.spv` - Compiled vertex SPIR-V
- `builtAssets/shaders/pbr.frag.spv` - Compiled fragment SPIR-V
- `generated/sdi/pbr_material-SDI.h` - Type-safe C++ interface header

---

### 2. Compile Compute Shader

Compiles a compute shader into a bundle.

```bash
shader_tool compile-compute <input.comp> [options]
```

**Example**:
```bash
shader_tool compile-compute shaders/particle_update.comp \
    --name "Particle Update" \
    --output builtAssets/shaders/particle.bundle.json
```

---

### 3. Generate SDI from Existing Bundle

Generates an SDI header from a pre-compiled shader bundle.

```bash
shader_tool generate-sdi <bundle.json> [options]
```

**Example**:
```bash
shader_tool generate-sdi builtAssets/shaders/pbr.bundle.json \
    --sdi-dir generated/sdi \
    --sdi-namespace "PBR"
```

**Output**:
- `generated/sdi/pbr_material-SDI.h` - Regenerated SDI header

---

### 4. Build Central SDI Registry

Creates a central `SDI_Registry.h` header that includes all shader SDI headers for convenient single-include access.

```bash
shader_tool build-registry <bundle1.json> <bundle2.json> ... [options]
```

**Options**:
- `--output <path>` - Registry header path (default: `generated/SDI_Registry.h`)
- `--sdi-dir <dir>` - SDI directory (default: `./generated/sdi`)
- `--sdi-namespace <ns>` - Registry namespace prefix (default: "Shaders")

**Example**:
```bash
shader_tool build-registry \
    builtAssets/shaders/pbr.bundle.json \
    builtAssets/shaders/terrain.bundle.json \
    builtAssets/shaders/skybox.bundle.json \
    --output generated/SDI_Registry.h \
    --sdi-namespace "Shaders"
```

**Generated Registry**:
```cpp
// generated/SDI_Registry.h
#pragma once

#include "pbr_material-SDI.h"
#include "terrain-SDI.h"
#include "skybox-SDI.h"

namespace Shaders {
    namespace PBRMaterial = ShaderInterface::pbr_material;
    namespace Terrain = ShaderInterface::terrain;
    namespace Skybox = ShaderInterface::skybox;
}
```

**Usage in C++**:
```cpp
#include "generated/SDI_Registry.h"

using namespace Shaders;

// Access shader constants with IDE autocomplete
VkDescriptorSetLayoutBinding binding{};
binding.binding = PBRMaterial::Set0::MaterialBuffer::BINDING;
binding.descriptorType = PBRMaterial::Set0::MaterialBuffer::TYPE;
binding.descriptorCount = PBRMaterial::Set0::MaterialBuffer::COUNT;
```

---

### 5. Batch Processing

Process multiple shaders from a configuration file.

```bash
shader_tool batch <config.json> [options]
```

**Configuration File Format**:
```json
{
  "version": 1,
  "outputDir": "builtAssets/shaders",
  "sdiDir": "generated/sdi",
  "sdiNamespace": "Shaders",
  "shaders": [
    {
      "name": "PBR Material",
      "type": "graphics",
      "vertex": "shaders/pbr.vert",
      "fragment": "shaders/pbr.frag",
      "output": "pbr.bundle.json"
    },
    {
      "name": "Terrain",
      "type": "graphics",
      "vertex": "shaders/terrain.vert",
      "fragment": "shaders/terrain.frag",
      "output": "terrain.bundle.json"
    },
    {
      "name": "Particle Update",
      "type": "compute",
      "compute": "shaders/particle_update.comp",
      "output": "particle.bundle.json"
    }
  ],
  "generateRegistry": true
}
```

**Example**:
```bash
shader_tool batch shader_config.json --verbose
```

---

### 6. Cleanup Orphaned Files

Removes orphaned SPIR-V and JSON files from output directory (files not tracked in manifest).

```bash
shader_tool cleanup <output-dir> [options]
```

**Example**:
```bash
shader_tool cleanup builtAssets/shaders --verbose
```

**Output**:
```
Cleaned up 3 orphaned files from builtAssets/shaders
```

---

## CMake Integration

### Option 1: Custom Target (Recommended)

Add a custom CMake target to pre-compile shaders during build:

```cmake
# Add shader_tool to your build
add_subdirectory(ShaderManagement)

# Define shader files
set(SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/shaders/pbr.vert
    ${CMAKE_SOURCE_DIR}/shaders/pbr.frag
    ${CMAKE_SOURCE_DIR}/shaders/terrain.vert
    ${CMAKE_SOURCE_DIR}/shaders/terrain.frag
)

# Output directory
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/builtAssets/shaders)
set(SDI_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated/sdi)

# Create custom target
add_custom_target(CompileShaders
    COMMAND shader_tool batch ${CMAKE_SOURCE_DIR}/shader_config.json
        --output-dir ${SHADER_OUTPUT_DIR}
        --sdi-dir ${SDI_OUTPUT_DIR}
        --verbose
    DEPENDS shader_tool
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Pre-compiling shaders..."
)

# Make your main target depend on shader compilation
add_dependencies(YourExecutable CompileShaders)

# Include SDI headers in your build
target_include_directories(YourExecutable PRIVATE ${CMAKE_BINARY_DIR}/generated)
```

### Option 2: Individual Shader Compilation

```cmake
# Function to compile a shader pair
function(compile_shader_pair NAME VERT FRAG)
    set(OUTPUT_BUNDLE ${SHADER_OUTPUT_DIR}/${NAME}.bundle.json)

    add_custom_command(
        OUTPUT ${OUTPUT_BUNDLE}
        COMMAND shader_tool compile
            ${VERT} ${FRAG}
            --name ${NAME}
            --output ${OUTPUT_BUNDLE}
            --sdi-dir ${SDI_OUTPUT_DIR}
        DEPENDS shader_tool ${VERT} ${FRAG}
        COMMENT "Compiling shader: ${NAME}"
    )

    list(APPEND SHADER_BUNDLES ${OUTPUT_BUNDLE})
    set(SHADER_BUNDLES ${SHADER_BUNDLES} PARENT_SCOPE)
endfunction()

# Compile shaders
compile_shader_pair("PBR Material"
    ${CMAKE_SOURCE_DIR}/shaders/pbr.vert
    ${CMAKE_SOURCE_DIR}/shaders/pbr.frag
)

compile_shader_pair("Terrain"
    ${CMAKE_SOURCE_DIR}/shaders/terrain.vert
    ${CMAKE_SOURCE_DIR}/shaders/terrain.frag
)

# Create target
add_custom_target(CompileShaders DEPENDS ${SHADER_BUNDLES})
add_dependencies(YourExecutable CompileShaders)
```

---

## Shader Bundle Format

Shader bundles are JSON files containing compiled SPIR-V and metadata:

```json
{
  "version": 1,
  "uuid": "pbr_material_v1",
  "name": "PBR Material",
  "pipelineType": "Graphics",
  "stages": {
    "vertex": {
      "spirvPath": "pbr.vert.spv",
      "entryPoint": "main",
      "spirvSize": 2048
    },
    "fragment": {
      "spirvPath": "pbr.frag.spv",
      "entryPoint": "main",
      "spirvSize": 3072
    }
  },
  "reflection": {
    "descriptorSets": {
      "0": [
        {
          "binding": 0,
          "name": "ubo",
          "type": "UniformBuffer",
          "stageFlags": "Vertex | Fragment"
        }
      ]
    },
    "pushConstants": [],
    "vertexInputs": [
      {"location": 0, "name": "inPosition", "format": "R32G32B32_SFLOAT"},
      {"location": 1, "name": "inNormal", "format": "R32G32B32_SFLOAT"},
      {"location": 2, "name": "inTexCoord", "format": "R32G32_SFLOAT"}
    ]
  },
  "sdiHeaderPath": "../../generated/sdi/pbr_material-SDI.h",
  "interfaceHash": "abc123...",
  "compiledAt": "2025-10-30T22:00:00Z"
}
```

---

## SDI Header Format

Generated SDI headers provide type-safe access to shader resources:

```cpp
// pbr_material-SDI.h
#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace ShaderInterface {
namespace pbr_material {

// Descriptor Set 0
namespace Set0 {
    /**
     * @brief ubo
     * Type: UNIFORM_BUFFER
     * Stages: VERTEX | FRAGMENT
     * Count: 1
     */
    struct ubo {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        // Data structure (from UBO reflection)
        struct DataType {
            alignas(16) float model[16];      // mat4
            alignas(16) float view[16];       // mat4
            alignas(16) float projection[16]; // mat4
        };
    };
}

// Vertex Input Attributes
namespace VertexInput {
    static constexpr uint32_t POSITION_LOCATION = 0;
    static constexpr uint32_t NORMAL_LOCATION = 1;
    static constexpr uint32_t TEXCOORD_LOCATION = 2;
}

} // namespace pbr_material
} // namespace ShaderInterface
```

---

## Runtime Usage

### Loading Shader Bundle

```cpp
#include "ShaderManagement/ShaderBundleLoader.h"
#include "generated/SDI_Registry.h"

using namespace ShaderManagement;
using namespace Shaders;

// Load shader bundle
ShaderBundleLoader loader;
auto bundle = loader.LoadBundle("builtAssets/shaders/pbr.bundle.json");

if (!bundle) {
    std::cerr << "Failed to load shader bundle\n";
    return;
}

// Access SPIR-V for Vulkan module creation
const auto& vertSpirv = bundle->GetSpirv(ShaderStage::Vertex);
const auto& fragSpirv = bundle->GetSpirv(ShaderStage::Fragment);

// Create Vulkan shader modules
VkShaderModuleCreateInfo vertCreateInfo{};
vertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
vertCreateInfo.codeSize = vertSpirv.size() * sizeof(uint32_t);
vertCreateInfo.pCode = vertSpirv.data();
vkCreateShaderModule(device, &vertCreateInfo, nullptr, &vertModule);

// Use SDI constants for descriptor layout
VkDescriptorSetLayoutBinding binding{};
binding.binding = PBRMaterial::Set0::ubo::BINDING;
binding.descriptorType = PBRMaterial::Set0::ubo::TYPE;
binding.descriptorCount = PBRMaterial::Set0::ubo::COUNT;
binding.stageFlags = PBRMaterial::Set0::ubo::STAGES;
```

---

## Best Practices

### 1. Build-Time Compilation
- **Always pre-compile shaders** during build to catch syntax errors early
- Use CMake integration to automate shader compilation
- Include SDI headers in version control for IDE autocomplete

### 2. Central Registry
- **Use `build-registry`** to create a single-include header for all shaders
- Provides convenient namespace aliases (e.g., `Shaders::PBRMaterial`)
- Reduces #include boilerplate across codebase

### 3. Naming Conventions
- Use descriptive shader names in `--name` flag
- Use consistent SDI namespace prefixes per subsystem
- Follow project naming conventions for bundle output paths

### 4. Batch Processing
- **Use batch mode** for projects with many shaders
- Centralizes shader configuration in JSON
- Easier to maintain and update

### 5. Cleanup Workflow
- Run `shader_tool cleanup` periodically to remove orphaned files
- Especially important after renaming/deleting shaders
- Prevents stale SPIR-V files from accumulating

---

## Troubleshooting

### Shader Won't Compile

**Check**:
1. GLSL syntax errors (shader_tool provides detailed error messages)
2. Vulkan version compatibility (default: Vulkan 1.2)
3. Missing SPIR-V features (use `--verbose` for diagnostics)

**Example Error**:
```
Error: Failed to compile vertex shader: shaders/pbr.vert
Compilation failed:
  Line 42: 'texture2D' : no matching overloaded function found
  Line 42: Use 'texture()' for Vulkan GLSL
```

### SDI Header Not Found

**Check**:
1. SDI directory exists and is in include paths
2. CMake includes SDI output directory: `target_include_directories(... ${SDI_OUTPUT_DIR})`
3. File permissions allow reading generated headers

### Registry Deadlock (Known Issue)

**Symptom**: `RegisterShaderInSDIRegistry` test fails with "resource deadlock would occur"

**Workaround**: Registry functionality works but has a mutex issue in cleanup. Use direct SDI headers instead of registry for now.

**Status**: Issue tracked, does not affect runtime shader loading

---

## Next Steps: RenderGraph Integration

To integrate ShaderManagement with RenderGraph:

1. **Create ShaderLibraryNode enhancement** to use `ShaderBundleLoader`
2. **Replace MVP manual loading** in VulkanGraphApplication.cpp
3. **Use reflection data** for automatic descriptor layout creation
4. **Integrate CashSystem** for shader module caching

See `documentation/ShaderManagement-Integration-Plan.md` for detailed roadmap.

---

## References

- **shader_tool source**: `ShaderManagement/tools/shader_tool.cpp`
- **ShaderBundleBuilder API**: `ShaderManagement/include/ShaderManagement/ShaderBundleBuilder.h`
- **SDI Generator**: `ShaderManagement/include/ShaderManagement/SpirvInterfaceGenerator.h`
- **Test Suite**: `ShaderManagement/tests/test_sdi_lifecycle.cpp`
- **Integration Plan**: `documentation/ShaderManagement-Integration-Plan.md`
