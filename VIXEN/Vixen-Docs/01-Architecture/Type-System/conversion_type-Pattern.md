---
tags: [architecture, type-system, render-graph, cpp23]
created: 2025-12-13
related: [[RenderGraph-System]], [[CompileTimeResourceSystem]], [[ShaderCountersBuffer]]
---

# The conversion_type Pattern

## Overview

The `conversion_type` pattern is a C++23 technique that enables **zero-overhead wrapper type identification** within the render graph's compile-time resource system. It allows wrapper classes to declare their underlying type explicitly, enabling automatic descriptor extraction and slot validation without manual registration.

Instead of:
```cpp
ShaderCountersBuffer wrapper;
register(wrapper);  // Manual registration required
```

You can write:
```cpp
ShaderCountersBuffer wrapper;
// Automatically recognized via: using conversion_type = ShaderCountersData;
```

## The Problem

Before this pattern, the render graph had two problematic approaches:

### 1. Explicit Registration (Manual Overhead)
Wrapper types required explicit registration:
```cpp
// Old approach: had to manually call register()
auto wrapper = ShaderCountersBuffer{...};
renderGraph.registerWrapper(wrapper);  // Easy to forget
renderGraph.addSlot(wrapper);          // Now usable
```

**Issues:**
- Boilerplate code duplicated across all wrappers
- Runtime cost of registration
- Easy to forget, leading to cryptic validation errors
- No type safety guarantee

### 2. OutWithInterface Requirement
Without registration, you had to use a marker base class:
```cpp
class ShaderCountersBuffer : public OutWithInterface {
    // Marker class required for type identification
};
```

**Issues:**
- Forced inheritance hierarchy
- Not composable with other interfaces
- Virtual dispatch overhead
- Fragile to refactoring

### 3. No Automatic Descriptor Binding
Wrapper types couldn't automatically find their descriptor:
```cpp
// Had to manually specify descriptor extraction
template<> struct DescriptorExtractor<ShaderCountersBuffer> {
    static auto get(ShaderCountersBuffer& buf) { ... }
};
```

## The Solution

The `conversion_type` pattern uses a **type alias** inside the wrapper class:

```cpp
class ShaderCountersBuffer {
public:
    // This line declares the underlying type!
    using conversion_type = ShaderCountersData;
    
    // Rest of implementation...
    ShaderCountersBuffer(VulkanContext& ctx) : data_(ctx) {}
    
private:
    ShaderCountersData data_;
};
```

This single line of code enables:
- Automatic type identification at compile time
- Zero-overhead validation via concepts
- Descriptor extraction without registration
- Direct slot usage with wrapper types

## Implementation Details

### 1. HasConversionType Concept

Located in: `libraries/RenderGraph/include/CompileTimeResourceSystem.h` (lines ~450-460)

```cpp
template<typename T>
concept HasConversionType = requires {
    typename T::conversion_type;
};
```

This concept checks if type `T` has a nested `conversion_type` typedef.

### 2. ConversionTypeOf_t Trait

Located in: `libraries/RenderGraph/include/CompileTimeResourceSystem.h` (lines ~462-470)

```cpp
template<typename T>
struct ConversionTypeOf {
    using type = T;  // Default: no conversion
};

template<HasConversionType T>
struct ConversionTypeOf<T> {
    using type = typename T::conversion_type;  // Use declared type
};

template<typename T>
using ConversionTypeOf_t = typename ConversionTypeOf<T>::type;
```

**How it works:**
1. If `T` has `conversion_type`, use that
2. Otherwise, `T` is its own underlying type
3. This provides a uniform interface for both wrappers and direct types

### 3. Recursive Validation in IsValidTypeImpl

Located in: `libraries/RenderGraph/include/CompileTimeResourceSystem.h` (lines ~520-550)

```cpp
template<typename SlotType, typename ProvidedType>
struct IsValidTypeImpl {
    // Check if ProvidedType can satisfy SlotType requirements
    static constexpr bool value = 
        std::is_same_v<ConversionTypeOf_t<ProvidedType>, SlotType> ||
        requires(ProvidedType& p) {
            // Additional constraints...
        };
};
```

**Validation flow:**
1. Extract underlying type from ProvidedType using `ConversionTypeOf_t`
2. Compare with expected SlotType
3. If match, validation succeeds
4. If not, compile error with clear message

### 4. Descriptor Extraction via descriptorExtractor_

Located in: `libraries/RenderGraph/include/CompileTimeResourceSystem.h` (lines ~600-620)

```cpp
template<typename T>
auto descriptorExtractor_ = [](T& wrapper) {
    using UnderlyingType = ConversionTypeOf_t<T>;
    
    if constexpr(HasConversionType<T>) {
        // Wrapper with conversion_type
        return wrapper.conversion_type_data_member->getDescriptor();
    } else {
        // Direct type
        return wrapper.getDescriptor();
    }
};
```

**Descriptor binding:**
1. Detect if type has `conversion_type`
2. Route to appropriate descriptor getter
3. Return descriptor for slot binding
4. **All at compile time—no runtime overhead**

## Usage Example

### ShaderCountersBuffer Wrapper

Located in: `libraries/RenderGraph/include/Debug/ShaderCountersBuffer.h` (lines ~40-80)

```cpp
class ShaderCountersBuffer {
public:
    // Declare the underlying type
    using conversion_type = ShaderCountersData;
    
    explicit ShaderCountersBuffer(VulkanContext& context)
        : data_(context) {}
    
    // Methods delegate to underlying type
    void record(VkCommandBuffer cmd, uint32_t counterValue) {
        data_.record(cmd, counterValue);
    }
    
    VkBuffer getBuffer() const {
        return data_.getBuffer();
    }
    
private:
    ShaderCountersData data_;
};
```

### RayTraceBuffer Wrapper

Located in: `libraries/RenderGraph/include/Nodes/VoxelGridNode.h` (lines ~120-150)

```cpp
class RayTraceBuffer {
public:
    using conversion_type = RayTraceData;
    
    explicit RayTraceBuffer(VulkanContext& context)
        : data_(context) {}
    
    void updateTraceParams(const RayTraceParams& params) {
        data_.updateTraceParams(params);
    }
    
private:
    RayTraceData data_;
};
```

### Usage in SlotBindings

```cpp
template<typename Context>
void bindSlots(Context& ctx) {
    // Slot expects ShaderCountersData
    slots_.counterData = ShaderCountersBuffer{ctx};  // ✓ Automatically valid
    
    // Slot expects RayTraceData
    slots_.rayData = RayTraceBuffer{ctx};            // ✓ Automatically valid
    
    // No registration needed!
}
```

The compiler automatically:
1. Extracts `conversion_type` from each wrapper
2. Validates against slot requirements
3. Binds descriptors correctly
4. **Generates zero-overhead code**

## Benefits

### 1. Zero Overhead
- No runtime registration mechanism
- No virtual dispatch
- No type introspection code
- **All validation happens at compile time**

### 2. No Registration Required
- Wrapper types work immediately
- No need to remember to register
- Impossible to create invalid bindings (compile error)

### 3. Type-Safe Wrapper Access
- Slots use wrapper types directly
- Type system prevents misuse
- Refactoring is safe and automatic

### 4. Composable Design
- Wrappers don't inherit from marker classes
- Can use other patterns (RAII, composition, etc.)
- Future extensions (e.g., lifecycle hooks) are possible

### 5. Self-Documenting Code
- `using conversion_type = T;` explicitly declares intent
- Readers know the wrapper's underlying type immediately
- No hidden mappings or registration tables

## Related Files with Line Numbers

### Architecture Headers

| File | Purpose | Key Lines |
|------|---------|-----------|
| `libraries/RenderGraph/include/CompileTimeResourceSystem.h` | Concept definitions | ~450-470 |
| | ConversionTypeOf trait | ~462-470 |
| | IsValidTypeImpl validation | ~520-550 |
| | descriptorExtractor_ implementation | ~600-620 |

### Implementation Files

| File | Purpose | Key Lines |
|------|---------|-----------|
| `libraries/RenderGraph/include/Debug/ShaderCountersBuffer.h` | ShaderCountersBuffer wrapper | ~40-80 |
| | conversion_type declaration | ~45 |
| `libraries/RenderGraph/src/Debug/ShaderCountersBuffer.cpp` | ShaderCountersBuffer impl | ~1-100 |
| `libraries/RenderGraph/include/Nodes/VoxelGridNode.h` | RayTraceBuffer wrapper | ~120-150 |
| | conversion_type declaration | ~125 |
| `libraries/RenderGraph/src/Nodes/VoxelGridNode.cpp` | RayTraceBuffer impl | ~1-150 |

### Descriptor Implementations

| File | Purpose | Key Lines |
|------|---------|-----------|
| `libraries/RenderGraph/include/Data/ShaderCountersData.h` | Underlying descriptor type | ~1-60 |
| `libraries/RenderGraph/include/Data/RayTraceData.h` | Underlying descriptor type | ~1-80 |

## Design Rationale

### Why a Type Alias?
- **Minimal syntax:** Single line of declaration
- **Explicit intent:** Reader knows exactly what the wrapper represents
- **Zero cost:** Completely eliminated at compile time
- **Standard C++:** No custom macros or magic

### Why Concepts?
- **Compile-time checking:** Impossible to pass wrong types
- **Clear error messages:** Compiler explains validation failures
- **No runtime cost:** All checks eliminated in release builds
- **Type safety:** Works with SFINAE and overload resolution

### Why Not Templates?

A template-based approach would require:
```cpp
template<typename T>
class Wrapper : public EnableWrapper<T> { ... };
```

This is worse because:
- More boilerplate per wrapper
- Harder to read (readers must find the template)
- Inheritance-based (not composable)
- More runtime overhead

## Future Extensions

The pattern supports several natural extensions:

### Lifecycle Hooks
```cpp
template<typename T>
concept HasLifecycleHooks = requires(T& t) {
    { t.onBind() } -> std::same_as<void>;
    { t.onUnbind() } -> std::same_as<void>;
};
```

### Custom Validation
```cpp
template<typename T>
concept CustomValidation = requires(T& t) {
    { T::validateDescriptor() } -> std::same_as<bool>;
};
```

### Descriptor Caching
Extend `descriptorExtractor_` to cache descriptors for hot paths.

## See Also

- [[RenderGraph-System]] - Overall render graph architecture
- [[CompileTimeResourceSystem]] - Full compile-time system documentation
- [[ShaderCountersBuffer]] - Complete implementation walkthrough
- [[Concepts-and-Constraints]] - C++20/23 concepts guide
