---
tags: [feature, render-graph, api-design, ergonomics]
status: PROPOSED
created: 2026-01-06
sprint: Sprint 6.0.2
---

# Variadic Modifier API - Streamlined Connection Syntax

**Status:** PROPOSED
**Created:** 2026-01-06
**Target:** Sprint 6.0.2 or later

---

## Problem Statement

Current modifier API is verbose:

```cpp
// Current: Explicit ConnectionMeta construction
batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
              gatherer, VoxelRayMarch::cameraPos::BINDING,
              ConnectionMeta{}.With(ExtractField(&CameraData::cameraPos, SlotRole::Execute)));

// With multiple modifiers - gets even worse
batch.Connect(nodeA, ConfigA::OUTPUT,
              nodeB, ConfigB::INPUT,
              ConnectionMeta{}
                  .With(ExtractField(&MyStruct::field))
                  .With<SlotRoleModifier>(SlotRole::Execute)
                  .With<DebugTagModifier>("camera-pos"));
```

**Industry comparison:**
- **Unity HDRP**: `builder.Use(texture)`
- **Unreal RDG**: `GraphBuilder.CreateTexture(desc)`
- **VIXEN Current**: `ConnectionMeta{}.With(...).With(...)`

The common case (1-2 modifiers) shouldn't require explicit meta construction.

---

## Proposed Solution

### Option 1: Variadic Template Overload (Recommended)

Add variadic overload that accepts modifiers directly:

```cpp
template<typename SourceSlot, typename TargetSlot, typename... Modifiers>
ConnectionBatch& Connect(
    NodeHandle sourceNode,
    SourceSlot sourceSlot,
    NodeHandle targetNode,
    TargetSlot targetSlot,
    Modifiers&&... modifiers  // Accept modifiers directly
) {
    // Construct ConnectionMeta from modifiers
    ConnectionMeta meta;
    (meta.With(std::forward<Modifiers>(modifiers)), ...);  // Fold expression

    return Connect(sourceNode, sourceSlot, targetNode, targetSlot, std::move(meta));
}
```

**Usage:**
```cpp
// No modifiers - unchanged
batch.Connect(deviceNode, DeviceConfig::DEVICE,
              swapchainNode, SwapChainConfig::DEVICE);

// Single modifier - no ConnectionMeta{}
batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
              gatherer, VoxelRayMarch::cameraPos::BINDING,
              ExtractField(&CameraData::cameraPos, SlotRole::Execute));

// Multiple modifiers - comma-separated
batch.Connect(nodeA, ConfigA::OUTPUT,
              nodeB, ConfigB::INPUT,
              ExtractField(&MyStruct::field),
              SlotRoleModifier(SlotRole::Execute),
              DebugTagModifier("camera-pos"));
```

**Benefits:**
- 80% reduction in boilerplate for common case
- No breaking changes (existing code still works)
- Type-safe (modifiers validated at compile-time)
- Consistent with modern C++ (fold expressions, perfect forwarding)

**Implementation:**
- Add variadic template overload to `ConnectionBatch`
- Use fold expression to populate `ConnectionMeta`
- Delegate to existing `Connect(...)` with meta

---

### Option 2: Modifier Pack Helper (Alternative)

Create helper function to construct ConnectionMeta:

```cpp
template<typename... Modifiers>
ConnectionMeta Mods(Modifiers&&... mods) {
    ConnectionMeta meta;
    (meta.With(std::forward<Modifiers>(mods)), ...);
    return meta;
}

// Usage
batch.Connect(nodeA, ConfigA::OUTPUT,
              nodeB, ConfigB::INPUT,
              Mods(ExtractField(&MyStruct::field),
                   SlotRoleModifier(SlotRole::Execute)));
```

**Benefits:**
- Explicit about modifier construction
- Shorter than `ConnectionMeta{}.With(...).With(...)`

**Drawbacks:**
- Still requires helper invocation (`Mods(...)`)
- Less ergonomic than Option 1

---

### Option 3: Implicit Conversion (Not Recommended)

Make ConnectionModifier implicitly convertible to ConnectionMeta:

```cpp
struct ConnectionMeta {
    // Implicit conversion from single modifier
    ConnectionMeta(std::unique_ptr<ConnectionModifier> mod) {
        modifiers.push_back(std::move(mod));
    }
};

// Usage
batch.Connect(nodeA, ConfigA::OUTPUT,
              nodeB, ConfigB::INPUT,
              ExtractField(&MyStruct::field));  // Implicit conversion
```

**Issues:**
- Only works for single modifier
- Multiple modifiers still need meta construction
- Potential for ambiguous overloads
- **Not Recommended** - limited value

---

## Recommended Implementation: Option 1

### Phase 1: Add Variadic Overload (2-4 hours)

1. **Update TypedConnection.h** - Add variadic Connect overload:

```cpp
/**
 * @brief Streamlined connection with implicit modifier construction
 *
 * Accepts modifiers directly without ConnectionMeta{} boilerplate.
 *
 * Examples:
 * @code
 * // No modifiers
 * batch.Connect(src, SrcConfig::OUT, tgt, TgtConfig::IN);
 *
 * // Single modifier
 * batch.Connect(src, SrcConfig::OUT, tgt, TgtConfig::IN,
 *               ExtractField(&Struct::field));
 *
 * // Multiple modifiers
 * batch.Connect(src, SrcConfig::OUT, tgt, TgtConfig::IN,
 *               ExtractField(&Struct::field),
 *               SlotRoleModifier(SlotRole::Execute));
 * @endcode
 */
template<typename SourceSlot, typename TargetSlot, typename... Modifiers>
    requires (sizeof...(Modifiers) > 0) &&  // At least one modifier
             (std::derived_from<std::remove_cvref_t<Modifiers>, ConnectionModifier> && ...)
ConnectionBatch& Connect(
    NodeHandle sourceNode,
    SourceSlot sourceSlot,
    NodeHandle targetNode,
    TargetSlot targetSlot,
    Modifiers&&... modifiers
) {
    ConnectionMeta meta;

    // Fold expression to add each modifier
    ([&meta, &modifiers] {
        if constexpr (std::is_rvalue_reference_v<decltype(modifiers)>) {
            meta.modifiers.push_back(
                std::make_unique<std::remove_cvref_t<Modifiers>>(
                    std::move(modifiers)));
        } else {
            meta.modifiers.push_back(
                std::make_unique<std::remove_cvref_t<Modifiers>>(modifiers));
        }
    }(), ...);

    return Connect(sourceNode, sourceSlot, targetNode, targetSlot, std::move(meta));
}
```

2. **Test Coverage** - Add tests for variadic API:

```cpp
TEST(VariadicModifierAPI, SingleModifier) {
    ConnectionBatch batch(&graph);
    batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::IN,
                  SlotRoleModifier(SlotRole::Execute));
    // Verify modifier applied
}

TEST(VariadicModifierAPI, MultipleModifiers) {
    ConnectionBatch batch(&graph);
    batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::IN,
                  ExtractField(&Struct::field),
                  SlotRoleModifier(SlotRole::Execute),
                  DebugTagModifier("test"));
    // Verify all modifiers applied in order
}
```

3. **Documentation Update** - Update examples in headers to show streamlined syntax

### Phase 2: Migrate Codebase (4-8 hours)

Gradually migrate call sites to use streamlined syntax:

```cpp
// Before (50+ instances in VulkanGraphApplication.cpp)
batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
              gatherer, VoxelRayMarch::cameraPos::BINDING,
              ConnectionMeta{}.With(ExtractField(&CameraData::cameraPos, SlotRole::Execute)));

// After
batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
              gatherer, VoxelRayMarch::cameraPos::BINDING,
              ExtractField(&CameraData::cameraPos, SlotRole::Execute));
```

**Migration Strategy:**
- Phase 2a: Migrate VulkanGraphApplication.cpp (50+ call sites)
- Phase 2b: Migrate BenchmarkGraphFactory.cpp (200+ call sites)
- Phase 2c: Update examples and documentation

---

## Alternative: Direct Modifier Construction

If variadic template is too complex, provide convenience constructors:

```cpp
// In FieldExtractionModifier.h
inline auto ExtractField(auto memberPtr, SlotRole role = SlotRole::Dependency) {
    return std::make_unique<FieldExtractionModifier>(memberPtr, role);
}

// In SlotRoleModifier.h
inline auto RoleModifier(SlotRole role) {
    return std::make_unique<SlotRoleModifier>(role);
}

// Usage - still requires ConnectionMeta but shorter
batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::IN,
              ConnectionMeta{}.With(ExtractField(&Struct::field))
                              .With(RoleModifier(SlotRole::Execute)));
```

---

## Performance Considerations

**Compile-time:**
- Variadic template instantiations: +0.5-1% compile time
- Worth it for 80% reduction in user code

**Runtime:**
- Zero overhead - same code gen as explicit ConnectionMeta
- Fold expressions inline to identical assembly

---

## Breaking Changes

**None** - Existing code continues to work:

```cpp
// Existing explicit syntax - still valid
batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::IN,
              ConnectionMeta{}.With(ExtractField(&Struct::field)));

// New streamlined syntax - cleaner
batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::IN,
              ExtractField(&Struct::field));
```

Both compile to same code.

---

## Industry Precedent

**C++20 Ranges:**
```cpp
// Old
std::ranges::sort(vec.begin(), vec.end(), comp);

// New
std::ranges::sort(vec, comp);  // Implicit construction
```

**fmt/std::format:**
```cpp
// Old
fmt::format(fmt::arg("name", "value"), "Hello {name}");

// New
fmt::format("Hello {name}", fmt::arg("name", "value"));  // Variadic
```

**VIXEN Proposed:**
```cpp
// Old
batch.Connect(src, slot, tgt, slot, ConnectionMeta{}.With(mod));

// New
batch.Connect(src, slot, tgt, slot, mod);  // Variadic
```

---

## Recommendation

**Implement Option 1: Variadic Template Overload**

**Pros:**
- 80% less boilerplate for common case
- No breaking changes
- Modern C++ style
- Industry precedent

**Estimated Effort:**
- Phase 1 (API + tests): 2-4 hours
- Phase 2 (migration): 4-8 hours
- **Total**: 6-12 hours

**ROI:** High - Significant ergonomics improvement with minimal cost

---

## Next Steps

1. Review this proposal with team
2. Prototype variadic overload
3. Validate compile-time impact
4. Implement Phase 1 (API + tests)
5. Gradual Phase 2 migration

---

## References

- [[Sprint6.0.1-Unified-Connection-System]] - Original modifier design
- [C++20 Fold Expressions](https://en.cppreference.com/w/cpp/language/fold)
- Unity HDRP RenderGraph API
- Unreal RDG API
