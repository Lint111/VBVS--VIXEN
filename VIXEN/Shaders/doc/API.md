# VulkanShader API Reference

## Table of Contents
1. [Classes and Structures](#classes-and-structures)
2. [Builder Pattern API](#builder-pattern-api)
3. [Shader Management](#shader-management)
4. [Hot Reloading](#hot-reloading)
5. [Shader Reflection](#shader-reflection)
6. [Legacy API](#legacy-api)

---

## Classes and Structures

### ShaderCompileOptions

Compilation options for GLSL to SPIR-V conversion.

```cpp
struct ShaderCompileOptions {
    std::map<std::string, std::string> defines;  // Preprocessor defines
    std::string entryPoint = "main";             // Entry point function name
    bool enableOptimization = true;              // Enable SPIR-V optimization
    bool enableDebugInfo = false;                // Include debug information
    std::vector<std::string> includePaths;       // Paths for #include resolution
};
```

#### Members

- **defines**: Map of preprocessor macro definitions
  - Key: Macro name
  - Value: Macro value (or "1" for simple defines)

- **entryPoint**: Name of the shader entry point function
  - Default: "main"
  - Can be customized for multiple entry points in a single file

- **enableOptimization**: Enable SPIR-V optimization passes
  - Default: `true`
  - Set to `false` for faster compilation during development

- **enableDebugInfo**: Include debug information in SPIR-V
  - Default: `false`
  - Enable for better debugging support in tools like RenderDoc

- **includePaths**: Directories to search for `#include` files
  - Paths are searched in order
  - Relative includes are resolved relative to the current file

#### Example

```cpp
ShaderCompileOptions options;
options.defines["USE_PBR"] = "1";
options.defines["MAX_LIGHTS"] = "16";
options.enableDebugInfo = true;
options.includePaths = {"./shaders/include", "/usr/share/shaders"};

shader.SetCompileOptions(options);
```

---

### ShaderStageInfo

Internal structure representing a single shader stage.

```cpp
struct ShaderStageInfo {
    VkShaderStageFlagBits stage;
    std::string source;                          // GLSL source code
    std::vector<uint32_t> spirv;                 // Compiled SPIR-V
    std::string entryPoint = "main";
    VkShaderModule module = VK_NULL_HANDLE;
    VkSpecializationInfo* specializationInfo = nullptr;
};
```

**Note**: This structure is primarily for internal use. Users interact with it indirectly through the builder API.

---

### ShaderReflection

Structure containing shader reflection data (extensible for future SPIRV-Reflect integration).

```cpp
struct ShaderReflection {
    struct DescriptorBinding {
        uint32_t set;
        uint32_t binding;
        VkDescriptorType type;
        uint32_t count;
        std::string name;
    };

    struct PushConstantRange {
        VkShaderStageFlags stages;
        uint32_t offset;
        uint32_t size;
    };

    std::vector<DescriptorBinding> descriptorBindings;
    std::vector<PushConstantRange> pushConstants;
    std::vector<VkVertexInputAttributeDescription> inputAttributes;
};
```

**Note**: Currently a placeholder. Full reflection requires integration with SPIRV-Reflect library.

---

## VulkanShader Class

### Constructor / Destructor

```cpp
VulkanShader();
~VulkanShader();
```

- **Constructor**: Initializes the shader object and creates a logger instance
- **Destructor**: Automatically calls `DestroyShader()` to clean up resources

---

## Builder Pattern API

### AddStage

Add a shader stage from GLSL source code.

```cpp
VulkanShader& AddStage(VkShaderStageFlagBits stage,
                       const std::string& source,
                       const std::string& entryPoint = "main");
```

#### Parameters
- **stage**: Shader stage type (VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, etc.)
- **source**: GLSL source code as a string
- **entryPoint**: Entry point function name (default: "main")

#### Returns
Reference to `this` for method chaining

#### Example

```cpp
std::string vertSource = R"(
    #version 450
    layout(location = 0) in vec3 inPosition;
    layout(location = 0) out vec3 fragColor;

    void main() {
        gl_Position = vec4(inPosition, 1.0);
        fragColor = inPosition * 0.5 + 0.5;
    }
)";

shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertSource);
```

---

### AddStageSPV

Add a shader stage from pre-compiled SPIR-V binary.

```cpp
VulkanShader& AddStageSPV(VkShaderStageFlagBits stage,
                          const std::vector<uint32_t>& spirv,
                          const std::string& entryPoint = "main");
```

#### Parameters
- **stage**: Shader stage type
- **spirv**: SPIR-V binary data
- **entryPoint**: Entry point function name

#### Returns
Reference to `this` for method chaining

#### Example

```cpp
std::vector<uint32_t> spirv = LoadSpirvFromFile("shader.vert.spv");
shader.AddStageSPV(VK_SHADER_STAGE_VERTEX_BIT, spirv);
```

---

### AddStageFromFile

Add a shader stage by loading from a file.

```cpp
VulkanShader& AddStageFromFile(VkShaderStageFlagBits stage,
                               const std::string& filepath,
                               const std::string& entryPoint = "main");
```

#### Parameters
- **stage**: Shader stage type
- **filepath**: Path to shader file
  - GLSL files: `.vert`, `.frag`, `.geom`, `.tesc`, `.tese`, `.comp`, `.glsl`
  - SPIR-V files: `.spv`
- **entryPoint**: Entry point function name

#### Returns
Reference to `this` for method chaining

#### Notes
- Automatically detects file type based on extension
- Stores file path for hot reloading support
- Records file modification time for change detection

#### Example

```cpp
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shaders/basic.vert")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/basic.frag");
```

---

### SetCompileOptions

Set compilation options for GLSL to SPIR-V conversion.

```cpp
VulkanShader& SetCompileOptions(const ShaderCompileOptions& options);
```

#### Parameters
- **options**: Compilation options structure

#### Returns
Reference to `this` for method chaining

#### Example

```cpp
ShaderCompileOptions opts;
opts.enableDebugInfo = true;
opts.enableOptimization = false;
shader.SetCompileOptions(opts);
```

---

### AddDefine

Add a single preprocessor define.

```cpp
VulkanShader& AddDefine(const std::string& name,
                        const std::string& value = "1");
```

#### Parameters
- **name**: Define name
- **value**: Define value (default: "1")

#### Returns
Reference to `this` for method chaining

#### Example

```cpp
shader.AddDefine("USE_NORMALMAP")
      .AddDefine("MAX_BONES", "128")
      .AddDefine("SHADOW_QUALITY", "2");
```

---

### EnableCache

Enable shader caching to disk.

```cpp
VulkanShader& EnableCache(const std::string& cachePath = "./shader_cache");
```

#### Parameters
- **cachePath**: Directory path for cache storage (default: "./shader_cache")

#### Returns
Reference to `this` for method chaining

#### Notes
- Creates cache directory if it doesn't exist
- Cache files are named based on source hash
- Cache is checked before compilation

#### Example

```cpp
shader.EnableCache("./build/shader_cache");
```

---

### Build

Compile and build all shader modules.

```cpp
bool Build();
```

#### Returns
- `true` if all stages built successfully
- `false` if any stage failed to build

#### Notes
- Must be called after adding all stages
- Handles GLSL to SPIR-V compilation
- Creates VkShaderModule objects
- Thread-safe operation

#### Example

```cpp
if (!shader.Build()) {
    std::cerr << "Shader build failed!" << std::endl;
    return false;
}
```

---

## Shader Management

### DestroyShader

Destroy all shader modules and release resources.

```cpp
void DestroyShader();
```

#### Notes
- Destroys all VkShaderModule objects
- Clears all stage information
- Resets initialized state
- Called automatically by destructor

#### Example

```cpp
shader.DestroyShader();
```

---

### IsInitialized

Check if shader is successfully initialized.

```cpp
bool IsInitialized() const;
```

#### Returns
- `true` if shader was built successfully
- `false` otherwise

#### Example

```cpp
if (shader.IsInitialized()) {
    // Use shader in pipeline
}
```

---

### GetStages

Get shader stage create infos for pipeline creation.

```cpp
const VkPipelineShaderStageCreateInfo* GetStages() const;
```

#### Returns
Pointer to array of VkPipelineShaderStageCreateInfo structures

#### Example

```cpp
pipelineInfo.stageCount = shader.GetStageCount();
pipelineInfo.pStages = shader.GetStages();
```

---

### GetStageCount

Get the number of shader stages.

```cpp
uint32_t GetStageCount() const;
```

#### Returns
Number of successfully built shader stages

#### Example

```cpp
uint32_t count = shader.GetStageCount();
std::cout << "Shader has " << count << " stages" << std::endl;
```

---

### SetLogger

Set a custom logger for the shader.

```cpp
void SetLogger(std::shared_ptr<Logger> logger);
```

#### Parameters
- **logger**: Shared pointer to logger instance

#### Example

```cpp
auto myLogger = std::make_shared<Logger>("CustomShader");
shader.SetLogger(myLogger);
```

---

## Hot Reloading

### HotReload

Reload shaders from their source files.

```cpp
bool HotReload();
```

#### Returns
- `true` if reload successful
- `false` if no changes detected or reload failed

#### Notes
- Only works for shaders loaded with `AddStageFromFile()`
- Checks file modification times
- Recompiles and rebuilds automatically

#### Example

```cpp
// In update loop
if (shader.HotReload()) {
    std::cout << "Shader reloaded!" << std::endl;
    // Rebuild pipeline
    pipeline.Rebuild(&shader);
}
```

---

### HasSourceChanged

Check if any source files have been modified.

```cpp
bool HasSourceChanged() const;
```

#### Returns
- `true` if any source file has been modified
- `false` otherwise

#### Notes
- Thread-safe operation
- Checks all files loaded with `AddStageFromFile()`

#### Example

```cpp
if (shader.HasSourceChanged()) {
    std::cout << "Shader source modified" << std::endl;
    shader.HotReload();
}
```

---

## Shader Reflection

### GetReflection

Get shader reflection data.

```cpp
const ShaderReflection& GetReflection() const;
```

#### Returns
Reference to ShaderReflection structure

#### Note
Currently returns placeholder data. Requires SPIRV-Reflect integration for full functionality.

---

### ReflectShader

Perform shader reflection to extract metadata.

```cpp
bool ReflectShader();
```

#### Returns
- `true` if reflection successful
- `false` otherwise

#### Note
Currently not implemented. Placeholder for future SPIRV-Reflect integration.

---

## Legacy API

Maintained for backward compatibility with existing code.

### BuildShaderModuleWithSPV (Deprecated)

Build vertex and fragment shaders from SPIR-V.

```cpp
void BuildShaderModuleWithSPV(uint32_t* vertShaderText, size_t vertexSPVSize,
                              uint32_t* fragShaderText, size_t fragSPVSize);
```

**Deprecated**: Use builder API instead

#### Example

```cpp
// Old way (deprecated)
shader.BuildShaderModuleWithSPV(vertSpv, vertSize, fragSpv, fragSize);

// New way (recommended)
shader.AddStageSPV(VK_SHADER_STAGE_VERTEX_BIT, vertSpirv)
      .AddStageSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragSpirv)
      .Build();
```

---

### BuildShader (Deprecated)

Build vertex and fragment shaders from GLSL source.

```cpp
void BuildShader(const char* vertShaderText, const char* fragShaderText);
```

**Deprecated**: Use builder API instead
**Requires**: `AUTO_COMPILE_GLSL_TO_SPV` flag

#### Example

```cpp
// Old way (deprecated)
shader.BuildShader(vertSource, fragSource);

// New way (recommended)
shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertSource)
      .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragSource)
      .Build();
```

---

## Supported Shader Stages

The library supports all Vulkan shader stage types:

| Stage Flag | Description | File Extension |
|-----------|-------------|----------------|
| `VK_SHADER_STAGE_VERTEX_BIT` | Vertex shader | `.vert` |
| `VK_SHADER_STAGE_FRAGMENT_BIT` | Fragment shader | `.frag` |
| `VK_SHADER_STAGE_GEOMETRY_BIT` | Geometry shader | `.geom` |
| `VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT` | Tessellation control | `.tesc` |
| `VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT` | Tessellation evaluation | `.tese` |
| `VK_SHADER_STAGE_COMPUTE_BIT` | Compute shader | `.comp` |

---

## Error Handling

The library uses the project's logger system for error reporting. All errors, warnings, and info messages are logged through the logger instance.

### Checking for Errors

```cpp
auto logger = std::make_shared<Logger>("MyShader");
shader.SetLogger(logger);

if (!shader.Build()) {
    // Extract and print logs
    std::cout << logger->ExtractLogs() << std::endl;
}
```

### Common Error Messages

- **"Shader file not found"**: File path is incorrect
- **"Failed to compile GLSL to SPIR-V"**: GLSL syntax error
- **"GLSL compilation not enabled"**: Build without AUTO_COMPILE_GLSL_TO_SPV flag
- **"Failed to create shader module"**: Vulkan device error
- **"Include file not found"**: #include directive references missing file

---

## Thread Safety

All VulkanShader methods are thread-safe through internal mutex locking. You can safely:

- Build shaders from multiple threads
- Hot reload from any thread
- Query shader state concurrently

```cpp
// Safe concurrent operations
std::thread t1([&]() { shader1.Build(); });
std::thread t2([&]() { shader2.Build(); });
t1.join();
t2.join();
```

---

## Constants

```cpp
const int MAX_SHADER_STAGES = 6;
```

Maximum number of shader stages supported in a single VulkanShader object.
