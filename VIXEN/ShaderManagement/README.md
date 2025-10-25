# ShaderManagement Library

Advanced Vulkan shader compilation and management system with build-time tooling, SPIRV reflection, type-safe descriptor interfaces, and intelligent hot-reload support.

## Features

### Core Functionality
- **Shader Compilation** - GLSL → SPIRV compilation with optimization and debug modes
- **SPIRV Reflection** - Automatic extraction of descriptor sets, push constants, vertex inputs, and struct layouts
- **SDI Code Generation** - Auto-generated C++ headers with type-safe shader descriptor constants
- **Central SDI Registry** - Dynamic registry with optimized compilation (only includes active shaders)
- **Async Compilation** - Non-blocking shader builds with EventBus integration
- **Smart Hot-Reload** - Granular dirty tracking to detect safe vs. breaking changes
- **Build-Time Tooling** - CMake integration for pre-compilation and SDI generation

### Advanced Features
- **Descriptor-Only Interface Hashing** - Enables descriptor layout sharing across shaders
- **Pipeline Constraint Validation** - Ensures required stages are present for each pipeline type
- **Shader Caching** - Automatic SPIRV caching to avoid redundant compilations
- **Preprocessing Support** - Macro expansion, include resolution, and conditional compilation
- **Dirty Flags** - Track what changed (SPIRV, descriptors, vertex inputs) for intelligent updates
- **Hot-Reload Compatibility Detection** - Automatic detection of safe vs. breaking shader changes
- **Structured Logging & Telemetry** - Callback-based logging with comprehensive compilation metrics
- **Automatic Dependency Tracking** - CMake detects #include changes for incremental builds
- **Vulkan Device Validation** - Pre-runtime validation of shader compatibility with device capabilities
- **Content-Based UUIDs** - Deterministic shader IDs for stable caching and hot-reload
- **File Manifest Tracking** - Automatic cleanup of orphaned SPIRV files

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    ShaderManagement Library                 │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────┐        ┌──────────────────┐         │
│  │ ShaderCompiler   │        │ SpirvReflector   │         │
│  │ (GLSL→SPIRV)     │──────→│ (Extract Metadata)│         │
│  └──────────────────┘        └──────────────────┘         │
│           │                            │                    │
│           ▼                            ▼                    │
│  ┌──────────────────────────────────────────────┐         │
│  │     ShaderBundleBuilder (Orchestrator)       │         │
│  │  • Compile stages                            │         │
│  │  • Reflect metadata                          │         │
│  │  • Generate SDI headers                      │         │
│  │  • Compute descriptor hash                   │         │
│  │  • Assemble ShaderDataBundle                 │         │
│  └──────────────────────────────────────────────┘         │
│           │                            │                    │
│           ▼                            ▼                    │
│  ┌──────────────────┐        ┌──────────────────┐         │
│  │ ShaderDataBundle │        │ SDI Headers      │         │
│  │ • SPIRV code     │        │ • Type-safe      │         │
│  │ • Reflection data│        │   constants      │         │
│  │ • Descriptor hash│        │ • Compile-time   │         │
│  │ • Dirty flags    │        │   validation     │         │
│  └──────────────────┘        └──────────────────┘         │
│           │                            │                    │
│           ▼                            ▼                    │
│  ┌──────────────────────────────────────────────┐         │
│  │        SdiRegistryManager (Central)          │         │
│  │  • Dynamic registry generation               │         │
│  │  • Only includes active shaders              │         │
│  │  • Optimized compilation                     │         │
│  └──────────────────────────────────────────────┘         │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                    Async Build System                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────────────────────────────────┐         │
│  │   AsyncShaderBundleBuilder (Worker Threads)  │         │
│  │  • Non-blocking compilation                  │         │
│  │  • EventBus integration                      │         │
│  │  • Progress tracking                         │         │
│  │  • Parallel builds                           │         │
│  └──────────────────────────────────────────────┘         │
│           │                                                 │
│           ▼                                                 │
│  ┌──────────────────────────────────────────────┐         │
│  │          EventBus (Main Thread)              │         │
│  │  • CompilationStarted                        │         │
│  │  • CompilationProgress                       │         │
│  │  • CompilationCompleted                      │         │
│  │  • CompilationFailed                         │         │
│  │  • SdiGenerated                              │         │
│  │  • HotReloadReady                            │         │
│  └──────────────────────────────────────────────┘         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
        ┌─────────────────────────────────────┐
        │     Build Tool (shader_tool)        │
        │  • CMake integration                │
        │  • Build-time compilation           │
        │  • Batch processing                 │
        │  • Registry generation              │
        └─────────────────────────────────────┘
```

## Quick Start

### 1. Runtime Usage (Traditional)

```cpp
#include <ShaderManagement/ShaderBundleBuilder.h>
using namespace ShaderManagement;

// Create builder
ShaderBundleBuilder builder;
builder.SetProgramName("MyShader")
       .AddStageFromFile(ShaderStage::Vertex, "shader.vert")
       .AddStageFromFile(ShaderStage::Fragment, "shader.frag")
       .EnableSdiGeneration(true);

// Build synchronously
auto result = builder.Build();
if (result.success) {
    ShaderDataBundle& bundle = *result.bundle;

    // Use bundle for Vulkan pipeline creation...
    // Access SDI header: bundle.sdiHeaderPath
}
```

### 2. Build-Time Usage (Recommended)

**CMakeLists.txt:**
```cmake
find_package(ShaderManagement REQUIRED)
include(ShaderToolUtils)

# Batch compile all shaders
add_shader_batch(AllShaders
    CONFIG shaders.json
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated
)

add_executable(MyApp main.cpp)
add_dependencies(MyApp AllShaders)
target_include_directories(MyApp PRIVATE ${CMAKE_BINARY_DIR}/generated)
```

**shaders.json:**
```json
{
  "shaders": [
    {
      "name": "MyShader",
      "stages": ["shader.vert", "shader.frag"],
      "pipeline": "graphics"
    }
  ],
  "buildRegistry": true
}
```

**main.cpp:**
```cpp
#include <SDI_Registry.h>  // Auto-generated

// Type-safe descriptor access (compile-time validated!)
auto texBinding = SDI::MyShader::Descriptors::MainTexture;
// texBinding.set = 0
// texBinding.binding = 1
// texBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER

// Load pre-compiled shader
ShaderDataBundle bundle = LoadBundleFromFile("generated/shaders/MyShader.json");
```

### 3. Async Compilation (Hot-Reload)

```cpp
#include <ShaderManagement/AsyncShaderBundleBuilder.h>

EventBus::MessageBus messageBus;
AsyncShaderBundleBuilder asyncBuilder(&messageBus);

// Subscribe to completion events
messageBus.Subscribe(
    ShaderCompilationCompletedMessage::TYPE,
    [](const Message& msg) {
        auto& completed = static_cast<const ShaderCompilationCompletedMessage&>(msg);
        UpdateShaderLibrary(completed.bundle);
        return true;
    }
);

// Start async build (non-blocking!)
std::string uuid = asyncBuilder.BuildAsync()
    .SetProgramName("MyShader")
    .AddStageFromFile(ShaderStage::Vertex, "shader.vert")
    .AddStageFromFile(ShaderStage::Fragment, "shader.frag")
    .Submit();

// Main thread continues running...
// When done, event fires on next messageBus.ProcessMessages()
```

## Detailed Documentation

### ShaderDataBundle

The unified package containing everything about a shader:

```cpp
struct ShaderDataBundle {
    CompiledProgram program;                    // SPIRV stages
    std::shared_ptr<SpirvReflectionData> reflectionData;  // Metadata
    std::shared_ptr<DescriptorSetLayout> descriptorLayout; // Vulkan layouts

    std::string uuid;                           // Unique identifier
    std::filesystem::path sdiHeaderPath;        // Path to SDI header
    std::string sdiNamespace;                   // SDI namespace

    std::string descriptorInterfaceHash;        // Descriptor-only hash
    ShaderDirtyFlags dirtyFlags;                // What changed?

    std::chrono::system_clock::time_point createdAt;

    // Check if interfaces match (descriptor layout only)
    bool HasIdenticalInterface(const ShaderDataBundle& other) const;

    // Determine hot-reload compatibility
    HotReloadCompatibility GetHotReloadCompatibility(
        const ShaderDataBundle& other
    ) const;
};
```

### SPIRV Descriptor Interface (SDI)

Auto-generated header providing type-safe shader descriptor access:

**Generated Header (MyShader_SDI.h):**
```cpp
namespace SDI::MyShader {
    constexpr const char* UUID = "a1b2c3d4e5f6...";
    constexpr const char* PROGRAM_NAME = "MyShader";

    namespace Descriptors {
        struct MainTexture {
            static constexpr uint32_t set = 0;
            static constexpr uint32_t binding = 0;
            static constexpr VkDescriptorType type =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        };

        struct UniformBuffer {
            static constexpr uint32_t set = 0;
            static constexpr uint32_t binding = 1;
            static constexpr VkDescriptorType type =
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            static constexpr uint32_t size = 64;  // bytes

            struct Layout {
                alignas(16) glm::mat4 viewProjection;
                alignas(16) glm::vec4 lightPosition;
            };
        };
    }

    namespace PushConstants {
        struct ModelMatrix {
            static constexpr uint32_t offset = 0;
            static constexpr uint32_t size = 64;
            static constexpr VkShaderStageFlags stages =
                VK_SHADER_STAGE_VERTEX_BIT;
        };
    }

    namespace VertexInputs {
        struct Position {
            static constexpr uint32_t location = 0;
            static constexpr VkFormat format = VK_FORMAT_R32G32B32_SFLOAT;
        };

        struct Normal {
            static constexpr uint32_t location = 1;
            static constexpr VkFormat format = VK_FORMAT_R32G32B32_SFLOAT;
        };
    }
}
```

**Usage:**
```cpp
#include "MyShader_SDI.h"

// Compile-time validated descriptor access
VkDescriptorSetLayoutBinding binding{};
binding.binding = SDI::MyShader::Descriptors::MainTexture::binding;
binding.descriptorType = SDI::MyShader::Descriptors::MainTexture::type;

// Type-safe uniform buffer
SDI::MyShader::Descriptors::UniformBuffer::Layout ubo;
ubo.viewProjection = camera.GetViewProjection();
ubo.lightPosition = glm::vec4(light.position, 1.0f);
```

### Central SDI Registry

The registry manager creates a single header including all active shaders:

```cpp
#include <SDI_Registry.h>

// Access any registered shader's SDI
auto shader1 = SDI::Shader1::Descriptors::MainTexture;
auto shader2 = SDI::Shader2::Descriptors::MainTexture;

// Registry only includes shaders that were RegisterShader()'ed
// Reduces compilation time by excluding unused shaders
```

Registry is dynamically generated and can be regenerated at runtime or build time.

### Smart Hot-Reload

The library tracks granular changes to enable intelligent hot-reload:

```cpp
ShaderDataBundle oldBundle = GetCurrentShader("MyShader");
ShaderDataBundle newBundle = RecompileShader("MyShader");

// Compare bundles
ShaderDirtyFlags flags = CompareBundles(oldBundle, newBundle);

if (flags & ShaderDirtyFlags::Spirv) {
    // Only SPIRV changed - safe to hot-swap!
    ReplaceShaderModule(newBundle.program);
}

if (flags & ShaderDirtyFlags::DescriptorSets) {
    // Descriptor layout changed - need to recreate descriptor sets
    RecreateDescriptorSets(newBundle);
}

if (flags & ShaderDirtyFlags::VertexInputs) {
    // Vertex input layout changed - must rebuild pipeline
    RebuildPipeline(newBundle);
}

// Or use high-level compatibility check
auto compatibility = newBundle.GetHotReloadCompatibility(oldBundle);
switch (compatibility) {
    case HotReloadCompatibility::FullyCompatible:
        // Safe hot-swap
        break;
    case HotReloadCompatibility::LayoutCompatible:
        // Update data, keep pipeline
        break;
    case HotReloadCompatibility::PipelineRebuild:
        // Rebuild pipeline
        break;
    case HotReloadCompatibility::Incompatible:
        // Full reload required
        break;
}
```

### Dirty Flags

```cpp
enum class ShaderDirtyFlags : uint32_t {
    None = 0,
    Spirv = 1 << 0,              // SPIRV bytecode changed (safe)
    DescriptorSets = 1 << 1,      // Descriptor set count changed
    DescriptorBindings = 1 << 2,  // Binding numbers changed
    DescriptorTypes = 1 << 3,     // Descriptor types changed
    PushConstants = 1 << 4,       // Push constant layout changed
    VertexInputs = 1 << 5,        // Vertex input layout changed
    FragmentOutputs = 1 << 6,     // Fragment output layout changed
    StageCount = 1 << 7,          // Stage count changed
    StructLayouts = 1 << 8,       // Struct member layouts changed
    MetadataOnly = 1 << 15,       // Only metadata changed

    // Convenience combinations
    InterfaceChanged = DescriptorSets | DescriptorBindings |
                       DescriptorTypes | PushConstants |
                       VertexInputs | StructLayouts,
    SafeHotReload = Spirv | MetadataOnly,
    RequiresPipelineRebuild = VertexInputs | DescriptorBindings | PushConstants,
    All = 0xFFFF
};
```

## New Features Guide

### Logging and Telemetry

The library includes a comprehensive logging and telemetry system for tracking compilation performance and debugging issues.

**Basic Usage:**
```cpp
#include "ShaderManagement/ShaderLogger.h"

// Set custom logger (optional)
ShaderLogger::GetInstance().SetCallback([](const LogMessage& msg) {
    MyLogger::Log(msg.level, msg.category, msg.message);
});

// Set minimum log level
ShaderLogger::GetInstance().SetMinimumLevel(LogLevel::Warning);

// Logs are automatically generated during compilation
// Or log manually:
ShaderLogger::LogInfo("Custom message", "MyCategory");
```

**Telemetry Metrics:**
```cpp
auto& telemetry = ShaderLogger::GetTelemetry();

std::cout << "Total compilations: " << telemetry.totalCompilations << "\n";
std::cout << "Success rate: " << telemetry.GetSuccessRate() * 100 << "%\n";
std::cout << "Cache hit rate: " << telemetry.GetCacheHitRate() * 100 << "%\n";
std::cout << "Avg compile time: " << telemetry.GetAverageCompileTimeMs() << "ms\n";

// Reset metrics
telemetry.Reset();
```

### Vulkan Device Validation

Validate shader bundles against device capabilities before runtime:

```cpp
#include "ShaderManagement/VulkanDeviceValidator.h"

VulkanDeviceValidator validator(physicalDevice);

// Validate shader bundle
auto result = validator.ValidateBundle(bundle);
if (!result.IsValid()) {
    std::cerr << "Shader incompatible:\n" << result.GetErrorMessage();

    for (const auto& feature : result.missingFeatures) {
        std::cerr << "  Missing: " << feature << "\n";
    }
}

// Query specific capabilities
if (!validator.SupportsMeshShaders()) {
    std::cerr << "Device doesn't support mesh shaders\n";
}

std::cout << "Max descriptor sets: " << validator.GetMaxDescriptorSets() << "\n";
std::cout << "Max push constants: " << validator.GetMaxPushConstantsSize() << " bytes\n";
```

**Validation Checks:**
- Shader stage support (geometry, tessellation, mesh, ray tracing)
- Descriptor set limits (per-set and per-type)
- Push constant size limits
- Vertex input attribute limits
- Pipeline type compatibility

### Automatic Dependency Tracking (CMake)

The CMake integration now automatically tracks `#include` dependencies for incremental builds:

```glsl
// main.vert
#include "common/lighting.glsl"
#include "common/transforms.glsl"

void main() {
    // When lighting.glsl or transforms.glsl changes,
    // main.vert will automatically recompile!
}
```

**CMake automatically detects includes:**
```cmake
add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    VERTEX main.vert
    FRAGMENT main.frag
    # Dependencies are auto-detected from #include directives
)
```

**Manual search paths (if needed):**
```cmake
# The parser searches these directories by default:
# - Same directory as shader file
# - ${CMAKE_CURRENT_SOURCE_DIR}
# - ${CMAKE_CURRENT_SOURCE_DIR}/shaders
# - ${CMAKE_CURRENT_SOURCE_DIR}/../shaders
```

### Content-Based UUIDs

Shaders now generate deterministic UUIDs based on their content, enabling stable caching and hot-reload:

```cpp
// Same source → Same UUID across builds
auto builder1 = ShaderBundleBuilder()
    .SetProgramName("MyShader")
    .AddStage(ShaderStage::Vertex, vertexSource);

auto result1 = builder1.Build();
auto uuid1 = result1.bundle->uuid;  // e.g., "a1b2c3d4e5f6..."

// Later build with same source
auto builder2 = ShaderBundleBuilder()
    .SetProgramName("MyShader")
    .AddStage(ShaderStage::Vertex, vertexSource);

auto result2 = builder2.Build();
auto uuid2 = result2.bundle->uuid;  // Same UUID!

// Benefits:
// - Cache can persist across sessions
// - Hot-reload knows it's the same shader
// - Registry remains stable
```

**UUID is based on:**
- Shader source code
- Compilation options
- Entry point names
- Macro defines
- Pipeline type

### File Manifest Tracking

The build tool now tracks generated files and cleans up orphaned SPIRV files:

```bash
# Compile with tracking
shader_tool compile shader.vert shader.frag --name MyShader --output-dir ./out

# Files tracked in ./out/.shader_tool_manifest.json:
# - MyShader.json
# - MyShader_stage0.spv
# - MyShader_stage1.spv

# If you remove a shader from the build, orphaned .spv files are automatically cleaned:
shader_tool cleanup ./out
# Removed 5 orphaned file(s)

# Or embed SPIRV in JSON to avoid external files:
shader_tool compile shader.vert --name MyShader --embed-spirv
```

**Batch mode auto-cleanup:**
```bash
# Automatically cleans orphaned files after batch processing
shader_tool batch shaders.json --output-dir ./generated --verbose
# ...
# Cleaned up 3 orphaned files
```

## Build Tool Integration

See [tools/README.md](tools/README.md) for comprehensive documentation.

### Quick Reference

**Compile single shader:**
```bash
shader_tool compile shader.vert shader.frag --name MyShader --output-dir ./out
```

**Build registry:**
```bash
shader_tool build-registry shader1.json shader2.json --output SDI_Registry.h
```

**Batch process:**
```bash
shader_tool batch shaders.json --output-dir ./generated
```

**CMake integration:**
```cmake
include(ShaderToolUtils)

add_shader_bundle(MyShader_Bundle
    PROGRAM_NAME "MyShader"
    VERTEX shader.vert
    FRAGMENT shader.frag
)

add_shader_registry(Registry
    BUNDLES MyShader_Bundle AnotherShader_Bundle
)
```

## Performance

### Build-Time vs Runtime Compilation

| Metric | Runtime Compilation | Build-Time Compilation |
|--------|---------------------|------------------------|
| App Startup | 500-2000ms (compiling) | < 50ms (loading) |
| Iteration Speed | Slow (recompile on launch) | Fast (only changed shaders) |
| Binary Size | Large (includes compiler) | Small (no compiler) |
| Type Safety | None | Compile-time validation |
| IDE Support | None | Full autocomplete |

### Descriptor Hash Benefits

Two shaders with identical descriptor layouts share the same hash, enabling:
- **Descriptor Set Sharing** - Reuse descriptor sets across compatible shaders
- **Fast Compatibility Checks** - O(1) hash comparison instead of deep reflection
- **Pipeline Compatibility** - Detect pipeline compatibility without Vulkan validation

## Examples

See the `examples/` directory for complete working examples:
- `basic_shader/` - Simple graphics shader compilation
- `compute_shader/` - Compute shader example
- `hot_reload/` - Hot-reload with dirty tracking
- `batch_build/` - CMake batch processing
- `async_compilation/` - Async builds with EventBus

## Testing

The library includes a comprehensive test suite using Google Test.

**Run all tests:**
```bash
cd build
cmake .. -DBUILD_TESTING=ON
make
ctest --output-on-failure
```

**Test Coverage:**
- ✅ Content-based UUID generation (determinism, collision avoidance)
- ✅ Logging system (filtering, callbacks, thread safety)
- ✅ Telemetry metrics (counters, timers, rates)
- ✅ ShaderBundleBuilder (move semantics, validation, errors)
- ✅ Input validation (size limits, stage limits)
- ✅ Compilation pipeline (success/failure cases)
- ✅ SPIRV reflection (descriptor extraction)
- ✅ SDI registry (registration, lookup, thread safety)
- ✅ File tracking and cleanup

**Test files:**
- `test_uuid_generation.cpp` - UUID determinism and uniqueness
- `test_logging.cpp` - Logging and telemetry system
- `test_shader_bundle_builder.cpp` - Builder pattern and validation
- `test_shader_compiler.cpp` - Compilation success/failure
- `test_spirv_reflection.cpp` - Metadata extraction
- `test_sdi_generator.cpp` - Registry management
- `test_file_tracking.cpp` - Manifest and cleanup

## Dependencies

- **Vulkan SDK** - Headers for Vulkan types
- **SPIRV-Reflect** - SPIRV metadata extraction (auto-fetched)
- **OpenSSL** - SHA-256 hashing (libcrypto)
- **nlohmann/json** - JSON serialization for bundles (auto-fetched, tool only)
- **EventBus** - Event system for async compilation (VIXEN library)
- **Google Test** - Testing framework (optional, for tests only)

## Building

```bash
cd VIXEN/ShaderManagement
mkdir build && cd build
cmake .. -DBUILD_TESTING=ON  # Enable tests (optional)
make
```

This builds:
- `libShaderManagement.a` - Core library
- `shader_tool` - Build-time compiler tool
- `ShaderManagementTests` - Test suite (if BUILD_TESTING=ON)

## License

[Your License Here]

## Contributing

[Your Contributing Guidelines Here]

## See Also

- [Build Tool Documentation](tools/README.md)
- [SDI Guide](docs/SDI_Guide.md) (TODO)
- [Hot-Reload System](docs/HotReload.md) (TODO)
- [Async Compilation](docs/AsyncCompilation.md) (TODO)
