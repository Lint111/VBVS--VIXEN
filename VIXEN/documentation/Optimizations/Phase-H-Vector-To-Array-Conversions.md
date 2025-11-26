# Phase H: Vector → Array Conversions

## Overview

This document tracks the conversion of heap-allocated `std::vector` to stack-allocated `std::array` for resources with predictable, bounded sizes. This eliminates heap allocations in hot paths and improves cache locality.

## Conversion Criteria

Convert `std::vector<T>` to `std::array<T, N>` when:
1. Maximum size is known at compile-time
2. Size is bounded by Vulkan spec limits (defined in VulkanLimits.h)
3. Resource is on the hot path (per-frame or per-swapchain)
4. Total stack usage remains within safe limits (~1-2 MB per frame)

**Do NOT convert** when:
- Size is truly unbounded or unpredictable
- Resource is allocated once at initialization
- Stack usage would be excessive (>100 KB for single array)

---

## Completed Conversions

### 1. **FramebufferNode.h** ✓
**Commit**: a846a85 (Phase H: Complete URM integration)

**Before**:
```cpp
std::vector<VkFramebuffer> framebuffers;
```

**After**:
```cpp
std::array<VkFramebuffer, MAX_SWAPCHAIN_IMAGES> framebuffers{};
uint32_t framebufferCount = 0;
```

**Benefits**:
- 100% heap allocation elimination in framebuffer storage
- Zero-copy access via `GetFramebufferArray()`
- Validation against MAX_SWAPCHAIN_IMAGES

**Files Modified**:
- `VIXEN/RenderGraph/include/Nodes/FramebufferNode.h`
- `VIXEN/RenderGraph/src/Nodes/FramebufferNode.cpp`

---

### 2. **VulkanSwapChain.h** ✓
**Current commit** (Phase H: Vector-to-array conversions)

#### 2a. SwapChainPrivateVariables::swapChainImages

**Before**:
```cpp
std::vector<VkImage> swapChainImages;
```

**After**:
```cpp
std::array<VkImage, Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES> swapChainImages{};
uint32_t swapChainImageCount = 0;  // Track actual count
```

**Benefits**:
- Eliminates heap allocation for swapchain image handles
- Maximum 4 images (MAX_SWAPCHAIN_IMAGES)
- ~32 bytes stack usage (4 × sizeof(VkImage))

#### 2b. SwapChainPublicVariables::colorBuffers

**Before**:
```cpp
std::vector<SwapChainBuffer> colorBuffers;
```

**After**:
```cpp
std::array<SwapChainBuffer, Vixen::RenderGraph::MAX_SWAPCHAIN_IMAGES> colorBuffers{};
// swapChainImageCount tracks actual count
```

**Benefits**:
- Eliminates heap allocation for color buffers
- ~64 bytes stack usage (4 × sizeof(SwapChainBuffer))
- Compatible vector conversion operator for legacy code

**Implementation Changes**:
- `VulkanSwapChain::CleanUp()`: Reset array and count
- `VulkanSwapChain::DestroySwapChain()`: Loop using count
- `VulkanSwapChain::CreateSwapChainColorImages()`: Validate count, use array
- `VulkanSwapChain::CreateColorImageView()`: Array indexing instead of push_back

**Files Modified**:
- `VIXEN/include/VulkanSwapChain.h`
- `VIXEN/source/VulkanSwapChain.cpp`

---

### 3. **SwapChainNode.h** ✓
**Current commit** (Phase H: Vector-to-array conversions)

**Before**:
```cpp
std::vector<bool> semaphoreInFlight;
```

**After**:
```cpp
std::array<bool, MAX_SWAPCHAIN_IMAGES> semaphoreInFlight{};
```

**Benefits**:
- Eliminates heap allocation for semaphore tracking
- ~4 bytes stack usage (4 × sizeof(bool))
- Zero-initialized by default

**Files Modified**:
- `VIXEN/RenderGraph/include/Nodes/SwapChainNode.h`

---

## Pending Conversions

### Candidates for Future Conversion

These vectors were analyzed but require further investigation or don't meet conversion criteria:

#### 1. **VulkanSwapChain::presentModes** ❌
```cpp
std::vector<VkPresentModeKHR> presentModes;  // Keep as vector
```
**Reason**: Variable size, query result from driver. Not performance-critical.

#### 2. **VulkanSwapChain::surfaceFormats** ❌
```cpp
std::vector<VkSurfaceFormatKHR> surfaceFormats;  // Keep as vector
```
**Reason**: Variable size, query result from driver. Not performance-critical.

---

## Performance Impact

### Memory Savings

**Per-Frame Heap Allocations Eliminated**:
- FramebufferNode: 1 allocation → 0 (vector → array)
- SwapChainNode: 1 allocation → 0 (semaphoreInFlight vector → array)
- VulkanSwapChain: 2 allocations → 0 (swapChainImages, colorBuffers)

**Total**: ~4 heap allocations eliminated per frame/swapchain lifecycle

### Stack Usage Added

- FramebufferNode: ~32 bytes (4 × VkFramebuffer)
- SwapChainPrivateVariables: ~32 bytes (4 × VkImage)
- SwapChainPublicVariables: ~64 bytes (4 × SwapChainBuffer)
- SwapChainNode: ~4 bytes (4 × bool)

**Total**: ~132 bytes (well within safe limits)

### Cache Locality

Stack-allocated arrays provide better cache locality compared to heap-allocated vectors, especially for small, fixed-size collections.

---

## Integration with ResourceVariant

### Current Status ✓

**Status**: `std::array<T, N>` is **fully supported** by the ResourceVariant type system.

**Existing Support** (in ResourceTypeTraits.h):

```cpp
// StripContainer specialization for std::array<T, N>
template<typename T, size_t N>
struct StripContainer<std::array<T, N>> {
    using Type = T;
    static constexpr bool isContainer = true;
    static constexpr bool isVector = false;
    static constexpr bool isArray = true;
    static constexpr size_t arraySize = N;
};
```

**Type Traits Exposed**:
- `ResourceTypeTraits<T>::isArray` - Compile-time check for array types
- `ResourceTypeTraits<T>::arraySize` - Compile-time array size
- `ResourceTypeTraits<T>::isVector` - Distinguish vector vs array
- `StripContainer<T>::Type` - Extract element type

**Files Confirmed**:
- `VIXEN/RenderGraph/include/Data/Core/ResourceTypeTraits.h` - Lines 48-56
- `VIXEN/RenderGraph/include/Data/Core/ResourceVariant.h` - Uses type traits

### Remaining Work

**StackArray<T, N>** support (for RequestStackResource pattern):
- StackArray is separate from ResourceVariant (stack-only allocations)
- No ResourceVariant integration needed (different allocation strategy)
- See StackResourceHandle.h for StackArray implementation

---

## Testing

### Validation Required

1. **Build Verification**: Ensure all conversions compile without errors
2. **Runtime Testing**: Verify swapchain creation with 2, 3, and 4 images
3. **Bounds Checking**: Validate MAX_SWAPCHAIN_IMAGES enforcement
4. **Integration Testing**: Test with full render graph execution

### Known Issues

None at this time.

---

## Future Work

1. **Descriptor Sets**: Investigate converting descriptor-related vectors
2. **Command Buffers**: Explore fixed-size command buffer arrays
3. **Per-Frame Resources**: Convert per-frame resource vectors where bounded
4. **ResourceVariant Integration**: Complete array type support

---

## References

- **VulkanLimits.h**: Compile-time constants for array sizes
- **Phase H Documentation**: Phase-H-ResourcePool-Design.md
- **Stack Optimization**: Phase-H-Node-Integration.md

---

## Summary

**Conversions Completed**: 3
**Heap Allocations Eliminated**: ~4 per frame/lifecycle
**Stack Usage Added**: ~132 bytes
**Performance Benefit**: Reduced memory fragmentation, better cache locality

All conversions maintain backward compatibility through conversion operators and careful index tracking.
