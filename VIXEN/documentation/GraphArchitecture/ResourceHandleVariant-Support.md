# ResourceHandleVariant Support in Type System

**Status**: Implemented
**Created**: November 4, 2025
**Related**: ArrayTypeValidation-Implementation.md

---

## Feature Overview

The `ResourceHandleVariant` type (auto-generated from `RESOURCE_TYPE_REGISTRY`) is now a **first-class slot type** with full array support:

```cpp
// ALL these are now valid slot types: ✅
ResourceSlot<ResourceHandleVariant, 0>              // Accepts ANY registered type
ResourceSlot<vector<ResourceHandleVariant>, 0>      // Accepts arrays of ANY type
ResourceSlot<array<ResourceHandleVariant, 5>, 0>    // Accepts fixed-size arrays
```

---

## What is ResourceHandleVariant?

**Auto-generated type** from the `RESOURCE_TYPE_REGISTRY` macro:

```cpp
// RESOURCE_TYPE_REGISTRY expands to:
using ResourceHandleVariant = std::variant<
    std::monostate,
    VkImage,
    VkBuffer,
    VkSampler,
    // ... all 40+ registered types
>;
```

**Purpose**: Runtime polymorphic storage for any registered resource type.

---

## Key Features

### 1. Universal Slot Type

Slots typed as `ResourceHandleVariant` accept **ANY** registered type:

```cpp
CONSTEXPR_NODE_CONFIG(GenericLoaderConfig, 1, 1, SlotArrayMode::Single) {
    CONSTEXPR_INPUT(PATH, std::string, 0, Required);

    // Accepts: VkImage | VkBuffer | VkSampler | ... (any registered type)
    CONSTEXPR_OUTPUT(RESOURCE, ResourceHandleVariant, 0, Required);
};
```

**Connection flexibility**:
```cpp
// ALL these connections are valid:
imageLoader.Connect("OUTPUT", genericNode, "RESOURCE");    // VkImage → Variant ✅
bufferLoader.Connect("OUTPUT", genericNode, "RESOURCE");   // VkBuffer → Variant ✅
samplerNode.Connect("OUTPUT", genericNode, "RESOURCE");    // VkSampler → Variant ✅
```

### 2. Array Support

Vector and array forms automatically valid:

```cpp
CONSTEXPR_NODE_CONFIG(BatchLoaderConfig, 1, 2, SlotArrayMode::Single) {
    CONSTEXPR_INPUT(PATHS, std::vector<std::string>, 0, Required);

    // Accepts arrays of ANY registered type
    CONSTEXPR_OUTPUT(RESOURCES, std::vector<ResourceHandleVariant>, 0, Required);

    // Fixed-size array also works
    CONSTEXPR_OUTPUT(SAMPLERS, std::array<ResourceHandleVariant, 4>, 1, Required);
};
```

**Connection flexibility**:
```cpp
// ALL these connections are valid:
multiImageLoader.Connect("IMAGES", batchNode, "RESOURCES");   // vector<VkImage> → vector<Variant> ✅
multiBufferLoader.Connect("BUFFERS", batchNode, "RESOURCES"); // vector<VkBuffer> → vector<Variant> ✅
```

### 3. Type Detection

Three helper traits for variant detection:

```cpp
// Check if T is ResourceHandleVariant
ResourceTypeTraits<ResourceHandleVariant>::isVariantType → true

// Check if T is container of ResourceHandleVariant
ResourceTypeTraits<vector<ResourceHandleVariant>>::isVariantContainer → true

// Check if T is ANY form of ResourceHandleVariant
ResourceTypeTraits<vector<ResourceHandleVariant>>::isAnyVariant → true
```

---

## Implementation Details

### 1. Helper Templates

**IsResourceHandleVariant**: Detect the variant type
```cpp
template<typename T>
struct IsResourceHandleVariant : std::false_type {};

template<>
struct IsResourceHandleVariant<ResourceHandleVariant> : std::true_type {};
```

**IsResourceHandleVariantContainer**: Detect containers of variant
```cpp
template<typename T>
inline constexpr bool IsResourceHandleVariantContainer_v =
    StripContainer<T>::isContainer &&
    IsResourceHandleVariant_v<typename StripContainer<T>::Type>;
```

**IsAnyResourceHandleVariant**: Detect any form (scalar or container)
```cpp
template<typename T>
inline constexpr bool IsAnyResourceHandleVariant_v =
    IsResourceHandleVariant_v<T> || IsResourceHandleVariantContainer_v<T>;
```

### 2. Explicit Trait Specialization

Register ResourceHandleVariant as valid type:

```cpp
template<>
struct ResourceTypeTraitsImpl<ResourceHandleVariant> {
    using DescriptorT = HandleDescriptor;
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isValid = true;
    static constexpr bool isVariantType = true;
};
```

### 3. Enhanced ResourceTypeTraits

Integrated variant checking into validation logic:

```cpp
template<typename T>
struct ResourceTypeTraits {
    static constexpr bool isValid =
        ResourceTypeTraitsImpl<T>::isValid ||           // Direct registration
        (StripContainer<T>::isContainer &&
         ResourceTypeTraitsImpl<BaseType>::isValid) ||  // Container of registered
        IsResourceHandleVariant_v<T> ||                 // Variant itself ✅
        IsResourceHandleVariantContainer_v<T>;          // Container of variant ✅

    // Variant detection flags
    static constexpr bool isVariantType = IsResourceHandleVariant_v<T>;
    static constexpr bool isVariantContainer = IsResourceHandleVariantContainer_v<T>;
    static constexpr bool isAnyVariant = IsAnyResourceHandleVariant_v<T>;

    // ... rest of traits
};
```

---

## Use Cases

### Use Case 1: Generic Resource Loader

**Problem**: Want ONE loader node that handles any resource type (texture, buffer, mesh, etc.)

**Solution**:
```cpp
class GenericResourceLoaderNode : public TypedNode<GenericLoaderConfig> {
    void CompileImpl() override {
        auto path = In(PATH);

        if (IsTextureFile(path)) {
            VkImage texture = LoadTexture(path);
            Out(RESOURCE, texture);  // VkImage → Variant ✅
        } else if (IsMeshFile(path)) {
            VkBuffer buffer = LoadMesh(path);
            Out(RESOURCE, buffer);   // VkBuffer → Variant ✅
        }
        // ResourceHandleVariant slot accepts both!
    }
};

CONSTEXPR_NODE_CONFIG(GenericLoaderConfig, 1, 1, SlotArrayMode::Single) {
    CONSTEXPR_INPUT(PATH, std::string, 0, Required);
    CONSTEXPR_OUTPUT(RESOURCE, ResourceHandleVariant, 0, Required);
};
```

### Use Case 2: Batch Resource Processing

**Problem**: Process arrays of heterogeneous resources

**Solution**:
```cpp
class BatchProcessorNode : public TypedNode<BatchProcessorConfig> {
    void CompileImpl() override {
        // Input: vector of ANY resource types
        auto resources = In(RESOURCES);

        // Process each variant
        for (auto& resource : resources) {
            std::visit([](auto&& handle) {
                // Type-safe visitor pattern
                ProcessResource(handle);
            }, resource);
        }
    }
};

CONSTEXPR_NODE_CONFIG(BatchProcessorConfig, 1, 0, SlotArrayMode::Single) {
    CONSTEXPR_INPUT(RESOURCES, std::vector<ResourceHandleVariant>, 0, Required);
};
```

### Use Case 3: Dynamic Pipeline (SPIR-V Reflection)

**Problem**: Compute shader descriptors unknown at compile time

**Solution**:
```cpp
class ComputePipelineNode : public TypedNode<ComputePipelineConfig> {
    void LoadShader(const char* path) {
        bundle = ShaderBundleBuilder::FromFile(path);

        // Dynamically configure slots based on reflection
        for (auto& binding : bundle->GetDescriptorBindings()) {
            // Create variant slots - accept ANY type!
            dynamicConfig.AddInput(binding.name,
                                   typeof(vector<ResourceHandleVariant>));
        }
    }
};

CONSTEXPR_NODE_CONFIG(ComputePipelineConfig, 0, 1, SlotArrayMode::Single) {
    // Dynamic slots added at runtime based on shader reflection
    CONSTEXPR_OUTPUT(DISPATCH, VkPipeline, 0, Required);
};
```

---

## Validation

### Compile-Time Tests

**Variant type validation**:
```cpp
static_assert(ResourceTypeTraits<ResourceHandleVariant>::isValid);
static_assert(ResourceTypeTraits<ResourceHandleVariant>::isVariantType);
```

**Container validation**:
```cpp
static_assert(ResourceTypeTraits<vector<ResourceHandleVariant>>::isValid);
static_assert(ResourceTypeTraits<vector<ResourceHandleVariant>>::isVariantContainer);
static_assert(ResourceTypeTraits<array<ResourceHandleVariant, 5>>::isValid);
```

**Slot validation**:
```cpp
CONSTEXPR_NODE_CONFIG(TestConfig, 2, 2, SlotArrayMode::Single) {
    CONSTEXPR_INPUT(VARIANT, ResourceHandleVariant, 0, Required);
    CONSTEXPR_INPUT(VARIANTS, vector<ResourceHandleVariant>, 1, Required);
    CONSTEXPR_OUTPUT(OUT_VARIANT, ResourceHandleVariant, 0, Required);
    CONSTEXPR_OUTPUT(OUT_VARIANTS, vector<ResourceHandleVariant>, 1, Required);
};

// All slots compile successfully ✅
```

### Runtime Validation

See `test_array_type_validation.cpp` for comprehensive runtime tests.

---

## Performance

**Compile-time overhead**: Minimal
- 3 additional trait templates
- Specialization for ResourceHandleVariant
- All resolved at compile time

**Runtime overhead**: Zero
- No additional virtual dispatch
- Same std::variant performance as before
- Type checking via `static_assert` eliminated at runtime

**Binary size impact**: Negligible
- ~3 additional trait instantiations
- No new runtime code

---

## Integration with Existing System

### ✅ Works With

- All existing registered types
- Array/vector types (from ArrayTypeValidation)
- Slot validation system
- Connection validation
- Bundle/task system

### 🔄 Future Enhancements

1. **Type-safe variant visitor helpers**:
   ```cpp
   template<typename Visitor>
   void VisitResource(ResourceHandleVariant& variant, Visitor&& visitor);
   ```

2. **Automatic conversion at connection time**:
   ```cpp
   // Source: VkImage
   // Target: ResourceHandleVariant
   // Graph compiler inserts automatic wrapping
   ```

3. **Constrained variant types** (subset of registered types):
   ```cpp
   using TextureVariant = std::variant<VkImage, VkImageView, VkSampler>;
   // Accept only texture-related types
   ```

---

## Summary

### Key Benefits

✅ **Universal slot type** - Accepts any registered resource
✅ **Full array support** - Works with vector/array containers
✅ **Type-safe detection** - Compile-time trait queries
✅ **Zero runtime cost** - All validation at compile time
✅ **Clean integration** - Works with existing systems

### Use When

- Need runtime polymorphism over resource types
- Processing heterogeneous resource arrays
- Dynamic slot configuration (SPIR-V reflection)
- Generic resource handling nodes

### Avoid When

- Type known at compile time (use specific type for better safety)
- Performance-critical paths (prefer concrete types)
- Need type-specific behavior (use visitor pattern or if constexpr)

---

## Next Steps

This completes **Phase F.3.1** (Array-aware type validation).

**Remaining for full polymorphic slot system**:
1. ✅ Scalar/array flexibility (DONE)
2. ✅ ResourceHandleVariant support (DONE)
3. 🔄 Union type slots (explicit type sets)
4. 🔄 Field path connections (struct member access)
5. 🔄 Dynamic slots (SPIR-V reflection)

Continue to **Phase F.3.2**: Union Type Slots and Field Paths.
