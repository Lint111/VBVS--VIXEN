# Resource State Management

## Problem Statement

When working with resource arrays (e.g., swapchain images, descriptor sets), we currently recompile the entire array when any single element changes. This is inefficient:

**Current Behavior:**
```cpp
// Window resizes → SwapChain recreates ALL images → Framebuffer recreates ALL framebuffers
// Even if only 1 image changed, we rebuild all 3
```

**Performance Issues:**
- Unnecessary Vulkan object destruction/creation
- Cache invalidation across entire dependency chain
- O(N) recompilation when O(1) update is possible

## Solution: Per-Element State Flags

Add fine-grained state tracking to each resource in an array, enabling **selective recompilation**.

### Resource State Flags

```cpp
// Resource.h
/**
 * @brief Resource state flags for fine-grained updates
 *
 * Enables selective recompilation of array elements instead of
 * rebuilding entire arrays on every change.
 */
enum class ResourceState : uint32_t {
    Clean         = 0,        // No action needed
    Dirty         = 1 << 0,   // Needs recompilation
    Deleted       = 1 << 1,   // Marked for deletion
    Stale         = 1 << 2,   // Data outdated, needs refresh
    Locked        = 1 << 3,   // In use, cannot modify
    Transient     = 1 << 4,   // Temporary, delete after use
    Persistent    = 1 << 5,   // Keep across frames
};

inline ResourceState operator|(ResourceState a, ResourceState b) {
    return static_cast<ResourceState>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ResourceState operator&(ResourceState a, ResourceState b) {
    return static_cast<ResourceState>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasState(ResourceState flags, ResourceState check) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(check)) != 0;
}
```

### Updated Resource Class

```cpp
class Resource {
public:
    // ... existing members ...

    // State management
    ResourceState GetState() const { return state; }
    void SetState(ResourceState newState) { state = newState; }
    void AddState(ResourceState flags) {
        state = state | flags;
    }
    void RemoveState(ResourceState flags) {
        state = static_cast<ResourceState>(
            static_cast<uint32_t>(state) & ~static_cast<uint32_t>(flags)
        );
    }
    bool IsDirty() const { return HasState(state, ResourceState::Dirty); }
    bool IsDeleted() const { return HasState(state, ResourceState::Deleted); }
    bool IsLocked() const { return HasState(state, ResourceState::Locked); }

    void MarkDirty() { AddState(ResourceState::Dirty); }
    void MarkDeleted() { AddState(ResourceState::Deleted); }
    void MarkClean() { RemoveState(ResourceState::Dirty); }

    // Generation tracking (for staleness detection)
    uint64_t GetGeneration() const { return generation; }
    void IncrementGeneration() { generation++; }

private:
    ResourceState state = ResourceState::Clean;
    uint64_t generation = 0;  // Incremented on each update
};
```

## Usage Patterns

### Pattern 1: Selective Framebuffer Recompilation

**Scenario**: Window resize changes swapchain image 2 out of 3.

```cpp
// SwapChainNode::Compile() - After recreating swapchain
for (size_t i = 0; i < newImageCount; i++) {
    Resource* imageRes = GetOutput(SWAPCHAIN_IMAGES, i);

    if (i < oldImageCount) {
        // Image already existed - check if it changed
        VkImage oldImage = imageRes->GetImage();
        VkImage newImage = swapChainWrapper->scPublicVars.colorBuffers[i].image;

        if (oldImage != newImage) {
            imageRes->SetImage(newImage);
            imageRes->MarkDirty();  // Mark changed
            NODE_LOG_DEBUG("SwapChain image " + std::to_string(i) + " changed");
        } else {
            imageRes->MarkClean();  // Unchanged
        }
    } else {
        // New image added
        imageRes->SetImage(swapChainWrapper->scPublicVars.colorBuffers[i].image);
        imageRes->MarkDirty();
        NODE_LOG_DEBUG("SwapChain image " + std::to_string(i) + " added");
    }
}

// Mark deleted images
for (size_t i = newImageCount; i < oldImageCount; i++) {
    Resource* imageRes = GetOutput(SWAPCHAIN_IMAGES, i);
    imageRes->MarkDeleted();
}
```

```cpp
// FramebufferNode::Compile() - Selective recompilation
size_t colorAttachmentCount = GetInputCount(COLOR_ATTACHMENTS_Slot::index);

for (size_t i = 0; i < colorAttachmentCount; i++) {
    Resource* colorRes = GetInput(COLOR_ATTACHMENTS_Slot::index, i);

    // Check if this specific input changed
    if (colorRes->IsDirty()) {
        NODE_LOG_DEBUG("Rebuilding framebuffer " + std::to_string(i) + " (input dirty)");

        // Destroy old framebuffer
        if (framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device->device, framebuffers[i], nullptr);
        }

        // Recreate only this framebuffer
        VkImageView colorView = colorRes->GetValue<VkImageView>();
        // ... create framebuffer ...
        framebuffers[i] = newFramebuffer;

        // Mark output as dirty (downstream nodes need to react)
        Resource* fbRes = GetOutput(FRAMEBUFFERS_Slot::index, i);
        fbRes->MarkDirty();
    } else {
        NODE_LOG_DEBUG("Framebuffer " + std::to_string(i) + " unchanged (skipping)");
    }
}

// Handle deleted framebuffers
for (size_t i = 0; i < colorAttachmentCount; i++) {
    Resource* colorRes = GetInput(COLOR_ATTACHMENTS_Slot::index, i);
    if (colorRes->IsDeleted()) {
        if (framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device->device, framebuffers[i], nullptr);
            framebuffers[i] = VK_NULL_HANDLE;
        }
    }
}
```

**Performance Gain:**
```
Before: 3 framebuffers destroyed + 3 created = 6 Vulkan calls
After:  1 framebuffer destroyed + 1 created = 2 Vulkan calls (67% reduction)
```

### Pattern 2: Descriptor Set Hot Reload

**Scenario**: Shader texture changes, only update affected descriptor set.

```cpp
// TextureLoaderNode::OnEvent(ShaderReloadEvent)
void TextureLoaderNode::OnShaderReload(const ShaderReloadEvent& event) {
    // Only mark the specific texture as dirty
    uint32_t textureIndex = event.textureIndex;
    Resource* texRes = GetOutput(TEXTURE_Slot::index, textureIndex);

    // Reload texture from disk
    LoadTexture(event.texturePath, textureIndex);

    // Mark dirty (downstream descriptor sets will update)
    texRes->MarkDirty();
    texRes->IncrementGeneration();
}

// DescriptorSetNode::Compile() - Selective update
for (size_t i = 0; i < descriptorSetCount; i++) {
    Resource* textureRes = GetInput(TEXTURES_Slot::index, i);

    if (textureRes->IsDirty()) {
        // Update only this descriptor set
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageView = textureRes->GetImageView();
        imageInfo.sampler = sampler;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device->device, 1, &write, 0, nullptr);

        NODE_LOG_INFO("Updated descriptor set " + std::to_string(i) + " (hot reload)");
        textureRes->MarkClean();
    }
}
```

**Performance Gain:**
```
Before: 64 descriptor sets recreated = 64 vkAllocateDescriptorSets calls
After:  1 descriptor set updated = 1 vkUpdateDescriptorSets call (98.4% reduction)
```

### Pattern 3: Generation-Based Staleness Detection

**Scenario**: Detect if cached data is outdated.

```cpp
class PipelineCache {
private:
    std::unordered_map<VkRenderPass, VkPipeline> cachedPipelines;
    std::unordered_map<VkRenderPass, uint64_t> pipelineGenerations;

public:
    VkPipeline GetOrCreatePipeline(Resource* renderPassRes) {
        VkRenderPass renderPass = renderPassRes->GetValue<VkRenderPass>();
        uint64_t currentGen = renderPassRes->GetGeneration();

        auto it = cachedPipelines.find(renderPass);
        if (it != cachedPipelines.end()) {
            // Check if cached pipeline is stale
            if (pipelineGenerations[renderPass] == currentGen) {
                return it->second;  // Cache hit, up-to-date
            } else {
                // Cache hit, but stale - recreate
                vkDestroyPipeline(device, it->second, nullptr);
                cachedPipelines.erase(it);
            }
        }

        // Create new pipeline
        VkPipeline pipeline = CreatePipeline(renderPass);
        cachedPipelines[renderPass] = pipeline;
        pipelineGenerations[renderPass] = currentGen;

        return pipeline;
    }
};
```

### Pattern 4: Locked Resources (In-Flight Protection)

**Scenario**: Prevent modification of resources in use by GPU.

```cpp
// GeometryRenderNode::Execute()
void GeometryRenderNode::Execute(VkCommandBuffer cmd) {
    uint32_t imageIndex = swapChain->GetCurrentImageIndex();

    // Lock resources for this frame
    Resource* framebufferRes = GetInput(FRAMEBUFFER_Slot::index, imageIndex);
    framebufferRes->AddState(ResourceState::Locked);

    VkFramebuffer fb = framebufferRes->GetValue<VkFramebuffer>();

    // Begin render pass
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    // ... rendering ...
    vkCmdEndRenderPass(cmd);

    // Note: Unlock happens after fence wait in next frame
}

// FramebufferNode::Compile() - Respect locks
for (size_t i = 0; i < framebufferCount; i++) {
    Resource* fbRes = GetOutput(FRAMEBUFFERS_Slot::index, i);

    if (fbRes->IsLocked()) {
        NODE_LOG_WARNING("Framebuffer " + std::to_string(i) +
                        " locked (in use by GPU), deferring update");
        fbRes->AddState(ResourceState::Dirty);  // Mark for next opportunity
        continue;
    }

    if (fbRes->IsDirty()) {
        // Safe to recreate
        RecreateFramebuffer(i);
    }
}
```

## Integration with EventBus

State flags work seamlessly with event-driven invalidation:

```cpp
// WindowNode emits resize event
eventBus->EmitWindowResize(handle, newWidth, newHeight, oldWidth, oldHeight);

// SwapChainNode receives event, marks dirty images
void SwapChainNode::OnWindowResize(const WindowResizeEvent& event) {
    MarkForRecompile();  // Rebuild swapchain
}

// SwapChainNode::Compile() marks specific outputs dirty
for (size_t i = 0; i < newImageCount; i++) {
    if (/* image changed */) {
        GetOutput(SWAPCHAIN_IMAGES, i)->MarkDirty();
    }
}

// FramebufferNode::Compile() checks each input
for (size_t i = 0; i < colorAttachmentCount; i++) {
    if (GetInput(COLOR_ATTACHMENTS, i)->IsDirty()) {
        RecreateFramebuffer(i);  // Selective rebuild
    }
}
```

## State Lifecycle

```
[Clean] ──(resource modified)──> [Dirty] ──(recompile)──> [Clean]
   │                                │
   │                                └──(delete requested)──> [Deleted]
   │
   └──(in use by GPU)──> [Locked] ──(fence signaled)──> [Clean]
   │
   └──(data outdated)──> [Stale] ──(refresh)──> [Clean]
```

## Implementation Checklist

### Phase 1: Core State Management
- [ ] Add `ResourceState` enum to `Resource.h`
- [ ] Add state tracking members to `Resource` class
- [ ] Implement state manipulation methods (MarkDirty, etc.)
- [ ] Add generation tracking

### Phase 2: Node Integration
- [ ] Update `SwapChainNode::Compile()` to mark dirty outputs
- [ ] Update `FramebufferNode::Compile()` for selective rebuild
- [ ] Update `DepthBufferNode::Compile()` for state checks
- [ ] Update `RenderPassNode::Compile()` for dirty detection

### Phase 3: Lock Management
- [ ] Add lock/unlock methods
- [ ] Implement fence-based automatic unlocking in `SynchronizationManager`
- [ ] Add lock validation in Compile() methods

### Phase 4: Generation Tracking
- [ ] Implement generation-based cache invalidation
- [ ] Add generation checks to pipeline cache
- [ ] Add generation checks to descriptor set cache

### Phase 5: Event Integration
- [ ] Emit fine-grained state change events
- [ ] Update EventBus to propagate dirty flags
- [ ] Add state transition logging (debug builds)

## Performance Metrics

### Expected Improvements

**Swapchain Resize (3 images):**
```
Before: 3 FB destroy + 3 FB create = ~0.5ms
After:  1 FB destroy + 1 FB create = ~0.17ms (66% faster)
```

**Descriptor Hot Reload (64 sets, 1 changed):**
```
Before: 64 set allocations = ~2ms
After:  1 set update = ~0.03ms (98.5% faster)
```

**Pipeline Cache Hit Rate:**
```
Before: No staleness detection, cache never invalidates
After:  Generation-based invalidation, 100% correctness + 95%+ hit rate
```

## Debugging and Validation

### Debug Logging

```cpp
#ifdef _DEBUG
void Resource::SetState(ResourceState newState) {
    ResourceState oldState = state;
    state = newState;

    if (oldState != newState) {
        LOG_DEBUG("Resource state change: " +
                 StateToString(oldState) + " → " + StateToString(newState));
    }
}

std::string StateToString(ResourceState state) {
    std::string result;
    if (HasState(state, ResourceState::Dirty)) result += "Dirty|";
    if (HasState(state, ResourceState::Deleted)) result += "Deleted|";
    if (HasState(state, ResourceState::Stale)) result += "Stale|";
    if (HasState(state, ResourceState::Locked)) result += "Locked|";
    if (result.empty()) return "Clean";
    return result.substr(0, result.size() - 1);  // Remove trailing |
}
#endif
```

### Validation Layer Integration

```cpp
// Detect locked resource modification attempts
void Resource::SetImage(VkImage img) {
    if (IsLocked()) {
        LOG_ERROR("Attempted to modify locked resource (in use by GPU)");
        throw std::runtime_error("Cannot modify locked resource");
    }
    image = img;
    IncrementGeneration();
}
```

## Future Enhancements

### Automatic Dirty Propagation
```cpp
// When output marked dirty, automatically mark dependent inputs
void NodeInstance::MarkOutputDirty(uint32_t slotIndex, uint32_t arrayIndex) {
    Resource* res = GetOutput(slotIndex, arrayIndex);
    res->MarkDirty();

    // Propagate to dependent nodes
    for (NodeConnection& conn : GetOutgoingConnections()) {
        if (conn.sourceOutputIndex == slotIndex) {
            conn.targetNode->MarkInputDirty(conn.targetInputIndex, arrayIndex);
        }
    }
}
```

### State Transition Hooks
```cpp
class Resource {
public:
    using StateCallback = std::function<void(ResourceState, ResourceState)>;

    void OnStateChange(StateCallback callback) {
        stateCallbacks.push_back(callback);
    }

private:
    std::vector<StateCallback> stateCallbacks;
};

// Usage
resource->OnStateChange([](ResourceState old, ResourceState new) {
    if (HasState(new, ResourceState::Deleted)) {
        LOG_INFO("Resource marked for deletion");
    }
});
```

## Summary

Resource state flags provide:
1. **Fine-grained updates** - O(1) per element instead of O(N) for array
2. **Lock protection** - Prevent GPU-in-use resource modification
3. **Generation tracking** - Cache invalidation without false negatives
4. **Event integration** - Clean separation between invalidation and recompilation
5. **Performance gains** - 66-98% reduction in unnecessary Vulkan calls

This system is essential for real-world applications with dynamic content (texture streaming, shader hot reload, adaptive resolution).