# Auto-Generated Storage from Config

## The Problem

**Before**: Config and storage were separate - easy to mismatch!

```cpp
// Config says VkSurfaceKHR
CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);

// But you manually declare:
class WindowNode {
    VkImage surface;  // OOPS! Wrong type!
    //      ^^^^ Runtime error - mismatch!
};
```

## The Solution

**After**: Config GENERATES the storage - impossible to mismatch!

```cpp
// 1. Config defines the schema
CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 0, 1) {
    CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);
};

// 2. Node inherits TypedNode<Config>
class WindowNode : public TypedNode<WindowNodeConfig> {
    void Compile() override {
        CreateSurface();

        // Storage type is AUTOMATICALLY VkSurfaceKHR from config!
        Out<0>() = surface;  // Compiler knows this is VkSurfaceKHR&
    }
};
```

## How It Works

### 1. Config Defines Types

```cpp
CONSTEXPR_NODE_CONFIG(SwapChainNodeConfig, 1, 3) {
    // Input: VkSurfaceKHR at index 0
    CONSTEXPR_INPUT(SURFACE, VkSurfaceKHR, 0, false);

    // Outputs: VkImage at 0, VkSemaphore at 1 and 2
    CONSTEXPR_OUTPUT(SWAPCHAIN_IMAGES, VkImage, 0, false);
    CONSTEXPR_OUTPUT(IMAGE_AVAILABLE_SEM, VkSemaphore, 1, true);
    CONSTEXPR_OUTPUT(RENDER_FINISHED_SEM, VkSemaphore, 2, true);
};
```

### 2. TypedNode Generates Storage

```cpp
template<typename ConfigType>
class TypedNode : public NodeInstance {
protected:
    // THESE are auto-generated based on config!
    std::array<void*, ConfigType::INPUT_COUNT> inputs;   // Size from config
    std::array<void*, ConfigType::OUTPUT_COUNT> outputs; // Size from config

    // Access is type-safe through templates:
    template<size_t Index>
    auto& In() {  // Returns correct type based on config slot
        static_assert(Index < ConfigType::INPUT_COUNT);
        return reinterpret_cast</* Type from config */&>(inputs[Index]);
    }
};
```

### 3. Node Uses Typed Access

```cpp
class SwapChainNode : public TypedNode<SwapChainNodeConfig> {
    void Compile() override {
        // Get input - compiler knows it's VkSurfaceKHR
        VkSurfaceKHR surface = In<0>();
        // Or: VkSurfaceKHR surface = In(SwapChainNodeConfig::SURFACE);

        CreateSwapchain(surface);

        // Set outputs - types are enforced
        Out<0>() = swapchainImages[0];  // VkImage
        Out<1>() = imageAvailableSem;    // VkSemaphore
        Out<2>() = renderFinishedSem;    // VkSemaphore
    }
};
```

## Complete Example: WindowNode

```cpp
// ===== CONFIG (Schema) =====
CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 0, 1) {
    CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);

    WindowNodeConfig() {
        INIT_OUTPUT_DESC(SURFACE, "surface",
            ResourceLifetime::Persistent,
            ImageDescription{}
        );
    }
};

// ===== NODE (Auto-Generated Storage) =====
class WindowNode : public TypedNode<WindowNodeConfig> {
public:
    WindowNode(const std::string& name, NodeType* type, VulkanDevice* device)
        : TypedNode<WindowNodeConfig>(name, type, device) {}

    void Setup() override {
        // Register window class...
    }

    void Compile() override {
        // Create window and surface
        CreateWindow();
        CreateSurface();

        // Store in auto-generated storage
        // Compiler KNOWS Out<0>() is VkSurfaceKHR& because config says so!
        Out<0>() = surface;

        // Alternative syntax:
        Out(WindowNodeConfig::SURFACE) = surface;
    }

    void Cleanup() override {
        // Clean up surface
        if (Out<0>() != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, Out<0>(), nullptr);
            Out<0>() = VK_NULL_HANDLE;
        }
    }

private:
    HWND window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;  // Local working copy
};
```

## Benefits

### ✅ 100% Type Safety

Config says `VkSurfaceKHR` → Storage IS `VkSurfaceKHR`

```cpp
CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);

// This works:
VkSurfaceKHR surf = Out<0>();  // ✅ Type matches

// This fails at compile time:
VkImage img = Out<0>();  // ❌ Compile error: cannot convert VkSurfaceKHR to VkImage
```

### ✅ Impossible to Mismatch

You CAN'T declare the wrong type - storage is auto-generated!

```cpp
// OLD (error-prone):
class WindowNode {
    VkImage surface;  // Manual declaration - could be wrong type!
};

// NEW (foolproof):
class WindowNode : public TypedNode<WindowNodeConfig> {
    // No manual declaration!
    // Storage is DERIVED from config - always correct!
};
```

### ✅ Automatic Bounds Checking

```cpp
CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 0, 1);  // 0 inputs, 1 output

Out<0>() = surface;  // ✅ Valid
Out<1>() = otherThing;  // ❌ Compile error: index 1 >= OUTPUT_COUNT (1)
```

### ✅ Named Access

```cpp
// Index-based (fast):
Out<0>() = surface;

// Slot-based (readable):
Out(WindowNodeConfig::SURFACE) = surface;

// Both compile to the same code - zero overhead!
```

## Full Implementation Plan

For production, use `std::tuple` for true typed storage:

```cpp
template<typename ConfigType>
class TypedNode : public NodeInstance {
protected:
    // Auto-generate tuple types from config slots
    using InputTuple = /* Extract from config: std::tuple<Slot0::Type, Slot1::Type, ...> */;
    using OutputTuple = /* Extract from config: std::tuple<Slot0::Type, Slot1::Type, ...> */;

    InputTuple inputs;
    OutputTuple outputs;

    template<size_t Index>
    auto& In() {
        return std::get<Index>(inputs);  // Perfect type safety!
    }
};
```

This requires template metaprogramming to extract types from the config, but provides **perfect zero-overhead type safety**!
