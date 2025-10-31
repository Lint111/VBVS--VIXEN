# ShaderManagement Documentation

**Status**: Phase 3 Complete - SDI Generation with Type-Safe UBO Updates
**Current Phase**: Phase 4 - Pipeline Layout Automation

## Overview

GLSL→SPIR-V shader compilation with SPIRV reflection and automatic Shader Descriptor Interface (SDI) generation for type-safe resource access.

## Core Documents

- **[ShaderManagement-Integration-Plan.md](ShaderManagement-Integration-Plan.md)** - 6-phase integration roadmap (Phase 3 complete)
- **[Shader-PreCompilation-Workflow.md](Shader-PreCompilation-Workflow.md)** - GLSL→SPIR-V compilation workflow
- **[ShaderLibraryArchitecture.md](ShaderLibraryArchitecture.md)** - Shader library design

## Completed Phases

### Phase 0: Library Compilation ✅
- ShaderManagement library compiling successfully
- Namespace conflicts resolved (EventBus, Hash, SPIRV-Reflect)

### Phase 1: Minimal Integration ✅
- ShaderLibraryNode loading shaders via ShaderBundleBuilder
- CashSystem cache logging activated
- Raw GLSL files replacing manual .spv loading

### Phase 2: Data-Driven Pipeline Creation ✅
- SPIRV reflection extracting vertex formats (vec4+vec2)
- All 14 shader stage types supported
- PipelineLayoutCacher sharing layouts
- Zero hardcoded shader assumptions

### Phase 3: Type-Safe UBO Updates ✅
- **Split SDI Architecture**: Generic `{content-hash}-SDI.h` + shader-specific `Names.h`
- **Content-Hash UUID**: Deterministic interface identification
- **Recursive UBO Extraction**: Complete struct definitions from SPIRV reflection
- **Matrix Detection**: Correct mat4/mat3 type identification via stride checking
- **Index-Based Linking**: Prevents dangling pointer bugs

## Generated Files

**Build-time shaders**: `generated/sdi/`
- Generic interfaces (shareable across shaders with same layout)

**Runtime shaders**: `binaries/generated/sdi/`
- Application-specific shader interfaces

**Example**:
```cpp
// Generic interface: 2071dff093caf4b3-SDI.h
namespace ShaderInterface::2071dff093caf4b3 {
    struct bufferVals {
        mat4 mvp;  // Correctly detected via stride checking
    };

    namespace Set0 {
        struct myBufferVals {
            static constexpr uint32_t BINDING = 0;
            using DataType = bufferVals;
        };
    };
}

// Shader-specific: Draw_ShaderNames.h
namespace Draw_Shader {
    namespace SDI = ShaderInterface::2071dff093caf4b3;
    using myBufferVals_t = SDI::Set0::myBufferVals;
}
```

## Phase 4 Objectives (Current)

1. ⏳ Use ShaderDataBundle for VkDescriptorSetLayout creation
2. ⏳ Extract push constants from reflection
3. ⏳ Size descriptor pools from reflection metadata
4. ⏳ Update nodes to use generated SDI headers
5. ⏳ Test cache persistence

## Key Implementation Files

- `ShaderManagement/src/SpirvReflector.cpp:177-220` - ExtractBlockMembers() with matrix detection
- `ShaderManagement/src/SpirvInterfaceGenerator.cpp:174-275` - GenerateNamesHeader()
- `ShaderManagement/include/ShaderManagement/SpirvReflectionData.h:102` - Index-based linking
- `RenderGraph/src/Nodes/ShaderLibraryNode.cpp:118-128` - Runtime SDI configuration

## Related

- See `memory-bank/systemPatterns.md` - SDI generation pattern (Pattern 10)
- See `memory-bank/activeContext.md` - Current phase status
