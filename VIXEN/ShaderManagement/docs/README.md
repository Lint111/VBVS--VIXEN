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

## Future Enhancements

Possible future additions (as separate classes):

- **ShaderOptimizer**: SPIR-V optimization passes
- **ShaderVariantGenerator**: Automatic variant generation
- **ShaderDependencyTracker**: Track shader file dependencies
- **ShaderMetricsCollector**: Compilation time, cache hit rates, etc.

All would follow the same pattern: focused, single-responsibility classes that compose well together.
