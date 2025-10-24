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

## Typical Workflow

### 1. Without ShaderLibrary (Manual Control)

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

### 2. With ShaderLibrary (Managed)

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

## Future Enhancements

Possible future additions (as separate classes):

- **ShaderOptimizer**: SPIR-V optimization passes
- **ShaderVariantGenerator**: Automatic variant generation
- **ShaderDependencyTracker**: Track shader file dependencies
- **ShaderMetricsCollector**: Compilation time, cache hit rates, etc.
- **SdiAccessorGenerator**: Generate high-level accessor APIs for descriptors

All would follow the same pattern: focused, single-responsibility classes that compose well together.
