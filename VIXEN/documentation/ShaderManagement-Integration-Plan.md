# ShaderManagement Integration Plan

**Status**: ShaderManagement library successfully compiled (October 30, 2025)
**Goal**: Replace MVP manual shader loading with full ShaderManagement integration for automatic descriptor layout generation and type-safe UBO updates

---

## Phase 1: Minimal Integration (NEXT SESSION START HERE)

### Objective
Replace manual shader loading in VulkanGraphApplication with ShaderLibraryNode using ShaderManagement, while keeping existing manual descriptor setup temporarily.

### Changes Required

#### 1.1 ShaderLibraryNode Enhancement
**File**: `RenderGraph/src/Nodes/ShaderLibraryNode.cpp`

**Current State** (lines 73-142):
- Has ShaderModuleCacher integration code
- Uses placeholder paths for SPIR-V files
- NODE_LOG_INFO macros present but disabled

**Changes**:
```cpp
#include <ShaderManagement/ShaderBundleBuilder.h>
#include <ShaderManagement/ShaderDataBundle.h>

void ShaderLibraryNode::Compile() {
    // 1. Create ShaderBundleBuilder
    ShaderManagement::ShaderBundleBuilder builder;
    builder.SetUuid("Draw_Shader");

    // 2. Load GLSL source files
    builder.AddStageFromFile(
        ShaderManagement::ShaderStage::Vertex,
        "shaders/Draw.vert",  // Raw GLSL
        "main"
    );
    builder.AddStageFromFile(
        ShaderManagement::ShaderStage::Fragment,
        "shaders/Draw.frag",
        "main"
    );

    // 3. Build bundle (compiles to SPIR-V, reflects metadata)
    auto result = builder.Build();
    if (!result.success) {
        throw std::runtime_error("Shader compilation failed: " + result.errorMessage);
    }

    // 4. Store bundle for downstream nodes
    shaderBundle_ = std::make_shared<ShaderManagement::ShaderDataBundle>(
        std::move(*result.bundle)
    );

    // 5. Create VkShaderModules for pipeline (temporary until VulkanShader removed)
    auto& stages = shaderBundle_->program.stages;
    for (auto& stage : stages) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = stage.spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = stage.spirvCode.data();

        VkShaderModule module;
        VkDevice device = /* get from DeviceNode output */;
        vkCreateShaderModule(device, &createInfo, nullptr, &module);

        // Store in outputs
        if (stage.stage == ShaderManagement::ShaderStage::Vertex) {
            vertexModule = module;
        } else if (stage.stage == ShaderManagement::ShaderStage::Fragment) {
            fragmentModule = module;
        }
    }

    // Output the bundle for DescriptorSetNode to consume later
    SetOutput("shader_bundle", shaderBundle_);
}
```

**New Outputs**:
- `shader_bundle` (std::shared_ptr<ShaderDataBundle>) - For descriptor automation
- `vertex_module` (VkShaderModule) - Temporary compatibility
- `fragment_module` (VkShaderModule) - Temporary compatibility

#### 1.2 Remove Manual Shader Loading
**File**: `source/VulkanGraphApplication.cpp` (lines 315-332)

**Action**: Comment out or delete manual shader loading:
```cpp
// REMOVED: Manual shader loading - now handled by ShaderLibraryNode
// uint32_t* vertSpv = ::ReadFile("../builtAssets/CompiledShaders/Draw.vert.spv", &vertSize);
// uint32_t* fragSpv = ::ReadFile("../builtAssets/CompiledShaders/Draw.frag.spv", &fragSize);
// triangleShader = new VulkanShader();
// triangleShader->BuildShaderModuleWithSPV(vertSpv, vertSize, fragSpv, fragSize, vulkanDevice);
```

#### 1.3 Source File Structure
**Create**: `shaders/` directory with raw GLSL files
```
shaders/
├── Draw.vert          # Vertex shader (GLSL source)
└── Draw.frag          # Fragment shader (GLSL source)
```

Copy content from existing `.spv` files' original GLSL sources.

#### 1.4 Update ShaderLibraryNode Header
**File**: `RenderGraph/include/Nodes/ShaderLibraryNode.h`

**Add**:
```cpp
#include <ShaderManagement/ShaderDataBundle.h>

private:
    std::shared_ptr<ShaderManagement::ShaderDataBundle> shaderBundle_;
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
```

### Validation Criteria
- ✅ Application compiles
- ✅ Shaders load through ShaderManagement
- ✅ VkShaderModules created successfully
- ✅ Rendering works identically to MVP
- ✅ CashSystem logs show shader caching activity

---

## Phase 2: Descriptor Automation

### Objective
Replace manual descriptor layout creation in DescriptorSetNode with automatic layout from ShaderDataBundle reflection data.

### Changes Required

#### 2.1 DescriptorSetNode Enhancement
**File**: `RenderGraph/src/Nodes/DescriptorSetNode.cpp`

**Current State**:
- Manually creates VkDescriptorSetLayoutBinding
- Hardcoded binding indices and types

**Changes**:
```cpp
void DescriptorSetNode::Compile() {
    // 1. Get ShaderDataBundle from ShaderLibraryNode
    auto bundle = GetInput<std::shared_ptr<ShaderManagement::ShaderDataBundle>>("shader_bundle");

    // 2. Extract descriptor layout from reflection data
    auto& descriptorLayout = bundle->descriptorLayout;
    auto& reflectionData = bundle->reflectionData;

    // 3. Build VkDescriptorSetLayoutBindings from reflection
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    for (auto& [setIndex, descriptorBindings] : reflectionData->descriptorSets) {
        if (setIndex != targetSetIndex_) continue;  // Only process requested set

        for (auto& desc : descriptorBindings) {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = desc.binding;
            binding.descriptorType = desc.descriptorType;
            binding.descriptorCount = desc.descriptorCount;
            binding.stageFlags = desc.stageFlags;
            binding.pImmutableSamplers = nullptr;

            bindings.push_back(binding);

            // Log descriptor info
            NODE_LOG_INFO("Descriptor [" << desc.binding << "] " << desc.name
                         << " - Type: " << DescriptorTypeToString(desc.descriptorType));
        }
    }

    // 4. Create VkDescriptorSetLayout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDevice device = GetInput<VkDevice>("device");
    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout_);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout from reflection data");
    }

    SetOutput("descriptor_set_layout", descriptorSetLayout_);
}
```

**New Inputs**:
- `shader_bundle` (from ShaderLibraryNode)

**Configuration**:
- `targetSetIndex_` - Which descriptor set index to create layout for (default 0)

#### 2.2 Connection Updates
**File**: `source/VulkanGraphApplication.cpp`

**Add connection**:
```cpp
// Connect shader bundle to descriptor node
graph.Connect(
    TypedConnection<std::shared_ptr<ShaderManagement::ShaderDataBundle>>(
        "shader_lib", "shader_bundle",
        "main_descriptors", "shader_bundle"
    )
);
```

### Validation Criteria
- ✅ Descriptor layouts match manual configuration
- ✅ Descriptors bind correctly at runtime
- ✅ No validation errors from Vulkan
- ✅ Reflection data logs show correct bindings

---

## Phase 3: Type-Safe UBO Updates

### Objective
Replace raw memory UBO updates with type-safe struct access using generated `.si.h` shader interface headers.

### Prerequisites
- ShaderManagement SDI (Shader Data Interface) generation enabled
- `.si.h` files generated during shader compilation

### Changes Required

#### 3.1 Enable SDI Generation
**File**: `RenderGraph/src/Nodes/ShaderLibraryNode.cpp`

**Add before Build()**:
```cpp
// Enable Shader Data Interface generation
ShaderManagement::SdiGeneratorConfig sdiConfig;
sdiConfig.outputDirectory = "generated/shader_interfaces/";
sdiConfig.namespacePrefix = "ShaderInterface";
sdiConfig.generateStructs = true;
sdiConfig.generateDescriptorInfo = true;

builder.EnableSdiGeneration(sdiConfig);
```

**Generated Files**:
```
generated/shader_interfaces/
└── Draw_Shader.si.h
```

**Example Generated Content**:
```cpp
namespace ShaderInterface::Draw_Shader {

// UBO at binding 0
struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPos;
};

// Push constants
struct PushConstants {
    glm::mat4 model;
};

// Descriptor set layout info
constexpr uint32_t SET_INDEX = 0;
constexpr uint32_t UBO_BINDING = 0;
constexpr uint32_t SAMPLER_BINDING = 1;

} // namespace ShaderInterface::Draw_Shader
```

#### 3.2 Update UBO Code
**File**: Application code updating UBOs (likely in GeometryRenderNode or similar)

**Before (Raw)**:
```cpp
// Manual struct definition (out of sync risk)
struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPos;
};

CameraUBO ubo;
ubo.view = camera.GetViewMatrix();
ubo.proj = camera.GetProjectionMatrix();
ubo.cameraPos = glm::vec4(camera.position, 1.0f);

void* data;
vkMapMemory(device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
memcpy(data, &ubo, sizeof(ubo));
vkUnmapMemory(device, uniformBufferMemory);
```

**After (Type-Safe)**:
```cpp
#include "generated/shader_interfaces/Draw_Shader.si.h"

using namespace ShaderInterface::Draw_Shader;

// Compile-time type safety - struct matches shader exactly
CameraUBO ubo;
ubo.view = camera.GetViewMatrix();
ubo.proj = camera.GetProjectionMatrix();
ubo.cameraPos = glm::vec4(camera.position, 1.0f);

void* data;
vkMapMemory(device, uniformBufferMemory, 0, sizeof(CameraUBO), 0, &data);
memcpy(data, &ubo, sizeof(CameraUBO));
vkUnmapMemory(device, uniformBufferMemory);
```

**Benefits**:
- Compile-time errors if shader UBO changes
- IDE autocomplete for UBO members
- No manual struct maintenance
- Guaranteed memory layout matches shader

#### 3.3 Push Constants Type Safety
**Example**:
```cpp
#include "generated/shader_interfaces/Draw_Shader.si.h"

using namespace ShaderInterface::Draw_Shader;

PushConstants pushConsts;
pushConsts.model = objectTransform;

vkCmdPushConstants(
    commandBuffer,
    pipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT,
    0,
    sizeof(PushConstants),
    &pushConsts
);
```

### Validation Criteria
- ✅ `.si.h` files generated successfully
- ✅ Code compiles with generated headers
- ✅ UBO updates use typed structs
- ✅ Shader modifications trigger recompilation and header regeneration
- ✅ IDE shows autocomplete for shader interface structs

---

## Phase 4: Pipeline Integration

### Objective
Use ShaderDataBundle descriptor layouts in GraphicsPipelineNode instead of manual pipeline layout creation.

### Changes Required

#### 4.1 GraphicsPipelineNode Enhancement
**File**: `RenderGraph/src/Nodes/GraphicsPipelineNode.cpp`

**Add Input**:
```cpp
auto bundle = GetInput<std::shared_ptr<ShaderManagement::ShaderDataBundle>>("shader_bundle");
```

**Use for Pipeline Layout**:
```cpp
// Get descriptor set layouts from reflection
std::vector<VkDescriptorSetLayout> setLayouts;
for (auto& [setIndex, _] : bundle->reflectionData->descriptorSets) {
    // Get layout from DescriptorSetNode for this set index
    setLayouts.push_back(GetDescriptorSetLayout(setIndex));
}

// Get push constant ranges from reflection
std::vector<VkPushConstantRange> pushConstantRanges;
for (auto& pcRange : bundle->reflectionData->pushConstants) {
    VkPushConstantRange range{};
    range.stageFlags = pcRange.stageFlags;
    range.offset = pcRange.offset;
    range.size = pcRange.size;
    pushConstantRanges.push_back(range);
}

// Create pipeline layout
VkPipelineLayoutCreateInfo layoutInfo{};
layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
layoutInfo.pSetLayouts = setLayouts.data();
layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
layoutInfo.pPushConstantRanges = pushConstantRanges.data();

vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_);
```

### Validation Criteria
- ✅ Pipeline layouts match shader requirements
- ✅ Push constants work correctly
- ✅ No validation layer errors
- ✅ Descriptor sets bind successfully

---

## Phase 5: CashSystem Validation & Optimization

### Objective
Verify caching works and add cache persistence across application restarts.

### Tasks

#### 5.1 Verify Cache Activity
**Action**: Run application and check logs for:
```
[ShaderModuleCacher::GetOrCreate] CACHE MISS for Draw_Vertex (key=abc123...)
[ShaderModuleCacher::GetOrCreate] Creating new shader module...
[ShaderModuleCacher::GetOrCreate] CACHE HIT for Draw_Vertex (key=abc123...)
```

**Expected Behavior**:
- First run: CACHE MISS for all shaders
- Second run (same shaders): CACHE HIT for all shaders
- Modified shader: CACHE MISS for changed shader only

#### 5.2 Add Cache Persistence
**File**: `CashSystem/src/main_cacher.cpp`

**Add Serialization**:
```cpp
void MainCacher::SaveCacheToDisk(const std::filesystem::path& cacheDir) {
    for (auto& [typeId, cacher] : m_cachers) {
        std::string filename = cacheDir / (cacher->GetTypeName() + ".cache");
        cacher->SerializeToFile(filename);
    }
}

void MainCacher::LoadCacheFromDisk(const std::filesystem::path& cacheDir) {
    for (auto& [typeId, cacher] : m_cachers) {
        std::string filename = cacheDir / (cacher->GetTypeName() + ".cache");
        if (std::filesystem::exists(filename)) {
            cacher->DeserializeFromFile(filename);
        }
    }
}
```

**Call on Startup/Shutdown**:
```cpp
// In VulkanGraphApplication::Initialize()
mainCacher.LoadCacheFromDisk(".vixen_cache/");

// In VulkanGraphApplication::Cleanup()
mainCacher.SaveCacheToDisk(".vixen_cache/");
```

#### 5.3 Integrate PipelineCacher
**File**: `RenderGraph/src/Nodes/GraphicsPipelineNode.cpp`

**Similar to ShaderModuleCacher**:
```cpp
void GraphicsPipelineNode::Compile() {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register PipelineCacher
    if (!mainCacher.IsRegistered(typeid(CashSystem::PipelineWrapper))) {
        mainCacher.RegisterCacher<...>(
            typeid(CashSystem::PipelineWrapper),
            "GraphicsPipeline",
            true
        );
    }

    auto pipelineCacher = mainCacher.GetCacher<...>();

    CashSystem::PipelineCreateParams params;
    params.vertexShaderKey = vertexShaderChecksum;
    params.fragmentShaderKey = fragmentShaderChecksum;
    params.renderPass = renderPass;
    params.viewportWidth = swapchainExtent.width;
    params.viewportHeight = swapchainExtent.height;
    // ... other params

    auto pipelineWrapper = pipelineCacher->GetOrCreate(params);
    graphicsPipeline_ = pipelineWrapper->pipeline;
}
```

### Validation Criteria
- ✅ Cache logs show HIT/MISS correctly
- ✅ Cache persists across application restarts
- ✅ Shader changes invalidate cache properly
- ✅ Performance improvement measurable (cache hits faster)

---

## Phase 6: Hot Reload (Future Enhancement)

### Objective
Enable runtime shader recompilation and hot-swap without restarting application.

### Architecture
```
FileWatcher → Detects shader change → ShaderManagement recompile →
  New ShaderDataBundle → Invalidate cache → Rebuild pipeline → Hot-swap
```

**Deferred to future session** - requires:
- File watching system
- Pipeline recreation without frame drops
- Resource synchronization
- State preservation

---

## Technical Debt & Future Work

### Known Issues to Address
1. **Hash Namespace Conflicts**: ShaderManagement uses FNV-1a instead of SHA256 due to global namespace collision with stbrumme Hash
2. **shader_tool Build Errors**: Optional build tool has compilation errors (not needed for runtime)
3. **NODE_LOG_INFO Disabled**: RenderGraph logging macros conditionally disabled, consider enabling for development

### Performance Optimizations
1. **Async Shader Compilation**: Use AsyncShaderBundleBuilder with worker threads
2. **Pipeline Cache**: Leverage VkPipelineCache for faster pipeline creation
3. **Parallel Compilation**: Compile multiple shaders concurrently

### Code Quality Improvements
1. **Error Handling**: Add better error messages for shader compilation failures
2. **Validation**: Add checks for descriptor mismatches
3. **Documentation**: Document shader interface generation workflow
4. **Unit Tests**: Add tests for reflection data extraction

---

## Risk Assessment

### High Risk
- **API Surface Changes**: ShaderDataBundle structure might not match all use cases
- **Descriptor Compatibility**: Existing descriptor update code may assume different layouts

### Medium Risk
- **Performance Regression**: Initial runs will be slower due to compilation overhead
- **Build Time**: Shader compilation during build increases iteration time

### Low Risk
- **Cache Invalidation**: FNV-1a hash adequate for cache keys despite not being cryptographic
- **Memory Usage**: ShaderDataBundle adds minimal overhead

---

## Success Metrics

### Phase 1 Success
- Application runs identically to MVP
- Shaders load through ShaderManagement
- Cache logs visible

### Full Integration Success
- Zero manual descriptor layout code
- All UBO updates use generated headers
- Cache hit rate > 90% after warm-up
- Hot reload functional (Phase 6)

---

## Session Boundaries

**This Session (October 30, 2025)**:
- ✅ ShaderManagement library compilation
- ✅ Namespace conflict resolution
- ✅ API mismatch fixes
- ✅ Full project build

**Next Session**:
- Start Phase 1: Minimal Integration
- ShaderLibraryNode implementation
- Remove MVP manual loading
- Verify CashSystem activation

**Future Sessions**:
- Phase 2: Descriptor automation
- Phase 3: Type-safe UBO updates
- Phase 4: Pipeline integration
- Phase 5: Cache optimization
