# Compiled Shaders (SPIRV)

This directory contains pre-compiled SPIRV shader bytecode for use with the ShaderLibraryNode.

## Available Shaders

### Draw.vert.spv
- **Source**: `Shaders/Draw.vert`
- **Stage**: Vertex
- **Inputs**:
  - `layout(location = 0)` vec4 pos
  - `layout(location = 1)` vec2 inUV
- **Outputs**:
  - `layout(location = 0)` vec2 outUV
- **Uniforms**:
  - `layout(binding = 0)` mat4 mvp
- **Features**: Vulkan coordinate system flip (Y-axis, Z depth [0,1])

### Draw.frag.spv
- **Source**: `Shaders/Draw.frag`
- **Stage**: Fragment
- **Inputs**:
  - `layout(location = 0)` vec2 uv
- **Outputs**:
  - `layout(location = 0)` vec4 outColor
- **Uniforms**:
  - `layout(binding = 1)` sampler2D tex
- **Push Constants**:
  - int constColorIndex
  - float mixerValue

## Compilation

Shaders are compiled using glslangValidator from the Vulkan SDK:

```bash
# Vertex shader
glslangValidator.exe -V Shaders/Draw.vert -o builtAssets/CompiledShaders/Draw.vert.spv

# Fragment shader
glslangValidator.exe -V Shaders/Draw.frag -o builtAssets/CompiledShaders/Draw.frag.spv
```

## Validation

Validate SPIRV bytecode with:

```bash
spirv-val builtAssets/CompiledShaders/Draw.vert.spv
spirv-val builtAssets/CompiledShaders/Draw.frag.spv
```

## Usage with ShaderLibraryNode

```cpp
// Register shader program
ShaderManagement::ShaderProgramDefinition drawProgram;
drawProgram.name = "DrawProgram";
drawProgram.pipelineType = ShaderManagement::PipelineTypeConstraint::Graphics;
drawProgram.stages = {
    {ShaderManagement::ShaderStage::Vertex,   "builtAssets/CompiledShaders/Draw.vert.spv"},
    {ShaderManagement::ShaderStage::Fragment, "builtAssets/CompiledShaders/Draw.frag.spv"}
};

uint32_t programId = shaderLibNode->RegisterProgram(drawProgram);
```

## Hot Reload

To recompile shaders during development:

```bash
# Recompile both shaders
glslangValidator.exe -V Shaders/Draw.vert -o builtAssets/CompiledShaders/Draw.vert.spv
glslangValidator.exe -V Shaders/Draw.frag -o builtAssets/CompiledShaders/Draw.frag.spv

# ShaderLibrary will detect file changes via timestamp checking
```

## Notes

- SPIRV magic number: `0x07230203`
- SPIRV version: 1.0 (Vulkan 1.0 compatible)
- All shaders use GLSL 450 (Vulkan GLSL)