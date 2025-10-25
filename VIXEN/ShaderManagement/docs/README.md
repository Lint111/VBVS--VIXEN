# ShaderManagement Library

## Architecture Overview

The ShaderManagement library is a **device-agnostic** static library for shader compilation, caching, and management. It follows a clean separation of concerns with focused, single-responsibility classes.

### Design Principles

1. **Device-Agnostic**: No `VkShaderModule` or Vulkan device operations
2. **Pure Data Processing**: Works with SPIR-V bytecode and file I/O only
3. **Separation of Concerns**: Each class has one clear responsibility
4. **Thread-Safe**: All operations internally synchronized
5. **Composable**: Classes can be used independently or together

### Separation from RenderGraph

- **ShaderManagement** (this library): Compilation, caching, preprocessing
- **RenderGraph**: Vulkan object creation (`VkShaderModule`, pipelines, etc.)

## Core Classes

### Data Structures (ShaderProgram.h)

Pure data - no logic, just definitions and results:

- **ShaderStageDefinition**: Input specification for a single shader stage
- **ShaderProgramDefinition**: Collection of stages forming a complete program
- **CompiledShaderStage**: Output SPIR-V bytecode for one stage
- **CompiledProgram**: Complete compiled program with all stages

### Manager (ShaderLibrary.h)

Central coordinator for shader programs:

- Registers and manages shader programs
- Coordinates compilation (sync/async)
- Hot reload support with swap policies
- File watching for automatic recompilation
- **Does NOT create Vulkan objects**

### New Utility Classes

#### ShaderCacheManager

**Responsibility**: Persistent caching of compiled SPIR-V to disk

```cpp
// Example usage
ShaderCacheManager cache;
auto spirv = cache.Lookup("shader_hash_key");
if (!spirv) {
    spirv = CompileShader(source);
    cache.Store("shader_hash_key", *spirv);
}
```

**Features**:
- Content-addressable storage (hash-based keys)
- LRU eviction policy
- Size limits
- Cache validation
- Hit/miss statistics

**Why separate?** Caching is orthogonal to compilation - can cache any SPIR-V regardless of source.

---

#### ShaderPreprocessor

**Responsibility**: GLSL source code preprocessing

```cpp
// Example usage
ShaderPreprocessor preprocessor;
preprocessor.AddIncludePath("./shaders/common");
preprocessor.AddGlobalDefine("VULKAN", "1");

auto result = preprocessor.Preprocess(source, {
    {"MAX_LIGHTS", "16"},
    {"USE_PBR", ""}
});
```

**Features**:
- Preprocessor define injection
- `#include` directive resolution
- Circular include prevention
- Include path searching
- Line directive generation (for debugging)

**Why separate?** Preprocessing is pure string manipulation, independent of compilation.

---

#### ShaderCompiler

**Responsibility**: GLSL to SPIR-V compilation using glslang

```cpp
// Example usage
ShaderCompiler compiler;
auto result = compiler.Compile(
    ShaderStage::Fragment,
    preprocessedSource,
    "main",
    CompilationOptions{.optimizePerformance = true}
);

if (result) {
    // Use result.spirv
}
```

**Features**:
- GLSL → SPIR-V compilation (glslang)
- SPIR-V validation
- SPIR-V disassembly
- Configurable optimization levels
- Debug info generation
- Proper error reporting with line numbers

**Why separate?** Compiler is a pure function: source → SPIR-V. No state needed.

---

## ShaderDataBundle - Unified Shader Package

### Overview

**ShaderDataBundle** is a complete, self-contained package that includes everything needed to work with a shader:

- ✅ Compiled SPIRV bytecode
- ✅ Full reflection metadata (descriptors, push constants, vertex inputs)
- ✅ Descriptor layout specifications
- ✅ Generated SDI header reference
- ✅ Unique identifier (UUID)
- ✅ Validation and debugging utilities

**Purpose**: Instead of juggling multiple separate pieces (SPIRV, reflection data, SDI path, etc.), you get one cohesive bundle with convenient accessors.

### Creating Bundles with ShaderBundleBuilder

**ShaderBundleBuilder** orchestrates the entire pipeline:

```cpp
// Simple example: vertex + fragment shader
auto result = ShaderBundleBuilder()
    .SetProgramName("MyMaterial")
    .SetPipelineType(PipelineTypeConstraint::Graphics)
    .AddStage(ShaderStage::Vertex, vertexSource)
    .AddStage(ShaderStage::Fragment, fragmentSource)
    .Build();

if (result) {
    ShaderDataBundle bundle = *result;
    // Now have everything in one place!
}
```

**With all features enabled:**

```cpp
ShaderPreprocessor preprocessor;
preprocessor.AddIncludePath("./shaders/common");

ShaderCacheManager cache;
cache.Initialize({"./shader_cache"});

SdiGeneratorConfig sdiConfig{
    .outputDirectory = "./generated/sdi",
    .namespacePrefix = "ShaderInterface",
    .generateComments = true
};

auto result = ShaderBundleBuilder()
    .SetProgramName("PBRMaterial")
    .SetPipelineType(PipelineTypeConstraint::Graphics)

    // Add stages
    .AddStage(ShaderStage::Vertex, vertexSource)
    .AddStage(ShaderStage::Fragment, fragmentSource)

    // Enable preprocessing for includes/defines
    .EnablePreprocessing(&preprocessor)

    // Enable caching for faster rebuilds
    .EnableCaching(&cache)

    // Configure SDI generation
    .SetSdiConfig(sdiConfig)

    .Build();

if (!result) {
    std::cerr << "Build failed: " << result.errorMessage << "\n";
    return;
}

// Success! Get the bundle
ShaderDataBundle bundle = *result;

// Build statistics
std::cout << "Compile time: " << result.compileTime.count() << "ms\n";
std::cout << "Reflect time: " << result.reflectTime.count() << "ms\n";
std::cout << "SDI gen time: " << result.sdiGenTime.count() << "ms\n";
std::cout << "Used cache: " << (result.usedCache ? "Yes" : "No") << "\n";
```

### Using ShaderDataBundle

**1. Access SPIRV for Vulkan:**

```cpp
// Get SPIRV bytecode for creating VkShaderModule
const auto& vertSpirv = bundle.GetSpirv(ShaderStage::Vertex);
const auto& fragSpirv = bundle.GetSpirv(ShaderStage::Fragment);

// Create Vulkan shader modules (RenderGraph side)
VkShaderModuleCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
createInfo.codeSize = vertSpirv.size() * sizeof(uint32_t);
createInfo.pCode = vertSpirv.data();
vkCreateShaderModule(device, &createInfo, nullptr, &vertModule);
```

**2. Include SDI header in C++ code:**

```cpp
// In your C++ source file:
#include "generated/sdi/abc123-SDI.h"  // bundle.GetSdiIncludePath()

// Use type-safe constants
using namespace ShaderInterface::abc123;  // bundle.GetSdiNamespace()

VkDescriptorSetLayoutBinding binding{};
binding.binding = Set0::MaterialBuffer::BINDING;        // No magic numbers!
binding.descriptorType = Set0::MaterialBuffer::TYPE;    // Type-safe!
binding.stageFlags = Set0::MaterialBuffer::STAGES;      // Matches shader!
binding.descriptorCount = Set0::MaterialBuffer::COUNT;
```

**3. Access descriptor metadata:**

```cpp
// Get all bindings for descriptor set 0
auto set0Bindings = bundle.GetDescriptorSet(0);

for (const auto& binding : set0Bindings) {
    std::cout << "Binding " << binding.binding
              << ": " << binding.name
              << " (type: " << binding.descriptorType << ")\n";
}

// Get push constants
const auto& pushConstants = bundle.GetPushConstants();
for (const auto& pc : pushConstants) {
    std::cout << "Push constant: " << pc.name
              << " (size: " << pc.size << " bytes)\n";
}

// Get vertex inputs (for vertex shaders)
const auto& vertexInputs = bundle.GetVertexInputs();
for (const auto& input : vertexInputs) {
    std::cout << "Vertex input " << input.location
              << ": " << input.name << "\n";
}
```

**4. Validate interface during hot-reload:**

```cpp
// Shader file changed, recompile
auto newResult = ShaderBundleBuilder()
    .SetProgramName("PBRMaterial")
    .AddStageFromFile(ShaderStage::Fragment, "material.frag")
    .Build();

if (newResult) {
    ShaderDataBundle newBundle = *newResult;

    // Check if interface is compatible
    if (newBundle.ValidateInterface(oldBundle.GetInterfaceHash())) {
        // Interface unchanged - safe to hot-swap
        SwapShaderModule(newBundle.GetSpirv(ShaderStage::Fragment));
    } else {
        // Interface changed - need full reload
        std::cerr << "WARNING: Shader interface changed!\n";
        std::cerr << "Regenerate SDI and rebuild C++ code.\n";
    }
}
```

**5. Debug information:**

```cpp
// Print comprehensive debug info
std::cout << bundle.GetDebugInfo();

// Output:
// ShaderDataBundle 'PBRMaterial'
//   UUID: abc123def456
//   Pipeline Type: Graphics
//   Stages: 2
//     - Vertex (1024 words)
//     - Fragment (2048 words)
//   Descriptor Sets: 1
//     Set 0: 3 bindings
//   SDI: Generated
//   SDI Path: ./generated/sdi/abc123def456-SDI.h
//   Interface Hash: a3f5d8e2b1c4f6a...
//   Age: 5234ms
```

### ShaderBundleBuilder Options

**Loading from files:**

```cpp
auto result = ShaderBundleBuilder()
    .SetProgramName("TerrainShader")
    .AddStageFromFile(ShaderStage::Vertex, "terrain.vert")
    .AddStageFromFile(ShaderStage::Fragment, "terrain.frag")
    .Build();
```

**Per-stage defines:**

```cpp
auto result = ShaderBundleBuilder()
    .AddStage(ShaderStage::Fragment, fragmentSource)
    .SetStageDefines(ShaderStage::Fragment, {
        {"MAX_LIGHTS", "16"},
        {"USE_SHADOWS", "1"},
        {"ENABLE_PBR", ""}
    })
    .Build();
```

**Custom compilation options:**

```cpp
CompilationOptions debugOptions{
    .optimizePerformance = false,
    .optimizeSize = false,
    .generateDebugInfo = true
};

auto result = ShaderBundleBuilder()
    .AddStage(ShaderStage::Fragment, source, "main", debugOptions)
    .Build();
```

**Building from pre-compiled SPIRV:**

```cpp
// You already have compiled SPIRV, just need reflection + SDI
std::vector<uint32_t> preCompiledSpirv = LoadSpirv("shader.spv");

CompiledProgram program;
program.name = "PrecompiledShader";
program.pipelineType = PipelineTypeConstraint::Graphics;
// ... populate program.stages with SPIRV ...

auto result = ShaderBundleBuilder()
    .BuildFromCompiled(program);
```

### Bundle Lifecycle

```cpp
// Create bundle
auto result = ShaderBundleBuilder()...Build();
ShaderDataBundle bundle = *result;

// Use bundle for entire shader lifetime
CreateVulkanPipeline(bundle);
UpdateUniforms(bundle);
RenderWithShader(bundle);

// Hot-reload: create new bundle
auto newResult = ShaderBundleBuilder()...Build();
if (newResult && IsCompatible(bundle, *newResult)) {
    bundle = *newResult;  // Swap to new version
}

// Bundle automatically cleans up when destroyed
// (SDI files remain on disk for C++ inclusion)
```

### Benefits Summary

| Feature | Without Bundle | With Bundle |
|---------|----------------|-------------|
| **SPIRV Access** | Separate CompiledProgram | `bundle.GetSpirv()` |
| **Descriptor Info** | Manual reflection calls | `bundle.GetDescriptorSet()` |
| **SDI Path** | Track separately | `bundle.GetSdiIncludePath()` |
| **Validation** | Manual hash comparison | `bundle.ValidateInterface()` |
| **Debug Info** | Assemble manually | `bundle.GetDebugInfo()` |
| **Type Safety** | Manual constants | Include SDI header |

**Result**: One unified interface for all shader-related operations!

---

## Typical Workflow

### 1. Modern Approach: ShaderDataBundle (Recommended)

```cpp
// Setup (once)
ShaderPreprocessor preprocessor;
preprocessor.AddIncludePath("./shaders/include");

ShaderCacheManager cache;
SdiGeneratorConfig sdiConfig{.outputDirectory = "./generated/sdi"};

// Build shader bundle
auto result = ShaderBundleBuilder()
    .SetProgramName("PBRShader")
    .AddStageFromFile(ShaderStage::Vertex, "pbr.vert")
    .AddStageFromFile(ShaderStage::Fragment, "pbr.frag")
    .EnablePreprocessing(&preprocessor)
    .EnableCaching(&cache)
    .SetSdiConfig(sdiConfig)
    .Build();

if (!result) {
    std::cerr << "Build failed: " << result.errorMessage << "\n";
    return;
}

// Use bundle
ShaderDataBundle bundle = *result;

// In C++ code, include the generated SDI header:
// #include "bundle.GetSdiIncludePath()"
// Now have type-safe access to all shader resources!
```

### 2. Manual Approach: Individual Components

```cpp
// Preprocess
ShaderPreprocessor preprocessor;
preprocessor.AddIncludePath("./shaders/include");
auto preprocessed = preprocessor.Preprocess(source, defines);

// Check cache
ShaderCacheManager cache;
auto cacheKey = GenerateCacheKey(preprocessed.processedSource, ...);
auto spirv = cache.Lookup(cacheKey);

if (!spirv) {
    // Compile
    ShaderCompiler compiler;
    auto result = compiler.Compile(stage, preprocessed.processedSource);
    spirv = result.spirv;

    // Store in cache
    cache.Store(cacheKey, spirv);
}

// Use spirv in RenderGraph to create VkShaderModule
```

### 3. Legacy Approach: With ShaderLibrary (Managed)

```cpp
ShaderLibrary library;

// Register program
ShaderProgramDefinition def;
def.name = "MainShader";
def.pipelineType = PipelineTypeConstraint::Graphics;
def.stages.push_back({
    .stage = ShaderStage::Vertex,
    .spirvPath = "shaders/main.vert.spv"
});
def.stages.push_back({
    .stage = ShaderStage::Fragment,
    .spirvPath = "shaders/main.frag.spv"
});

uint32_t progId = library.RegisterProgram(def);

// Compile
library.CompileProgram(progId);

// Get result
const CompiledProgram* compiled = library.GetCompiledProgram(progId);

// Use compiled->stages[i].spirvCode in RenderGraph
```

## Integration with RenderGraph

The RenderGraph side creates Vulkan objects from ShaderManagement outputs:

```cpp
// In RenderGraph/ShaderLibraryNode.cpp
void ShaderLibraryNode::CreateVulkanModules(const CompiledProgram& program) {
    for (const auto& stage : program.stages) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = stage.spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = stage.spirvCode.data();

        VkShaderModule module;
        vkCreateShaderModule(device, &createInfo, nullptr, &module);
        // Store module...
    }
}
```

## Class Dependency Graph

```
ShaderLibrary
    ├── Uses: ShaderPreprocessor
    ├── Uses: ShaderCompiler
    ├── Uses: ShaderCacheManager
    └── Uses: SPIRVReflection

ShaderPreprocessor
    └── No dependencies (pure string processing)

ShaderCompiler
    └── Uses: glslang (external)

ShaderCacheManager
    └── No dependencies (file I/O only)
```

## Benefits of This Architecture

1. **Testability**: Each class can be unit tested independently
2. **Reusability**: Use ShaderCompiler without caching, or caching without preprocessing
3. **Maintainability**: Clear responsibilities, easy to locate bugs
4. **Performance**: Can optimize each component separately
5. **Flexibility**: Easy to swap implementations (e.g., different cache strategies)

## Example: Custom Build Pipeline

```cpp
// Custom shader build tool using library components
void BuildShaders(const std::vector<ShaderSource>& sources) {
    ShaderPreprocessor preprocessor;
    ShaderCompiler compiler;
    ShaderCacheManager cache;

    for (const auto& src : sources) {
        // Preprocess
        auto preprocessed = preprocessor.PreprocessFile(src.path, src.defines);

        // Generate cache key
        auto key = GenerateCacheKey(preprocessed.processedSource, ...);

        // Skip if cached
        if (cache.Contains(key)) {
            std::cout << "Skipping " << src.path << " (cached)\n";
            continue;
        }

        // Compile
        auto result = compiler.Compile(src.stage, preprocessed.processedSource);

        if (!result) {
            std::cerr << "Failed: " << src.path << "\n";
            std::cerr << result.errorLog << "\n";
            continue;
        }

        // Cache
        cache.Store(key, result.spirv);

        // Save to disk
        SaveSpirv(src.outputPath, result.spirv);
    }
}
```

## Thread Safety

All classes are thread-safe:

```cpp
// Safe to compile multiple shaders in parallel
std::vector<std::thread> threads;
ShaderCompiler compiler;  // Shared instance

for (const auto& shader : shaders) {
    threads.emplace_back([&]() {
        auto result = compiler.Compile(...);  // Thread-safe
    });
}
```

## SPIRV Descriptor Interface (SDI) Code Generation

### Overview

The SDI system generates C++ header files from SPIRV reflection data, providing **compile-time type safety** when accessing shader resources.

### Components

#### SpirvReflector

**Responsibility**: Comprehensive SPIRV metadata extraction

```cpp
// Example usage
auto reflectionData = SpirvReflector::Reflect(compiledProgram);

// Access descriptor bindings with full type information
for (const auto& [setIndex, bindings] : reflectionData->descriptorSets) {
    for (const auto& binding : bindings) {
        std::cout << "Set " << setIndex
                  << ", Binding " << binding.binding
                  << ": " << binding.name << "\n";
    }
}

// Compute interface hash for validation
std::string hash = SpirvReflector::ComputeInterfaceHash(compiledProgram);
```

**Features**:
- Descriptor binding extraction (sets, bindings, types, names)
- Push constant reflection with struct layouts
- Vertex input/output analysis
- Struct definition extraction
- Specialization constant detection
- SHA-256 interface hashing
- Stage I/O analysis

---

#### SpirvInterfaceGenerator

**Responsibility**: Generate strongly-typed C++ header files from reflection data

```cpp
// Example usage
SdiGeneratorConfig config{
    .outputDirectory = "./generated/sdi",
    .namespacePrefix = "ShaderInterface",
    .generateComments = true,
    .generateLayoutInfo = true
};

SpirvInterfaceGenerator generator(config);

// Generate UUID-SDI.h header file
std::string uuid = "my_shader_uuid_123";
std::string filePath = generator.Generate(uuid, *reflectionData);

// Generated file: ./generated/sdi/my_shader_uuid_123-SDI.h
```

**Generated header structure**:

```cpp
namespace ShaderInterface {
namespace my_shader_uuid_123 {

// Struct definitions matching UBO/SSBO layouts
struct MaterialData {
    float roughness;      // Offset: 0 bytes
    float metallic;       // Offset: 4 bytes
    vec4 albedo;          // Offset: 16 bytes
};

// Descriptor bindings with compile-time constants
namespace Set0 {
    struct MaterialBuffer {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 0;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_FRAGMENT_BIT;
        using DataType = MaterialData;
    };

    struct AlbedoTexture {
        static constexpr uint32_t SET = 0;
        static constexpr uint32_t BINDING = 1;
        static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        static constexpr uint32_t COUNT = 1;
        static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_FRAGMENT_BIT;
    };
}

// Push constants
struct PushConstants {
    static constexpr uint32_t OFFSET = 0;
    static constexpr uint32_t SIZE = 64;
    static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_ALL;
    using DataType = TransformData;
};

// Metadata and validation
struct Metadata {
    static constexpr const char* PROGRAM_NAME = "PBRShader";
    static constexpr const char* INTERFACE_HASH = "a3f5d...";
    static constexpr uint32_t NUM_DESCRIPTOR_SETS = 1;
};

inline bool ValidateInterfaceHash(const char* runtimeHash) {
    return std::string(runtimeHash) == Metadata::INTERFACE_HASH;
}

} // namespace
} // namespace ShaderInterface
```

---

#### SdiFileManager

**Responsibility**: Lifecycle management of generated SDI files

```cpp
// Example usage
SdiFileManager manager("./generated/sdi");

// Register generated files
manager.RegisterSdi("shader_uuid_123", generatedPath);

// Cleanup orphaned files (removed shaders)
uint32_t orphansDeleted = manager.CleanupOrphans();

// Get all registered UUIDs
auto uuids = manager.GetRegisteredUuids();

// Unregister and delete
manager.UnregisterSdi("shader_uuid_123", true);
```

### Benefits of SDI

#### 1. IDE Autocompletion

```cpp
#include "my_shader_uuid_123-SDI.h"
using namespace ShaderInterface::my_shader_uuid_123;

// IDE knows Set0::MaterialBuffer exists and provides autocompletion
VkDescriptorSetLayoutBinding binding{};
binding.binding = Set0::MaterialBuffer::BINDING;  // Autocomplete!
binding.descriptorType = Set0::MaterialBuffer::TYPE;
binding.stageFlags = Set0::MaterialBuffer::STAGES;
```

#### 2. Compile-Time Type Safety

```cpp
// COMPILE ERROR if you use wrong binding number
binding.binding = 999;  // Works
binding.binding = Set0::MaterialBuffer::BINDING;  // Type-safe!

// Can't misspell or use wrong constant
binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;  // Wrong! (no compile error)
binding.descriptorType = Set0::MaterialBuffer::TYPE;  // Correct! (matches shader)
```

#### 3. Interface Validation

```cpp
// Validate at runtime that shader hasn't changed
auto runtimeReflection = SpirvReflector::Reflect(program);
if (!ShaderInterface::my_shader_uuid_123::ValidateInterfaceHash(
        runtimeReflection->interfaceHash.c_str())) {
    // ERROR: Shader interface has changed, regenerate SDI!
}
```

#### 4. Struct Layout Matching

```cpp
// C++ struct matches shader UBO layout EXACTLY
struct MaterialData {
    float roughness;
    float metallic;
    vec4 albedo;
};

// Fill data type-safely
MaterialData data{
    .roughness = 0.5f,
    .metallic = 0.8f,
    .albedo = {1.0f, 0.0f, 0.0f, 1.0f}
};

// Update uniform buffer (guaranteed to match shader)
UpdateUniformBuffer(buffer, &data, sizeof(data));
```

### Workflow Integration

#### Full Pipeline with SDI

```cpp
// 1. Compile shader
ShaderCompiler compiler;
auto compiled = compiler.Compile(ShaderStage::Fragment, source);

// 2. Reflect SPIRV
auto reflectionData = SpirvReflector::Reflect(*compiled);

// 3. Generate SDI header
SpirvInterfaceGenerator generator;
std::string uuid = GenerateUUID();
std::string sdiPath = generator.Generate(uuid, *reflectionData);

// 4. Include generated header in C++ code
// #include "generated/sdi/{uuid}-SDI.h"

// 5. Use type-safe constants
// binding.binding = Set0::MaterialBuffer::BINDING;
```

#### Hot Reload with Validation

```cpp
// Detect shader file change
if (ShaderFileChanged("material.frag")) {
    // Recompile
    auto newProgram = compiler.Compile(...);

    // Reflect new interface
    auto newReflection = SpirvReflector::Reflect(*newProgram);

    // Check if interface changed
    if (!AreInterfacesCompatible(*oldReflection, *newReflection)) {
        // Interface changed - regenerate SDI
        generator.Generate(uuid, *newReflection);

        // WARN: C++ code needs recompilation to use new interface
        std::cerr << "WARNING: Shader interface changed, rebuild C++ code!\n";
    } else {
        // Interface unchanged - hot reload is safe
        SwapShaderModule(newProgram);
    }
}
```

### Configuration Options

```cpp
SdiGeneratorConfig config{
    .outputDirectory = "./generated/sdi",        // Where to write files
    .namespacePrefix = "ShaderInterface",        // Namespace prefix
    .generateComments = true,                     // Include documentation
    .generateLayoutInfo = true,                   // Include memory layout
    .generateAccessorHelpers = false,             // Generate helper functions
    .prettyPrint = true                           // Format with indentation
};
```

### File Naming Convention

Generated files follow the pattern: `{UUID}-SDI.h`

- **UUID**: Unique identifier for the shader program (typically a hash or GUID)
- **SDI**: Suffix indicating "SPIRV Descriptor Interface"
- Example: `a3f5d8e2_b1c4_4f6a_9d2e_8c3b5a1f7e9d-SDI.h`

### Thread Safety

All SDI classes are thread-safe:

```cpp
// Safe to generate multiple SDI files in parallel
SpirvInterfaceGenerator generator(config);

std::vector<std::thread> threads;
for (const auto& [uuid, reflection] : shaderData) {
    threads.emplace_back([&]() {
        generator.Generate(uuid, *reflection);  // Thread-safe
    });
}
```

---

## Central SDI Registry - Single Include for All Shaders

### Overview

The **SdiRegistryManager** generates a central `SDI_Registry.h` header that includes **only registered/active shaders**. This provides:

- ✅ **Single include** for all shader interfaces
- ✅ **Convenient namespace aliases** for easy access
- ✅ **Reduced compilation time** - only includes active shaders
- ✅ **Runtime metadata** for shader enumeration

**Key Optimization**: The registry dynamically updates to include only currently registered shaders, avoiding compilation overhead from hundreds of unused shader interfaces.

### Generated Registry Structure

```cpp
// SDI_Registry.h (auto-generated)
#pragma once

// Include only active/registered shader SDI headers
#include "abc123-SDI.h"      // PBRShader
#include "def456-SDI.h"      // TerrainShader
#include "ghi789-SDI.h"      // WaterShader

// Convenient namespace aliases
namespace Shaders {
    namespace PBRShader = ShaderInterface::abc123;
    namespace TerrainShader = ShaderInterface::def456;
    namespace WaterShader = ShaderInterface::ghi789;
}

// Runtime metadata
namespace Shaders {
namespace Registry {
    struct ShaderInfo {
        const char* uuid;
        const char* name;
        const char* alias;
    };

    constexpr ShaderInfo SHADERS[] = {
        {"abc123", "PBRMaterial", "PBRShader"},
        {"def456", "TerrainRenderer", "TerrainShader"},
        {"ghi789", "WaterSurface", "WaterShader"}
    };

    constexpr size_t SHADER_COUNT = 3;
}
}
```

### Basic Usage

**1. Setup registry manager (once at startup):**

```cpp
SdiRegistryManager::Config config{
    .sdiDirectory = "./generated/sdi",
    .registryHeaderPath = "./generated/sdi/SDI_Registry.h",
    .registryNamespace = "Shaders",
    .generateAliases = true,
    .autoRegenerate = true  // Auto-update registry on changes
};

SdiRegistryManager registry(config);
```

**2. Build shaders with registry integration:**

```cpp
auto result = ShaderBundleBuilder()
    .SetProgramName("PBRMaterial")
    .AddStageFromFile(ShaderStage::Vertex, "pbr.vert")
    .AddStageFromFile(ShaderStage::Fragment, "pbr.frag")
    .EnableRegistryIntegration(&registry, "PBRShader")  // Auto-register!
    .Build();

// Shader is now in SDI_Registry.h
```

**3. Use in your C++ code:**

```cpp
// Single include for ALL registered shaders!
#include "generated/sdi/SDI_Registry.h"

using namespace Shaders;

// Access shader interfaces via convenient aliases
VkDescriptorSetLayoutBinding binding{};
binding.binding = PBRShader::Set0::MaterialBuffer::BINDING;
binding.descriptorType = PBRShader::Set0::MaterialBuffer::TYPE;

// Access another shader
binding.binding = TerrainShader::Set0::HeightMap::BINDING;
```

### Why This Matters: Compilation Time

**Without central registry:**
```cpp
// Must include each shader individually
#include "abc123-SDI.h"
#include "def456-SDI.h"
#include "ghi789-SDI.h"
#include "jkl012-SDI.h"
// ... 100 more shaders

// Compiles ALL headers even if you only use 3 shaders
// Compilation time: SLOW
```

**With central registry (smart filtering):**
```cpp
// Single include - only contains ACTIVE shaders
#include "generated/sdi/SDI_Registry.h"

// If you only registered 3 shaders out of 100 available,
// registry only includes those 3
// Compilation time: FAST
```

### Dynamic Registry Updates

The registry automatically updates when shaders are registered/unregistered:

```cpp
SdiRegistryManager registry;

// Register first shader
auto pbr = BuildShader("PBR");
registry.RegisterShader(...);
// SDI_Registry.h now includes: PBRShader

// Register second shader
auto terrain = BuildShader("Terrain");
registry.RegisterShader(...);
// SDI_Registry.h now includes: PBRShader, TerrainShader

// Unregister first shader
registry.UnregisterShader(pbr.uuid);
// SDI_Registry.h now includes: TerrainShader (only!)
```

### Advanced Features

**Runtime shader enumeration:**

```cpp
#include "SDI_Registry.h"

// Iterate all registered shaders at runtime
for (size_t i = 0; i < Shaders::Registry::SHADER_COUNT; ++i) {
    const auto& info = Shaders::Registry::SHADERS[i];
    std::cout << "Shader: " << info.name
              << " (alias: " << info.alias << ")\n";
}
```

**Manual registry operations:**

```cpp
SdiRegistryManager registry;

// Register manually
SdiRegistryEntry entry{
    .uuid = "abc123",
    .programName = "MyShader",
    .sdiHeaderPath = "./generated/sdi/abc123-SDI.h",
    .sdiNamespace = "ShaderInterface::abc123",
    .aliasName = "MyShader"
};
registry.RegisterShader(entry);

// Query
bool isRegistered = registry.IsRegistered("abc123");
auto entry = registry.GetEntry("abc123");

// Find by alias
std::string uuid = registry.FindByAlias("MyShader");

// Update alias
registry.UpdateAlias("abc123", "MyBetterShaderName");

// Get statistics
auto stats = registry.GetStats();
std::cout << "Active shaders: " << stats.activeShaders << "\n";
std::cout << "Inactive shaders: " << stats.inactiveShaders << "\n";
```

**Cleanup inactive shaders:**

```cpp
// Remove shaders inactive for > 24 hours
uint32_t removed = registry.CleanupInactive(std::chrono::hours(24));

// Validate registry (mark missing files as inactive)
uint32_t invalid = registry.ValidateRegistry();

// Clear all
registry.ClearAll(true);  // Also delete SDI files
```

**Manual regeneration control:**

```cpp
SdiRegistryManager::Config config{
    .autoRegenerate = false,  // Disable auto-regen
    .regenerationThreshold = 5  // Only regen after 5 changes
};

SdiRegistryManager registry(config);

// Register multiple shaders...
registry.RegisterShader(entry1);
registry.RegisterShader(entry2);
registry.RegisterShader(entry3);

// Manually trigger regeneration
registry.RegenerateRegistry();
```

### Integration with ShaderBundleBuilder

**Automatic registration:**

```cpp
SdiRegistryManager registry;

// All shaders built with this configuration auto-register
auto result = ShaderBundleBuilder()
    .SetProgramName("PBRShader")
    .AddStage(...)
    .EnableRegistryIntegration(&registry)  // Enable!
    .Build();

// No manual registration needed!
```

**With custom alias:**

```cpp
auto result = ShaderBundleBuilder()
    .SetProgramName("PhysicallyBasedRenderingMaterial")  // Long name
    .EnableRegistryIntegration(&registry, "PBR")  // Short alias
    .Build();

// In C++ code:
using namespace Shaders;
binding.binding = PBR::Set0::MaterialBuffer::BINDING;  // Clean!
```

### Benefits Summary

| Feature | Without Registry | With Registry |
|---------|-----------------|---------------|
| **Includes** | Many individual headers | Single SDI_Registry.h |
| **Compilation** | All SDI headers compiled | Only active shaders |
| **Access** | Long namespace paths | Short aliases |
| **Updates** | Manual tracking | Auto-regeneration |
| **Runtime Info** | Not available | Shader enumeration |

### Thread Safety

SdiRegistryManager is fully thread-safe - multiple threads can register/unregister simultaneously:

```cpp
SdiRegistryManager registry;

// Safe to register from multiple threads
std::vector<std::future<void>> futures;
for (const auto& shader : shaders) {
    futures.push_back(std::async([&]() {
        auto result = BuildShader(shader);
        registry.RegisterShader(...);  // Thread-safe!
    }));
}
```

---

## Future Enhancements

Possible future additions (as separate classes):

- **ShaderOptimizer**: SPIR-V optimization passes
- **ShaderVariantGenerator**: Automatic variant generation
- **ShaderDependencyTracker**: Track shader file dependencies
- **ShaderMetricsCollector**: Compilation time, cache hit rates, etc.
- **SdiAccessorGenerator**: Generate high-level accessor APIs for descriptors

All would follow the same pattern: focused, single-responsibility classes that compose well together.
