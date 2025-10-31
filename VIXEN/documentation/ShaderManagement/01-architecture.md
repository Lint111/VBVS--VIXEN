# ShaderManagement Architecture

**Last Updated**: October 31, 2025
**Status**: Phase 3 Complete - SDI Generation with Type-Safe UBO Updates

## Overview

GLSL→SPIR-V shader compilation library with SPIRV reflection and automatic Shader Descriptor Interface (SDI) generation. Provides type-safe shader resource access via generated C++ headers.

## Core Components

### 1. ShaderBundleBuilder - Compilation Orchestrator

Compiles GLSL to SPIR-V and orchestrates reflection + SDI generation.

**Responsibilities**:
- Load GLSL source from files or strings
- Invoke glslang compiler for SPIR-V generation
- Trigger SPIRV reflection
- Generate SDI headers
- Package results into ShaderDataBundle

**API**:
```cpp
ShaderBundleBuilder builder;
builder.SetProgramName("Draw_Shader")
       .SetSdiConfig(sdiConfig)
       .EnableSdiGeneration(true)
       .AddStageFromFile(ShaderStage::Vertex, "Shaders/Draw.vert", "main")
       .AddStageFromFile(ShaderStage::Fragment, "Shaders/Draw.frag", "main");

auto result = builder.Build();
if (!result.success) {
    throw std::runtime_error(result.errorMessage);
}

auto bundle = std::move(*result.bundle);
```

### 2. SpirvReflector - SPIRV Metadata Extraction

Extracts shader interface data from SPIR-V bytecode using spirv-cross.

**Extracted Data**:
- Descriptor bindings (UBOs, samplers, storage buffers)
- Push constant ranges
- Vertex input attributes
- UBO struct layouts (recursive)
- Shader stage information

**Key Methods**:
```cpp
SpirvReflectionData SpirvReflector::Reflect(const std::vector<uint32_t>& spirv);

// Internal helpers
void ReflectDescriptors(const spv_reflect::ShaderModule& module, SpirvReflectionData& data);
void ReflectPushConstants(const spv_reflect::ShaderModule& module, SpirvReflectionData& data);
void ReflectVertexInputs(const spv_reflect::ShaderModule& module, SpirvReflectionData& data);
void ExtractBlockMembers(const SpvReflectBlockVariable* blockVar, std::vector<SpirvStructMember>& outMembers);
```

### 3. SpirvInterfaceGenerator - SDI Header Generation

Generates type-safe C++ headers from reflection data.

**Output Files**:
1. **Generic SDI**: `{content-hash}-SDI.h` - UUID namespace with descriptor/struct definitions
2. **Shader-Specific Names**: `{shader}Names.h` - Constexpr constants and type aliases

**Key Methods**:
```cpp
std::string GenerateInterfaceHeader(const std::string& uuid, const SpirvReflectionData& data);
std::string GenerateNamesHeader(const std::string& programName, const std::string& uuid, const SpirvReflectionData& data);
```

### 4. ShaderDataBundle - Compilation Result

Encapsulates compiled shader + reflection metadata.

**Structure**:
```cpp
struct ShaderDataBundle {
    ShaderProgram program;                     // SPIR-V bytecode + stage info
    std::shared_ptr<SpirvReflectionData> reflectionData;  // Descriptor bindings, structs
    std::string descriptorInterfaceHash;       // For cache invalidation
    ShaderDirtyFlags dirtyFlags;               // Change tracking
};
```

**Usage**:
```cpp
// Get reflection data
auto& descriptorSets = bundle->reflectionData->descriptorSets;
auto& pushConstants = bundle->reflectionData->pushConstants;
auto& vertexInputs = bundle->reflectionData->vertexInputs;

// Get SPIR-V
for (auto& stage : bundle->program.stages) {
    const std::vector<uint32_t>& spirv = stage.spirvCode;
}
```

## Phase 3 SDI Architecture

### Split SDI Pattern

**Problem**: Generic interface sharing vs shader-specific convenience

**Solution**: Two files - generic interface + shader-specific names

**Generic SDI** (`2071dff093caf4b3-SDI.h`):
```cpp
namespace ShaderInterface {
namespace 2071dff093caf4b3 {  // Content-hash UUID

    // UBO struct (from SPIRV reflection)
    struct bufferVals {
        mat4 mvp;  // Offset: 0
    };

    // Descriptor bindings
    namespace Set0 {
        struct myBufferVals {
            static constexpr uint32_t SET = 0;
            static constexpr uint32_t BINDING = 0;
            static constexpr VkDescriptorType TYPE = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            static constexpr VkShaderStageFlags STAGES = VK_SHADER_STAGE_VERTEX_BIT;
            using DataType = bufferVals;  // Linked via structDefIndex
        };
    };
}
}
```

**Shader-Specific Names** (`Draw_ShaderNames.h`):
```cpp
#include "2071dff093caf4b3-SDI.h"

namespace Draw_Shader {
    // Reference to generic SDI
    namespace SDI = ShaderInterface::2071dff093caf4b3;

    // Convenience constants
    using myBufferVals_t = SDI::Set0::myBufferVals;
    constexpr uint32_t myBufferVals_SET = myBufferVals_t::SET;
    constexpr uint32_t myBufferVals_BINDING = myBufferVals_t::BINDING;
}
```

### Recursive UBO Struct Extraction

**Challenge**: Extract complete UBO struct layouts including nested structs and matrices

**Solution**: `ExtractBlockMembers()` recursive walker

**Implementation** (`SpirvReflector.cpp:177-220`):
```cpp
void ExtractBlockMembers(
    const ::SpvReflectBlockVariable* blockVar,
    std::vector<SpirvStructMember>& outMembers
) {
    for (uint32_t i = 0; i < blockVar->member_count; ++i) {
        const auto& member = blockVar->members[i];
        SpirvStructMember spirvMember;
        spirvMember.name = member.name ? member.name : "";
        spirvMember.offset = member.offset;
        spirvMember.arrayStride = member.array.stride;
        spirvMember.matrixStride = member.numeric.matrix.stride;

        // Matrix detection (CRITICAL: check stride, not type_description)
        if (member.numeric.matrix.stride > 0 && member.numeric.matrix.column_count > 0) {
            spirvMember.type.baseType = SpirvTypeInfo::BaseType::Matrix;
            spirvMember.type.columns = member.numeric.matrix.column_count;
            spirvMember.type.rows = member.numeric.matrix.row_count;
            spirvMember.type.width = member.numeric.scalar.width;
        }
        // Nested struct (recursive)
        else if (member.member_count > 0) {
            spirvMember.type.baseType = SpirvTypeInfo::BaseType::Struct;
            spirvMember.type.structName = member.type_description->type_name;
            // TODO: Recursively extract nested members
        }
        // Scalar/vector types
        else {
            spirvMember.type = ConvertType(member.type_description);
        }

        outMembers.push_back(spirvMember);
    }
}
```

**Key Insight**: Matrix detection requires checking `member.numeric.matrix.stride`, not `type_description`. The latter doesn't have stride info, leading to mat4→dvec4 misidentification.

### Index-Based Struct Linking

**Problem**: Pointer-based linking (`SpirvDescriptorBinding::structDef*`) causes dangling pointers during vector reallocation

**Solution**: Index-based linking

**Before**:
```cpp
struct SpirvDescriptorBinding {
    SpirvStructDefinition* structDef = nullptr;  // DANGER: invalidated on reallocation
};

// Usage (UNSAFE):
desc.structDef = &data.structDefinitions.back();  // Pointer becomes invalid!
```

**After** (`SpirvReflectionData.h:102`):
```cpp
struct SpirvDescriptorBinding {
    int structDefIndex = -1;  // Index into SpirvReflectionData::structDefinitions
};

// Usage (SAFE):
desc.structDefIndex = static_cast<int>(data.structDefinitions.size()) - 1;

// Dereference with bounds check
if (desc.structDefIndex >= 0 && desc.structDefIndex < data.structDefinitions.size()) {
    const auto& structDef = data.structDefinitions[desc.structDefIndex];
}
```

### Content-Hash UUID System

**Goal**: Deterministic UUID generation enabling interface sharing

**Implementation**:
```cpp
std::string GenerateContentBasedUuid(const std::string& source) {
    // Hash preprocessed source (GLSL after #include resolution)
    size_t hash = FNV1aHash(source);

    // Convert to 16-char hex string
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();  // e.g., "2071dff093caf4b3"
}
```

**Result**: Same shader source → same UUID → shared generic SDI file

## Directory Structure

### Build-Time Shaders
**Location**: `generated/sdi/`
**Purpose**: Version-controlled shader library tracking
**Contains**: SDI files for all shaders compiled during build

### Runtime Shaders
**Location**: `binaries/generated/sdi/`
**Purpose**: Application-specific runtime shader registration
**Contains**: SDI files for shaders loaded at runtime via ShaderLibraryNode

**Example**:
```
generated/sdi/
├── 2071dff093caf4b3-SDI.h           # Generic interface
├── Draw_ShaderNames.h                # Shader-specific

binaries/generated/sdi/
├── 2071dff093caf4b3-SDI.h           # Runtime-registered shader
└── RuntimeShader_Names.h
```

## Integration Points

### ShaderLibraryNode (Runtime)

**Role**: Load shaders at runtime, generate SDI dynamically

**Implementation** (`RenderGraph/src/Nodes/ShaderLibraryNode.cpp:118-128`):
```cpp
void ShaderLibraryNode::Compile() {
    // Configure SDI output to runtime directory
    ShaderManagement::SdiGeneratorConfig sdiConfig;
    sdiConfig.outputDirectory = std::filesystem::current_path() / "binaries" / "generated" / "sdi";
    sdiConfig.namespacePrefix = "ShaderInterface";

    ShaderManagement::ShaderBundleBuilder builder;
    builder.SetProgramName("Draw_Shader")
           .SetSdiConfig(sdiConfig)
           .EnableSdiGeneration(true)  // Phase 3: Enable SDI
           .AddStageFromFile(ShaderManagement::ShaderStage::Vertex, vertPath, "main")
           .AddStageFromFile(ShaderManagement::ShaderStage::Fragment, fragPath, "main");

    auto result = builder.Build();
    shaderBundle_ = std::make_shared<ShaderManagement::ShaderDataBundle>(std::move(*result.bundle));

    // Output bundle for downstream nodes
    Out(SHADER_BUNDLE, shaderBundle_);
}
```

### GraphicsPipelineNode (Reflection Usage)

**Role**: Use reflection data for vertex inputs, shader stages

**Implementation** (`RenderGraph/src/Nodes/GraphicsPipelineNode.cpp:275-357`):
```cpp
void BuildVertexInputsFromReflection(
    std::shared_ptr<ShaderManagement::ShaderDataBundle> bundle,
    std::vector<VkVertexInputBindingDescription>& outBindings,
    std::vector<VkVertexInputAttributeDescription>& outAttributes
) {
    if (!bundle || !bundle->reflectionData) return;

    // Extract vertex inputs from reflection
    for (const auto& input : bundle->reflectionData->vertexInputs) {
        VkVertexInputAttributeDescription attr{};
        attr.location = input.location;
        attr.binding = 0;
        attr.format = static_cast<VkFormat>(input.format);  // Direct from SPIRV-Reflect
        attr.offset = computedOffset;
        outAttributes.push_back(attr);
    }
}
```

## Key Architectural Decisions

### Decision: Split SDI Architecture
**Rationale**: Enable interface sharing while maintaining shader-specific convenience
**Trade-off**: Two files instead of one, but enables reuse and clarity

### Decision: Content-Hash UUID
**Rationale**: Deterministic UUID from source enables interface sharing
**Trade-off**: Hash collisions possible (extremely rare with 64-bit hash)

### Decision: Index-Based Struct Linking
**Rationale**: Prevent dangling pointers during vector reallocation
**Trade-off**: Slightly less direct access, but safety critical

### Decision: Matrix Detection via Stride
**Rationale**: `type_description` doesn't have stride info, block variable does
**Trade-off**: None - this is the correct reflection approach

### Decision: Separate Output Directories
**Rationale**: Clear separation of build-time library tracking vs runtime shader registration
**Trade-off**: None - improves organization

## Key Files

- `ShaderManagement/src/ShaderBundleBuilder.cpp` - Compilation orchestration
- `ShaderManagement/src/SpirvReflector.cpp:177-220` - Recursive UBO extraction
- `ShaderManagement/src/SpirvInterfaceGenerator.cpp:174-275` - SDI generation
- `ShaderManagement/include/ShaderManagement/SpirvReflectionData.h:102` - Index-based linking
- `ShaderManagement/src/ShaderDataBundle.cpp:103-162` - Interface hash computation
- `RenderGraph/src/Nodes/ShaderLibraryNode.cpp:118-128` - Runtime SDI output

## Future Enhancements (Phase 4+)

- Descriptor set layout automation from reflection
- Push constant extraction
- Descriptor pool sizing from metadata
- Cache persistence verification
