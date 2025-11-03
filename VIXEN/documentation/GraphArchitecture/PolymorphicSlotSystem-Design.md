# Polymorphic Slot System Design

**Status**: Draft Design for Phase F.3
**Created**: November 3, 2025
**Blocks**: Phase G (Compute Pipeline) - Generic shader support

---

## Problem Statement

### Issue 1: Scalar/Array Union Types

**Need**: Slot accepts ANY type from a set, in scalar OR array form.

**Example**:
```cpp
// Slot accepts:
// - VkImage (single)
// - VkBuffer (single)
// - vector<VkImage> (array)
// - vector<VkBuffer> (array)

VARIANT_INPUT(RESOURCE, VulkanHandleSet, 0, false);
```

**Use Case**: Generic resource loader node outputs different resource types based on file format (texture → VkImage, model → VkBuffer).

### Issue 2: Struct Field Access Without Slot Explosion

**Need**: Connect to specific struct fields without exposing every member as separate slot.

**Example**:
```cpp
// Node A outputs:
struct SwapChainPublicVariables {
    VkFormat format;
    VkExtent2D extent;
    uint32_t imageCount;
    // ... 20 more fields
};

// Node B needs ONLY format:
graph.Connect(nodeA, "SWAPCHAIN_PUBLIC", nodeB, "FORMAT",
              FieldPath{"format"}); // Extract .format
```

**Critical**: With SPIR-V reflection, struct layout unknown until graph compile time.

**Blocker**: Phase G requires generic `ComputePipelineNode` that works with arbitrary shaders without creating specialized node classes per shader.

---

## Design Goals

1. **Early Validation**: Type errors caught at graph compile time, NOT runtime
2. **Compile-Time Safety**: Preserve strong typing where possible
3. **Zero Overhead**: No virtual dispatch for known types
4. **Reflection-Friendly**: Support dynamic types from SPIR-V
5. **Clean API**: Minimal syntactic overhead

---

## Solution Architecture

### Part 1: Type Sets & Variant Slots

#### Type Set Definition

```cpp
// Define a named type set
TYPE_SET(VulkanHandleSet,
    VkImage,
    VkBuffer,
    VkSampler,
    VkImageView
);

TYPE_SET(NumericSet,
    uint32_t,
    float,
    double
);
```

#### Variant Slot Definition

```cpp
// Slot accepts any type from set (scalar or array)
VARIANT_INPUT(RESOURCE, VulkanHandleSet, 0, Optional);

// Expands to:
using RESOURCE_Slot = VariantResourceSlot<
    VulkanHandleSet,        // Type set
    0,                      // Index
    SlotNullability::Optional
>;

// Storage type:
std::variant<
    VkImage, vector<VkImage>,
    VkBuffer, vector<VkBuffer>,
    VkSampler, vector<VkSampler>,
    VkImageView, vector<VkImageView>
>
```

#### Connection Validation

```cpp
// Graph compile checks compatibility:
bool CanConnect(OutputSlot out, InputSlot in) {
    // Extract base types (strip array wrapper)
    Type outBaseType = StripArray(out.GetType());
    Type inBaseType = StripArray(in.GetType());

    // Check if output type is in input's accepted set
    if (in.IsVariantSlot()) {
        return in.GetTypeSet().Contains(outBaseType);
    }

    // Standard type match
    return outBaseType == inBaseType;
}
```

---

### Part 2: Field Path Connections

#### Field Path Syntax

```cpp
struct FieldPath {
    vector<string> path;  // e.g., {"params", "lighting", "intensity"}

    static FieldPath Parse(string_view str) {
        // "params.lighting.intensity" → {"params", "lighting", "intensity"}
    }
};
```

#### Connection with Field Extraction

```cpp
// Connect with field path
graph.Connect(
    nodeA, "SWAPCHAIN_PUBLIC",  // Outputs: SwapChainPublicVariables*
    nodeB, "FORMAT",             // Expects: VkFormat
    FieldPath{"format"}          // Extract .format field
);

// Graph compiler:
// 1. Reflects SwapChainPublicVariables struct
// 2. Validates "format" field exists
// 3. Validates VkFormat matches expected type
// 4. Inserts implicit field accessor
```

#### Reflection-Driven Field Access

```cpp
// For SPIR-V-reflected structs:
class StructuredResource : public Resource {
public:
    // Reflection metadata
    struct FieldInfo {
        string name;
        ResourceType type;
        size_t offset;
        size_t size;
    };

    vector<FieldInfo> GetFields() const;

    // Runtime field access (validated at graph compile)
    template<typename T>
    T* GetField(string_view fieldName);
};
```

---

### Part 3: Dynamic Slots (Reflection-Driven)

For compute shaders where descriptor layout is unknown at C++ compile time:

```cpp
class DynamicSlotConfig {
public:
    // Slots determined by reflection at graph compile
    void ConfigureFromReflection(const ShaderDataBundle& bundle) {
        // Extract UBO fields from SPIR-V
        for (auto& binding : bundle.GetDescriptorBindings()) {
            if (binding.type == UNIFORM_BUFFER) {
                // Create input slot for each UBO field
                AddDynamicInput(binding.name, binding.type);
            }
        }
    }

    // Graph validates connections against dynamic schema
    vector<DynamicSlotDescriptor> inputs;
    vector<DynamicSlotDescriptor> outputs;
};
```

#### Generic Compute Node

```cpp
class ComputePipelineNode : public TypedNode<ComputePipelineNodeConfig> {
    void LoadShader(const char* path) {
        // Load and reflect shader
        bundle = ShaderBundleBuilder::FromFile(path);

        // Configure slots dynamically
        dynamicConfig.ConfigureFromReflection(*bundle);
    }

    void CompileImpl() override {
        // Graph has already validated all connections
        // against dynamicConfig schema

        // Read inputs using dynamic accessors
        auto uboData = In(dynamicConfig.GetInput("params"));
    }

private:
    DynamicSlotConfig dynamicConfig;
    ShaderDataBundlePtr bundle;
};
```

---

## Implementation Phases

### Phase F.3.1: Variant Slots (1-2 days)
- [ ] `TYPE_SET` macro and registry
- [ ] `VariantResourceSlot` template
- [ ] `VARIANT_INPUT/OUTPUT` macros
- [ ] Connection validation with type set checks
- [ ] Test with generic resource loader

### Phase F.3.2: Field Path Connections (2-3 days)
- [ ] `FieldPath` parser and validator
- [ ] `StructuredResource` class for reflected types
- [ ] Connection metadata with field paths
- [ ] Graph compiler field extraction
- [ ] Test with SwapChainPublicVariables → format extraction

### Phase F.3.3: Dynamic Slots (3-4 days)
- [ ] `DynamicSlotConfig` class
- [ ] Reflection-driven slot generation
- [ ] Graph validation against dynamic schema
- [ ] Generic `ComputePipelineNode` implementation
- [ ] Test with arbitrary compute shader

### Phase F.3.4: Integration (1-2 days)
- [ ] Update all existing nodes for compatibility
- [ ] Documentation and examples
- [ ] Performance validation (ensure zero overhead)

**Total Estimate**: 7-11 days

---

## Alternative Approaches Considered

### Option A: Explicit Converter Nodes
**Idea**: Insert `StructUnpackNode` between producer/consumer.

**Pros**:
- Explicit, easy to understand
- Works with current system

**Cons**:
- Graph explosion (one unpacker per struct field)
- Tedious for reflection-driven data
- **Doesn't solve dynamic type problem**

**Verdict**: Rejected—doesn't solve Phase G blocker.

### Option B: Fully Dynamic Types (like Python)
**Idea**: All slots are `variant<...>` with runtime type checks.

**Pros**:
- Maximum flexibility

**Cons**:
- Loses compile-time safety
- Virtual dispatch overhead
- Type errors caught at runtime (violates goal #1)

**Verdict**: Rejected—sacrifices too much type safety.

### Option C: Reflection-Only Approach
**Idea**: All struct access via string-based field names.

**Pros**:
- Unified API for static and dynamic types

**Cons**:
- Loses compile-time checking for known types
- String overhead for every access

**Verdict**: Hybrid approach better—use static types where possible, dynamic where necessary.

---

## Open Questions

1. **Type Set Membership**: Should type sets be closed (defined once) or open (extensible)?
   - **Proposal**: Closed sets for safety, but allow custom sets per node.

2. **Field Path Performance**: String-based field access at runtime?
   - **Proposal**: Validate and resolve paths at graph compile, store offsets.

3. **Nested Structs**: Support `params.lighting.ambient.color`?
   - **Proposal**: Yes, using recursive reflection.

4. **Array-of-Structs**: Connect to field across all elements?
   - **Proposal**: Defer to Phase F.3.5 (advanced indexing).

---

## Success Criteria

✅ **Goal 1**: Generic `ComputePipelineNode` works with arbitrary shaders
✅ **Goal 2**: Type errors caught at graph compile, not execution
✅ **Goal 3**: No virtual dispatch for statically-typed slots
✅ **Goal 4**: Struct field access without slot explosion
✅ **Goal 5**: Clean, ergonomic API

---

## Example: End-to-End Compute Shader

```cpp
// ========================================
// SHADER: VoxelRayMarch.comp
// ========================================
layout(binding = 0) uniform Params {
    mat4 viewMatrix;
    vec3 cameraOrigin;
    float maxDistance;
} params;

layout(binding = 1, rgba8) uniform image2D outputImage;

// ========================================
// C++ GRAPH CONSTRUCTION
// ========================================

// Generic compute node - no specialized class needed!
auto compute = graph.AddNode<ComputePipelineNode>("voxel_raymarch");
compute->LoadShader("Shaders/VoxelRayMarch.comp");
// ↑ Reflection extracts: params struct, outputImage binding

// Connect camera data (structured output)
auto camera = graph.AddNode<CameraNode>("camera");
graph.Connect(camera, "TRANSFORM", compute, "params.viewMatrix",
              FieldPath{"viewMatrix"});
graph.Connect(camera, "POSITION", compute, "params.cameraOrigin",
              FieldPath{"position"});

// Connect swapchain image (variant slot - accepts any image type)
auto swapchain = graph.AddNode<SwapChainNode>("swapchain");
graph.Connect(swapchain, "IMAGE", compute, "outputImage");
// ↑ Accepts VkImage | VkImageView | VkFramebuffer

// Compile graph
graph.Compile();
// ↑ Validates:
// - "viewMatrix" field exists in camera.TRANSFORM output
// - Type mat4 matches SPIR-V uniform
// - outputImage compatible with swapchain.IMAGE

// Execute
graph.Execute();
```

---

## Next Steps

1. **Approve design direction**
2. **Prototype `TYPE_SET` and `VARIANT_INPUT` macros**
3. **Test with simple example (resource loader)**
4. **Proceed with phased implementation**

---

**Decision Required**: Should we proceed with this design, or explore alternatives?
