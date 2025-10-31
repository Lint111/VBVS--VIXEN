# Node Config API Improvements - October 2025

**Enhancement**: Replace magic bool with named enum, add count aliases for better readability

---

## Changes Made

### 1. SlotArrayMode Enum (Replaces Magic Bool)

**Before** (Magic bool):
```cpp
CONSTEXPR_NODE_CONFIG(MyNodeConfig, 1, 1, false) {  // What does false mean?
    // ...
};
```

**After** (Named enum):
```cpp
CONSTEXPR_NODE_CONFIG(MyNodeConfig, 1, 1, SlotArrayMode::Single) {  // Clear intent!
    // ...
};
```

**Enum Definition**:
```cpp
enum class SlotArrayMode : uint8_t {
    Single = 0,  // Single slot only (e.g., one framebuffer)
    Array = 1    // Array of slots (e.g., multiple color attachments)
};
```

### 2. Count Aliases (Better Documentation)

**Added Helper Types** (for documentation in struct body):
```cpp
template<size_t N> struct InputCount  { static constexpr size_t value = N; };
template<size_t N> struct OutputCount { static constexpr size_t value = N; };

// Convenience aliases
using NoInputs = InputCount<0>;
using OneInput = InputCount<1>;
using TwoInputs = InputCount<2>;
using ThreeInputs = InputCount<3>;

using NoOutputs = OutputCount<0>;
using OneOutput = OutputCount<1>;
using TwoOutputs = OutputCount<2>;
using ThreeOutputs = OutputCount<3>;
```

**Usage** (for better self-documentation):
```cpp
struct MyNodeConfig : public ResourceConfigBase<1, 2, SlotArrayMode::Array> {
    // Optional: Add named constants for clarity in struct body
    static constexpr auto INPUTS = OneInput::value;   // Documentation
    static constexpr auto OUTPUTS = TwoOutputs::value; // Documentation
    
    CONSTEXPR_INPUT(TEXTURE, VkImage, 0, false);
    CONSTEXPR_OUTPUT(COLOR0, VkImage, 0, false);
    CONSTEXPR_OUTPUT(COLOR1, VkImage, 1, false);
};
```

---

## Migration Summary

**All Node Configs Updated**:
- ✅ CommandPoolNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ WindowNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ VertexBufferNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ SwapChainNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ ShaderLibraryNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ RenderPassNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ FramebufferNodeConfig: `true` → `SlotArrayMode::Array` ⭐
- ✅ DeviceNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ DescriptorSetNodeConfig: `false` → `SlotArrayMode::Single`
- ✅ DepthBufferNodeConfig: `false` → `SlotArrayMode::Single`

**FramebufferNodeConfig** is the only node using `SlotArrayMode::Array` (supports multiple color attachments).

---

## Benefits

### 1. Readability ✅
```cpp
// Before: What does 'true' mean?
CONSTEXPR_NODE_CONFIG(FramebufferNodeConfig, 5, 1, true)

// After: Clear semantic meaning
CONSTEXPR_NODE_CONFIG(FramebufferNodeConfig, 5, 1, SlotArrayMode::Array)
```

### 2. Type Safety ✅
```cpp
// Compiler error if you use wrong type
CONSTEXPR_NODE_CONFIG(MyConfig, 1, 1, 42)  // ❌ Error: expects SlotArrayMode

// Correct
CONSTEXPR_NODE_CONFIG(MyConfig, 1, 1, SlotArrayMode::Single)  // ✅
```

### 3. Discoverability ✅
IDE autocomplete shows `SlotArrayMode::Single` | `SlotArrayMode::Array` instead of `true/false`.

### 4. Future Extension ✅
Easy to add more modes without breaking existing code:
```cpp
enum class SlotArrayMode : uint8_t {
    Single = 0,
    Array = 1,
    DynamicArray = 2  // Future: runtime-sized arrays
};
```

---

## Backward Compatibility

**Legacy bool compatibility maintained**:
```cpp
template<size_t NumInputs, size_t NumOutputs, SlotArrayMode ArrayMode = SlotArrayMode::Single>
struct ResourceConfigBase {
    static constexpr SlotArrayMode ARRAY_MODE = ArrayMode;
    
    // Legacy compatibility (deprecated - use ARRAY_MODE instead)
    static constexpr bool ALLOW_INPUT_ARRAYS = (ArrayMode == SlotArrayMode::Array);
};
```

Code accessing `ALLOW_INPUT_ARRAYS` still works, but `ARRAY_MODE` is preferred.

---

## Code Examples

### Example 1: Simple Single-Slot Node

```cpp
CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 0, 1, SlotArrayMode::Single) {
    CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);
    
    static constexpr const char* PARAM_WIDTH = "width";
    static constexpr const char* PARAM_HEIGHT = "height";
    
    WindowNodeConfig() {
        HandleDescriptor surfaceDesc{"VkSurfaceKHR"};
        INIT_OUTPUT_DESC(SURFACE, "surface", ResourceLifetime::Persistent, surfaceDesc);
    }
};
```

### Example 2: Array-Capable Node

```cpp
CONSTEXPR_NODE_CONFIG(FramebufferNodeConfig, 5, 1, SlotArrayMode::Array) {
    // Can accept multiple inputs per slot
    CONSTEXPR_INPUT(COLOR_ATTACHMENTS, VkImageView, 0, false);  // Array of color attachments
    CONSTEXPR_INPUT(DEPTH_ATTACHMENT, VkImageView, 1, true);    // Optional single depth
    CONSTEXPR_INPUT(RENDER_PASS, VkRenderPass, 2, false);
    CONSTEXPR_INPUT(WIDTH, uint32_t, 3, false);
    CONSTEXPR_INPUT(HEIGHT, uint32_t, 4, false);
    
    CONSTEXPR_OUTPUT(FRAMEBUFFER, VkFramebuffer, 0, false);
    
    // Node can call:
    // In(COLOR_ATTACHMENTS, 0), In(COLOR_ATTACHMENTS, 1), etc.
};
```

### Example 3: Using Named Constants (Optional)

```cpp
struct MyNodeConfig : public ResourceConfigBase<2, 3, SlotArrayMode::Single> {
    // Named constants for self-documentation
    static constexpr auto INPUTS = TwoInputs::value;
    static constexpr auto OUTPUTS = ThreeOutputs::value;
    
    CONSTEXPR_INPUT(ALBEDO, VkImage, 0, false);
    CONSTEXPR_INPUT(NORMAL, VkImage, 1, false);
    
    CONSTEXPR_OUTPUT(COLOR, VkImage, 0, false);
    CONSTEXPR_OUTPUT(DEPTH, VkImage, 1, false);
    CONSTEXPR_OUTPUT(VELOCITY, VkImage, 2, false);
};
```

---

## Build Status

**Last Build**: October 23, 2025  
**Target**: RenderGraph  
**Result**: ✅ **Success** - All 10 node configs updated, zero errors  

**Libraries Built**:
- EventBus.lib ✅
- Logger.lib ✅
- ShaderManagement.lib ✅
- VulkanResources.lib ✅
- RenderGraph.lib ✅

---

## Recommendation for Future Node Configs

**Preferred Style**:
```cpp
CONSTEXPR_NODE_CONFIG(MyNewNodeConfig, 1, 2, SlotArrayMode::Single) {
    // Input slots
    CONSTEXPR_INPUT(INPUT_IMAGE, VkImage, 0, false);
    
    // Output slots
    CONSTEXPR_OUTPUT(OUTPUT_IMAGE, VkImage, 0, false);
    CONSTEXPR_OUTPUT(DEPTH, VkImageView, 1, true);  // Optional output
    
    // Parameter names
    static constexpr const char* PARAM_QUALITY = "quality";
    
    MyNewNodeConfig() {
        // Runtime descriptor initialization
        ImageDescriptor imgDesc{};
        imgDesc.width = 0;  // Dynamic
        imgDesc.height = 0; // Dynamic
        imgDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
        
        INIT_INPUT_DESC(INPUT_IMAGE, "input_image", ResourceLifetime::Transient, imgDesc);
        INIT_OUTPUT_DESC(OUTPUT_IMAGE, "output_image", ResourceLifetime::Transient, imgDesc);
        INIT_OUTPUT_DESC(DEPTH, "depth", ResourceLifetime::Transient, imgDesc);
    }
};
```

**Key Points**:
- ✅ Use `SlotArrayMode::Single` or `SlotArrayMode::Array` (no bool)
- ✅ Keep numeric counts (macro doesn't support named counts yet)
- ✅ Add named constants in struct body for self-documentation (optional)
- ✅ Document slot purpose in comments

---

## Summary

**What Changed**: Replaced magic `true/false` with semantic `SlotArrayMode` enum  
**Why**: Improved readability, type safety, and discoverability  
**Impact**: All 10 node configs updated, backward compatible  
**Status**: ✅ Complete, builds successfully

This enhancement aligns with the project's **Type Safety First** principle and improves code maintainability.
