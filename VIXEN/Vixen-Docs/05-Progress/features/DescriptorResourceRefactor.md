# Descriptor Resource Reference Refactor

## Status: PLANNED

**HacknPlan Task:** #61
**Design Element:** Architecture Overview (ID: 8)
**Estimated Effort:** 7 hours
**Priority:** High

## Problem Statement

The `DescriptorResourceEntry` struct stores `DescriptorHandleVariant` (VkBuffer/VkImageView) by VALUE. When source nodes recompile, stored handles become stale pointers to destroyed Vulkan resources.

**Root Cause:** `DescriptorResourceGathererNode.cpp:583-584`
```cpp
auto handle = resource->GetDescriptorHandle();  // Snapshot
resourceArray_[binding].handle = handle;        // Becomes stale
```

**Symptoms:**
- Vulkan validation error: "vkUpdateDescriptorSets called with invalid buffer handle"
- Occurs on swapchain resize, resolution change
- Debugging cost: 4-8 hours per occurrence

## Solution: Lazy Handle Extraction

Store `Resource*` pointer instead of extracted handle. Extract handle lazily at bind time.

### Modified DescriptorResourceEntry

```cpp
struct DescriptorResourceEntry {
    Resource* resource = nullptr;  // Pointer, not value
    SlotRole slotRole = static_cast<SlotRole>(0);
    Debug::IDebugCapture* debugCapture = nullptr;

    DescriptorHandleVariant GetHandle() const {
        return resource ? resource->GetDescriptorHandle()
                        : DescriptorHandleVariant{std::monostate{}};
    }
};
```

## Implementation Plan

| Phase | Files | Changes | Hours |
|-------|-------|---------|-------|
| 1 | CompileTimeResourceSystem.h:440-448 | Struct refactor | 1 |
| 2 | DescriptorResourceGathererNode.cpp:581-587 | Store Resource* | 2 |
| 3 | DescriptorSetNode.cpp:765+ | Lazy GetHandle() | 2 |
| 4 | Testing | Validation | 2 |

## Acceptance Criteria

- [ ] DescriptorResourceEntry stores Resource* pointer
- [ ] GetHandle() extracts lazily at bind time
- [ ] No stale handle validation errors on swapchain resize
- [ ] Per-frame refresh workaround removed
- [ ] All 4 benchmark pipelines pass
- [ ] Shader counters still functional

## Industry Reference

- **Unreal RDG:** `FRDGResourceRef` pattern
- **Frostbite:** ResourceHandle with generation counters
- **Unity HDRP:** `RTHandle` version tracking

## Related Documentation

- [[01-Architecture/RenderGraph-System]]
- [[03-Research/Vulkan-Descriptor-Management]]

## Tags

#refactor #vulkan #descriptor-management #render-graph
