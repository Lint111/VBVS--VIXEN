# Refactoring Patterns Reference

Quick lookup for common refactoring patterns when improving node readability.

---

## Pattern 1: Device Validation

### Before (4 lines, repeated 20+ times)
```cpp
VulkanDevicePtr devicePtr = ctx.In(MyNodeConfig::VULKAN_DEVICE_IN);
if (!devicePtr) {
    throw std::runtime_error("MyNode: Device is null");
}
SetDevice(devicePtr);
```

### After (1 line, using helper)
```cpp
ValidateAndSetDevice<MyNodeConfig>(ctx, this);
```

### When to Apply
- Device validation at start of CompileImpl
- Any node that needs device access
- Consistent error messages across nodes

### Helper Location
`NodeHelpers/ValidationHelpers.h` → `ValidateAndSetDevice<NodeConfig, NodeType>()`

---

## Pattern 2: Cacher Registration & Lookup

### Before (16+ lines, repeated 5-8 times)
```cpp
auto& mainCacher = GetOwningGraph()->GetMainCacher();
if (!mainCacher.IsRegistered(std::type_index(typeid(MyWrapper)))) {
    mainCacher.RegisterCacher<MyCacher, MyWrapper, MyParams>(
        std::type_index(typeid(MyWrapper)),
        "MyResource",
        true  // device-dependent
    );
}
auto* cacher = mainCacher.GetCacher<MyCacher, MyWrapper, MyParams>(
    std::type_index(typeid(MyWrapper)),
    device
);
if (!cacher) {
    throw std::runtime_error("Failed to get cacher");
}
auto cached = cacher->GetOrCreate(params);
if (!cached || cached->handle == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to get or create resource");
}
```

### After (4-6 lines, using helpers)
```cpp
auto* cacher = RegisterCacherIfNeeded<MyCacher, MyWrapper, MyParams>(
    GetOwningGraph(), device, "MyResource", true
);
auto cached = GetOrCreateCached<MyCacher, MyWrapper>(cacher, params, "Resource");
ValidateCachedHandle(cached->handle, "VkHandle", "Resource");
```

### When to Apply
- Any node using RenderPassCacher, PipelineCacher, DescriptorSetLayoutCacher
- Creating or retrieving cached Vulkan resources
- Reducing cacher boilerplate

### Helper Location
`NodeHelpers/CacherHelpers.h` → `RegisterCacherIfNeeded()`, `GetOrCreateCached()`, `ValidateCachedHandle()`

---

## Pattern 3: Vulkan Structure Initialization

### Before (8-12 lines per struct)
```cpp
VkPipelineRasterizationStateCreateInfo rasterState{};
rasterState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
rasterState.pNext = nullptr;
rasterState.polygonMode = polygonMode;
rasterState.cullMode = cullMode;
rasterState.frontFace = frontFace;
rasterState.lineWidth = 1.0f;
rasterState.depthClampEnable = VK_FALSE;
rasterState.rasterizerDiscardEnable = VK_FALSE;
rasterState.depthBiasEnable = VK_FALSE;
```

### After (1 line, using builders)
```cpp
auto rasterState = CreateRasterizationState(polygonMode, cullMode, frontFace);
```

### When to Apply
- VkPipeline*CreateInfo initialization
- VkRenderPass structures (attachment, subpass, dependency)
- VkFramebufferCreateInfo
- VkImage/VkImageView creation info
- VkBufferCreateInfo

### Builder Functions Available
- Pipelines: DynamicState, VertexInput, InputAssembly, Rasterization, Multisample, DepthStencil, ColorBlend
- RenderPass: AttachmentDescription, AttachmentReference, SubpassDescription, SubpassDependency
- Resources: FramebufferInfo, ImageInfo, ImageViewInfo, BufferInfo

### Helper Location
`NodeHelpers/VulkanStructHelpers.h` → 18 builder functions

---

## Pattern 4: Enum String Parsing

### Before (4-8 lines per parser, 4 identical implementations)
```cpp
if (cullModeStr == "None") return VK_CULL_MODE_NONE;
if (cullModeStr == "Front") return VK_CULL_MODE_FRONT_BIT;
if (cullModeStr == "Back") return VK_CULL_MODE_BACK_BIT;
if (cullModeStr == "FrontAndBack") return VK_CULL_MODE_FRONT_AND_BACK;
throw std::runtime_error("Unknown cull mode: " + cullModeStr);

if (modeStr == "Fill") return VK_POLYGON_MODE_FILL;
if (modeStr == "Line") return VK_POLYGON_MODE_LINE;
if (modeStr == "Point") return VK_POLYGON_MODE_POINT;
throw std::runtime_error("Unknown polygon mode: " + modeStr);
// ... 2 more identical patterns
```

### After (1 line each)
```cpp
auto cullMode = ParseCullMode(cullModeStr);
auto polygonMode = ParsePolygonMode(modeStr);
auto topology = ParseTopology(topologyStr);
auto frontFace = ParseFrontFace(faceStr);
```

### When to Apply
- Parameter string-to-enum conversion in CompileImpl
- Configuration parsing from node parameters
- Graph definition loading

### Parsers Available
- CullMode, PolygonMode, Topology, FrontFace
- ImageLayout, AttachmentLoadOp, AttachmentStoreOp
- CompareOp, SampleCount

### Helper Location
`NodeHelpers/EnumParsers.h` → 10 parsers

---

## Pattern 5: Device-Local Buffer Allocation

### Before (25+ lines per buffer, repeated 3x in VoxelGridNode)
```cpp
// Create buffer
VkBufferCreateInfo bufferInfo{};
bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
bufferInfo.size = size;
bufferInfo.usage = usage;
bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
if (result != VK_SUCCESS) throw std::runtime_error(...);

// Query memory
VkMemoryRequirements memReq;
vkGetBufferMemoryRequirements(device, buffer, &memReq);

// Find memory type (10 lines of loops)
uint32_t memTypeIdx = UINT32_MAX;
for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((memReq.memoryTypeBits & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        memTypeIdx = i;
        break;
    }
}
if (memTypeIdx == UINT32_MAX) throw std::runtime_error(...);

// Allocate memory
VkMemoryAllocateInfo allocInfo{};
allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
allocInfo.allocationSize = memReq.size;
allocInfo.memoryTypeIndex = memTypeIdx;

result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
if (result != VK_SUCCESS) throw std::runtime_error(...);

vkBindBufferMemory(device, buffer, memory, 0);
```

### After (1 line, using helper)
```cpp
auto [buffer, memory] = CreateDeviceLocalBuffer(
    device, memProperties, size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    "OctreeNodesBuffer"
);
```

### When to Apply
- GPU buffer allocation in CompileImpl
- Device-local memory required
- Any Vulkan buffer creation

### Helper Functions
- `FindMemoryType()` - Locate memory type by requirements
- `CreateDeviceLocalBuffer()` - Allocate + bind buffer in one call
- `DestroyBuffer()` - Safe cleanup of both buffer and memory

### Helper Location
`NodeHelpers/BufferHelpers.h`

---

## Pattern 6: Long Method Extraction

### Before: Single 150-line method
```cpp
void MyNode::RecordDrawCommands(const Context& ctx) {
    // Validation (15 lines)
    VulkanDevicePtr device = ctx.In(...);
    if (!device) throw...;

    // Barrier setup (20 lines)
    VkImageMemoryBarrier barrier{};
    // ... field assignments

    // Descriptor binding (25 lines)
    VkDescriptorSet descriptorSet = ...;
    vkCmdBindDescriptorSets(...);

    // Render state (30 lines)
    vkCmdBindPipeline(...);
    vkCmdSetViewport(...);
    // ... many more commands

    // Recording (60 lines)
    for (auto& mesh : meshes) {
        vkCmdBindVertexBuffers(...);
        vkCmdDraw(...);
    }
}
```

### After: Multiple focused methods
```cpp
void MyNode::RecordDrawCommands(const Context& ctx) {
    ValidateAndSetDevice<MyConfig>(ctx, this);

    PreparePipelineBarriers();
    BindDescriptors();
    SetRenderState();
    RecordMeshDraws();
}

private:
    void PreparePipelineBarriers() {
        // 20 lines - Clear barrier setup
    }

    void BindDescriptors() {
        // 25 lines - Descriptor binding
    }

    void SetRenderState() {
        // 30 lines - Pipeline, viewport, scissor setup
    }

    void RecordMeshDraws() {
        // 60 lines - Per-mesh drawing
    }
```

### When to Apply
- Methods >50 lines
- Multiple distinct responsibilities
- Shared complexity across similar nodes

### Naming Convention
- Start with verb: Record, Build, Setup, Validate, Create, Destroy, Upload
- Be specific: RecordMeshDraws, not RecordDraws
- Order: Setup → Record → Cleanup pattern

### Declaration in Header
```cpp
private:
    // Primary orchestrator
    void RecordDrawCommands(const TypedExecuteContext& ctx) override;

    // Helper steps (extracted from primary)
    void PreparePipelineBarriers();
    void BindDescriptors();
    void SetRenderState();
    void RecordMeshDraws();
```

---

## Pattern 7: Cleanup in Steps

### Before: Scattered null-checks
```cpp
void CleanupImpl(TypedCleanupContext& ctx) {
    if (buffer1 != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->device, buffer1, nullptr);
        buffer1 = VK_NULL_HANDLE;
    }

    if (memory1 != VK_NULL_HANDLE) {
        vkFreeMemory(device->device, memory1, nullptr);
        memory1 = VK_NULL_HANDLE;
    }

    if (buffer2 != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->device, buffer2, nullptr);
        buffer2 = VK_NULL_HANDLE;
    }

    if (memory2 != VK_NULL_HANDLE) {
        vkFreeMemory(device->device, memory2, nullptr);
        memory2 = VK_NULL_HANDLE;
    }
    // ... 10+ more if-blocks
}
```

### After: Grouped logical cleanup
```cpp
void CleanupImpl(TypedCleanupContext& ctx) override {
    if (!device) return;
    vkDeviceWaitIdle(device->device);

    DestroyBuffer1Resources();
    DestroyBuffer2Resources();
    DestroyBuffer3Resources();
}

private:
    void DestroyBuffer1Resources() {
        if (buffer1 != VK_NULL_HANDLE) {
            vkDestroyBuffer(device->device, buffer1, nullptr);
            buffer1 = VK_NULL_HANDLE;
        }
        if (memory1 != VK_NULL_HANDLE) {
            vkFreeMemory(device->device, memory1, nullptr);
            memory1 = VK_NULL_HANDLE;
        }
    }

    void DestroyBuffer2Resources() { /* ... */ }
    void DestroyBuffer3Resources() { /* ... */ }
```

### When to Apply
- CleanupImpl with 50+ lines
- Multiple resource types to destroy
- Clear logical grouping (nodes, bricks, materials)

### Naming Convention
- Destroy* for resource destruction
- Group related resources (buffer + memory)
- Use descriptive names (DestroyOctreeNodesBuffer, DestroyPipelineCache)

---

## Quick Reference Table

| Pattern | Use When | Helper | Lines Saved |
|---------|----------|--------|-------------|
| Device Validation | Every CompileImpl | ValidateAndSetDevice() | 3 per node |
| Cacher Pattern | Using cached resources | RegisterCacherIfNeeded() + GetOrCreateCached() | 12+ per cacher |
| Struct Init | Vulkan struct creation | Create*Info() builders | 8+ per struct |
| Enum Parsing | String to enum conversion | Parse*() functions | 4+ per enum |
| Buffer Allocation | GPU memory allocation | CreateDeviceLocalBuffer() | 25+ per buffer |
| Method Extraction | Long methods (>50 lines) | Create private helper | Reorganizes code |
| Cleanup Grouping | Scattered destruction | Create Destroy*() helpers | 30+ in CleanupImpl |

---

## Checklist for Node Refactoring

When refactoring a node, work in this order:

- [ ] **Add includes** (all 5 helper headers)
- [ ] **Device validation** (Pattern 1) - First in CompileImpl
- [ ] **Cacher patterns** (Pattern 2) - For each cached resource
- [ ] **Struct builders** (Pattern 3) - Replace all VkXxxInfo inits
- [ ] **Enum parsing** (Pattern 4) - In parameter setup
- [ ] **Buffer allocation** (Pattern 5) - If allocating GPU memory
- [ ] **Extract long methods** (Pattern 6) - >50 lines to separate functions
- [ ] **Reorganize cleanup** (Pattern 7) - Group buffer destruction
- [ ] **Update header file** - Add new private method declarations
- [ ] **Test compilation** - Verify no errors/warnings
- [ ] **Verify behavior** - Ensure Vulkan calls identical

---

## Helper Library Includes

Add all needed headers to your node:
```cpp
#include "NodeHelpers/ValidationHelpers.h"
#include "NodeHelpers/CacherHelpers.h"
#include "NodeHelpers/VulkanStructHelpers.h"
#include "NodeHelpers/EnumParsers.h"
#include "NodeHelpers/BufferHelpers.h"  // Only if allocating buffers
```

---

## Real Example: VoxelGridNode CleanupImpl Refactoring

### Before (50 lines)
```cpp
void VoxelGridNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (!vulkanDevice) return;
    vkDeviceWaitIdle(vulkanDevice->device);

    if (octreeNodesBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeNodesBuffer, nullptr);
        octreeNodesBuffer = VK_NULL_HANDLE;
    }
    if (octreeNodesMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeNodesMemory, nullptr);
        octreeNodesMemory = VK_NULL_HANDLE;
    }

    if (octreeBricksBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeBricksBuffer, nullptr);
        octreeBricksBuffer = VK_NULL_HANDLE;
    }
    if (octreeBricksMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeBricksMemory, nullptr);
        octreeBricksMemory = VK_NULL_HANDLE;
    }

    if (octreeMaterialsBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeMaterialsBuffer, nullptr);
        octreeMaterialsBuffer = VK_NULL_HANDLE;
    }
    if (octreeMaterialsMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeMaterialsMemory, nullptr);
        octreeMaterialsMemory = VK_NULL_HANDLE;
    }
}
```

### After (10 lines)
```cpp
void VoxelGridNode::DestroyOctreeBuffers() {
    if (!vulkanDevice) return;

    // Destroy nodes (buffer + memory)
    if (octreeNodesBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkanDevice->device, octreeNodesBuffer, nullptr);
        octreeNodesBuffer = VK_NULL_HANDLE;
    }
    if (octreeNodesMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkanDevice->device, octreeNodesMemory, nullptr);
        octreeNodesMemory = VK_NULL_HANDLE;
    }

    // Similar for bricks and materials...
}

void VoxelGridNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("Destroying octree buffers");

    if (!vulkanDevice) return;
    vkDeviceWaitIdle(vulkanDevice->device);

    DestroyOctreeBuffers();
    NODE_LOG_INFO("Cleanup complete");
}
```

**Key improvements:**
- Clear intent: "Destroy octree buffers" (method name)
- Grouped resources: nodes with nodes, bricks with bricks
- Main method focuses on orchestration
- Easy to understand flow: Wait → Destroy → Log

---

## See Also
- [REFACTORING_GUIDE.md](./REFACTORING_GUIDE.md) - Complete refactoring overview
- [NodeHelpers/](./RenderGraph/include/NodeHelpers/) - All helper libraries
- [VoxelGridNode example](./RenderGraph/src/Nodes/VoxelGridNode.cpp) - Practical example
