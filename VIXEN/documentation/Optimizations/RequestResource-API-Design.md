# RequestResource API: Resource Creation Through URM

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: API Design
**Related**: CentralURM-TransparentIntegration.md

---

## Problem Statement

**Current**: Nodes create resources and then set them via `ctx.Out()`:
```cpp
VkBuffer buffer;
vkCreateBuffer(device, &info, nullptr, &buffer);  // Create
ctx.Out<VkBuffer>(0) = buffer;                     // Set (accessor)
```

**Issue**: URM doesn't know about the resource until AFTER it's set. No tracking during creation.

**Solution**: `ctx.RequestResource<T>()` - request URM-managed resource BEFORE creation.

---

## API Design

### ResourceHandle<T>

Wrapper returned by `RequestResource()` that bridges node code and URM:

```cpp
template<typename T>
class ResourceHandle {
public:
    /**
     * @brief Get pointer for writing resource
     *
     * Usage: vkCreateBuffer(device, &info, nullptr, handle.GetPtr());
     */
    T* GetPtr();

    /**
     * @brief Set resource value (alternative to GetPtr)
     *
     * Usage: handle.Set(myBuffer);
     */
    void Set(T value);

    /**
     * @brief Get resource value (read access)
     */
    T& Get();
    const T& Get() const;

    /**
     * @brief Commit resource to URM (automatic on destruction)
     *
     * Explicitly notify URM that resource creation is complete.
     * Called automatically in destructor if not called explicitly.
     */
    void Commit();

private:
    friend class TypedCompileContext;

    ResourceHandle(
        NodeInstance* owner,
        uint32_t slotIndex,
        uint32_t arrayIndex,
        T* storage,
        ResourceManagement::UnifiedRM_Base* rm
    );

    NodeInstance* owner_;
    uint32_t slotIndex_;
    uint32_t arrayIndex_;
    T* storage_;  // Points to bundle storage (bundles[arrayIndex].outputs[slotIndex])
    ResourceManagement::UnifiedRM_Base* rm_;  // URM tracking
    bool committed_ = false;
};
```

---

## Context Integration

### TypedCompileContext Extension

```cpp
template<typename ConfigType>
class TypedCompileContext : public CompileContext {
public:
    /**
     * @brief Request URM-managed resource for output slot
     *
     * This is the NEW resource creation API. It:
     * 1. Allocates storage in bundle (bundles[arrayIndex].outputs[slotIndex])
     * 2. Registers with central URM for tracking
     * 3. Returns handle for node to populate resource
     *
     * @param slotIndex Output slot index
     * @param strategy Allocation strategy (Stack/Heap/Device)
     * @return ResourceHandle for populating the resource
     *
     * Usage:
     * @code
     * auto buffer = ctx.RequestResource<VkBuffer>(0, AllocStrategy::Device);
     * vkCreateBuffer(device, &info, nullptr, buffer.GetPtr());
     * buffer.Commit();  // Optional - auto-commits on destruction
     * @endcode
     */
    template<typename T>
    ResourceHandle<T> RequestResource(
        uint32_t slotIndex,
        ResourceManagement::AllocStrategy strategy = ResourceManagement::AllocStrategy::Automatic
    );

    // Existing accessors remain unchanged:
    template<typename T>
    T& Out(uint32_t slotIndex);  // Still works as accessor/setter
};
```

---

## Usage Patterns

### Pattern 1: Explicit Request (Recommended)

```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    // Request URM-managed resource
    auto buffer = ctx.RequestResource<VkBuffer>(OUTPUT_BUFFER_SLOT, AllocStrategy::Device);

    // Create Vulkan resource
    VkBufferCreateInfo createInfo = {};
    createInfo.size = 1024;
    createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    vkCreateBuffer(device, &createInfo, nullptr, buffer.GetPtr());

    // Commit to URM (optional - auto-commits on scope exit)
    buffer.Commit();

    // ctx.Out() now available as normal:
    // VkBuffer& b = ctx.Out<VkBuffer>(OUTPUT_BUFFER_SLOT);  // Works!
}
```

### Pattern 2: Set After Creation

```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    auto buffer = ctx.RequestResource<VkBuffer>(OUTPUT_BUFFER_SLOT);

    VkBuffer vkBuffer;
    vkCreateBuffer(device, &info, nullptr, &vkBuffer);

    buffer.Set(vkBuffer);  // Updates bundle + URM
    // Auto-committed
}
```

### Pattern 3: Backward Compatible (Still Works)

```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    // Old code still works (passive tracking):
    VkBuffer buffer;
    vkCreateBuffer(device, &info, nullptr, &buffer);
    ctx.Out<VkBuffer>(0) = buffer;  // ✅ Still works, passively tracked
}
```

---

## Implementation Flow

### Step 1: RequestResource() Called

```cpp
template<typename T>
ResourceHandle<T> TypedCompileContext::RequestResource(
    uint32_t slotIndex,
    AllocStrategy strategy
) {
    // 1. Get bundle storage (existing mechanism)
    T* storage = GetOutputStorage<T>(slotIndex);

    // 2. Register with central URM (NEW)
    if (node_->GetOwningGraph()) {
        node_->GetOwningGraph()->GetBudgetManager()->RegisterResource(
            node_, slotIndex, taskIndex_, storage, strategy
        );
    }

    // 3. Get URM tracking object
    auto* rm = node_->GetOwningGraph()->GetBudgetManager()->GetResource(
        node_, slotIndex, taskIndex_
    );

    // 4. Return handle
    return ResourceHandle<T>(node_, slotIndex, taskIndex_, storage, rm);
}
```

### Step 2: Node Populates Resource

```cpp
// Node writes to handle.GetPtr():
vkCreateBuffer(device, &info, nullptr, buffer.GetPtr());
// *buffer.GetPtr() = actualBuffer;
```

### Step 3: Commit (Automatic or Explicit)

```cpp
void ResourceHandle<T>::Commit() {
    if (committed_) return;

    // Notify URM that resource is ready
    if (rm_) {
        rm_->Set(*storage_);  // Update URM with actual resource
    }

    committed_ = true;
}

// Auto-commit on destruction
~ResourceHandle() {
    if (!committed_) {
        Commit();
    }
}
```

---

## Bundle Index = arrayIndex Mapping

**Yes, arrayIndex IS the bundle index!**

```cpp
// Phase F architecture:
std::vector<Bundle> bundles;  // NodeInstance member

struct Bundle {
    std::vector<Resource*> inputs;   // inputs[slotIndex]
    std::vector<Resource*> outputs;  // outputs[slotIndex]
};

// Access pattern:
Resource* r = bundles[arrayIndex].outputs[slotIndex];
//                    ^^^^^^^^^^           ^^^^^^^^^
//                    bundle/task          which output
```

For URM ResourceKey:
- `owner`: NodeInstance* (which node)
- `slotIndex`: uint32_t (which output slot: 0, 1, 2...)
- `arrayIndex`: uint32_t (which bundle/task: 0 for single-task, 0..N for arrayable)

---

## Benefits

✅ **Explicit Resource Lifecycle** - URM knows about resource from creation
✅ **Type-Safe** - Template parameters ensure correctness
✅ **Automatic Tracking** - No manual registration needed
✅ **Backward Compatible** - Old ctx.Out() still works
✅ **Clear Intent** - RequestResource signals "I'm creating a resource"
✅ **RAII-Friendly** - Auto-commit on scope exit

---

## Migration Strategy

### Phase 1: Optional (Current)
- RequestResource() available but optional
- Nodes continue using ctx.Out()
- Passive tracking when ctx.Out() is set

### Phase 2: Gradual Migration
- New nodes use RequestResource()
- High-value nodes (large resources) migrated first
- Old nodes continue working

### Phase 3: Enforcement (Future)
- All nodes use RequestResource()
- ctx.Out() becomes read-only accessor
- Full URM tracking coverage

---

## Example: SwapChainNode Migration

### Before (Current):
```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    // Create swapchain
    VkSwapchainKHR swapchain;
    vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);

    // Set output
    ctx.Out<VkSwapchainKHR>(SWAPCHAIN_SLOT) = swapchain;

    // Get images
    std::vector<VkImage> images = GetSwapchainImages(swapchain);
    ctx.Out<std::vector<VkImage>>(IMAGES_SLOT) = images;
}
```

### After (With RequestResource):
```cpp
void CompileImpl(TypedCompileContext& ctx) override {
    // Request URM-managed swapchain
    auto swapchain = ctx.RequestResource<VkSwapchainKHR>(
        SWAPCHAIN_SLOT,
        AllocStrategy::Device
    );

    vkCreateSwapchainKHR(device, &createInfo, nullptr, swapchain.GetPtr());
    swapchain.Commit();

    // Request URM-managed image array
    auto images = ctx.RequestResource<std::vector<VkImage>>(
        IMAGES_SLOT,
        AllocStrategy::Heap
    );

    *images.GetPtr() = GetSwapchainImages(swapchain.Get());
    images.Commit();
}
```

---

## Implementation Checklist

- [ ] Implement ResourceHandle<T> template class
- [ ] Add RequestResource() to TypedCompileContext
- [ ] Update ResourceBudgetManager::RegisterResource() to handle bundles
- [ ] Implement auto-commit in ResourceHandle destructor
- [ ] Add GetOutputStorage() helper to TypedCompileContext
- [ ] Unit tests for ResourceHandle lifecycle
- [ ] Integration tests with SwapChainNode
- [ ] Documentation updates

---

**End of Document**
