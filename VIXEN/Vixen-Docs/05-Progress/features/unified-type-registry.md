---
tags: [feature, planned, architecture]
created: 2025-12-09
status: planned
priority: low
---

# Feature: Unified Parameter/Resource Type System

## Overview

**Objective:** Merge the node parameter system with the compile-time resource type system to eliminate duplicate type registries.

**Status:** Planned for post-benchmark phase

---

## Current State

### Two Separate Type Systems

| System | Registry | Location | Purpose |
|--------|----------|----------|---------|
| **Parameters** | `PARAMETER_TYPES` macro | `ParameterDataTypes.h` | Node configuration (fov, near/far, etc.) |
| **Resources** | `RESOURCE_TYPE_REGISTRY` macro | `ResourceVariant.h` | Slot data types (VkImage, buffers, etc.) |

### Parameter Types (ParameterDataTypes.h:91-107)

```cpp
#define PARAMETER_TYPES \
    PARAM_TYPE(Int32, int32_t) \
    PARAM_TYPE(UInt32, uint32_t) \
    PARAM_TYPE(Float, float) \
    PARAM_TYPE(Double, double) \
    PARAM_TYPE(Bool, bool) \
    PARAM_TYPE(String, std::string) \
    PARAM_TYPE(Vec2, glm::vec2) \
    PARAM_TYPE(Vec3, glm::vec3) \
    PARAM_TYPE(Vec4, glm::vec4) \
    PARAM_TYPE(Mat4, glm::mat4) \
    PARAM_TYPE(DepthFormat, DepthFormat) \
    PARAM_TYPE(AttachmentLoadOp, AttachmentLoadOp) \
    PARAM_TYPE(AttachmentStoreOp, AttachmentStoreOp) \
    PARAM_TYPE(ImageLayout, ImageLayout) \
    PARAM_TYPE(DebugExportFormat, DebugExportFormat) \
    PARAM_TYPE_LAST(DescriptorLayoutSpecPtr, DescriptorLayoutSpecPtr)
```

### Resource Type Traits (ResourceTypeTraits.h)

- `ResourceTypeTraitsImpl<T>` - Check if type is registered
- Automatic array/container support (vector<T>, array<T,N>)
- Pointer normalization (const T* → T*)

---

## Proposed Design

### Single Unified Registry

```cpp
// Unified type registry supporting both parameters and resources
#define UNIFIED_TYPE_REGISTRY \
    /* Primitives (parameters) */ \
    TYPE_ENTRY(int32_t, PrimitiveDescriptor, TypeCategory::Primitive) \
    TYPE_ENTRY(uint32_t, PrimitiveDescriptor, TypeCategory::Primitive) \
    TYPE_ENTRY(float, PrimitiveDescriptor, TypeCategory::Primitive) \
    TYPE_ENTRY(bool, PrimitiveDescriptor, TypeCategory::Primitive) \
    TYPE_ENTRY(std::string, StringDescriptor, TypeCategory::Primitive) \
    /* Math types (parameters + resources) */ \
    TYPE_ENTRY(glm::vec2, VectorDescriptor, TypeCategory::Math) \
    TYPE_ENTRY(glm::vec3, VectorDescriptor, TypeCategory::Math) \
    TYPE_ENTRY(glm::vec4, VectorDescriptor, TypeCategory::Math) \
    TYPE_ENTRY(glm::mat4, MatrixDescriptor, TypeCategory::Math) \
    /* Vulkan handles (resources only) */ \
    TYPE_ENTRY(VkImage, ImageDescriptor, TypeCategory::VulkanHandle) \
    TYPE_ENTRY(VkBuffer, BufferDescriptor, TypeCategory::VulkanHandle) \
    /* ... etc */
```

### Unified Traits

```cpp
template<typename T>
struct UnifiedTypeTraits {
    static constexpr bool isValid = /* check registry */;
    static constexpr bool isParameterType = /* primitives, math, enums */;
    static constexpr bool isResourceType = /* handles, structs */;
    static constexpr TypeCategory category = /* from registry */;
};
```

### Unified Variant

```cpp
// Single variant for both parameters and resources
using UnifiedTypeValue = std::variant<
    /* Generated from UNIFIED_TYPE_REGISTRY */
>;
```

---

## Benefits

1. **Single Source of Truth**: One macro defines all valid types
2. **Automatic Compatibility**: Any type valid in resources is valid in parameters
3. **Container Support**: `vector<T>`, `array<T,N>` work for parameters too
4. **Type Safety**: Compile-time validation for both systems
5. **Reduced Maintenance**: One registry to update when adding types

---

## Implementation Steps

1. **Create UnifiedTypeRegistry.h**
   - Combine both registries into single macro
   - Add TypeCategory enum (Primitive, Math, VulkanHandle, Struct)

2. **Update ResourceTypeTraits.h**
   - Point to unified registry
   - Add `isParameterType` trait

3. **Update NodeParameterManager.h**
   - Use unified variant instead of ParamTypeValue
   - Add traits-based type validation

4. **Migrate NodeConfig files**
   - Update parameter definitions to use unified system
   - No changes needed for slots (already use traits)

5. **Deprecate ParameterDataTypes.h**
   - Remove PARAMETER_TYPES macro
   - Keep enum definitions (DepthFormat, etc.)

---

## Risks & Considerations

- **Variant Size**: Combined variant will be larger (more alternatives)
- **Compile Time**: Single large variant may increase compile times
- **Migration**: Existing code using ParamTypeValue needs updates

---

## Related Files

| File | Role |
|------|------|
| `libraries/RenderGraph/include/Data/ParameterDataTypes.h` | Current parameter types |
| `libraries/RenderGraph/include/Data/Core/ResourceTypeTraits.h` | Current resource traits |
| `libraries/RenderGraph/include/Data/Core/ResourceVariant.h` | Resource type registry |
| `libraries/RenderGraph/include/Data/NodeParameterManager.h` | Parameter storage |
| `libraries/RenderGraph/include/Core/NodeInstance.h` | Uses both systems |

---

## Development Notes

### 2025-12-09 - Feature Proposed

- Identified opportunity to unify two type systems
- Added to deferred tasks in activeContext.md
- Created this planning document
