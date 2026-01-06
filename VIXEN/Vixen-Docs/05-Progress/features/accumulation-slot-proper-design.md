---
title: Proper Accumulation Slot System Design
aliases: [Accumulation Slots, Container Types, Storage Strategy]
tags: [feature, render-graph, type-system, sprint-6.0.2]
status: DESIGN
created: 2026-01-06
---

# Proper Accumulation Slot System Design

**Status:** DESIGN  
**Sprint:** 6.0.2  
**Estimated Effort:** 3 hours

## Problem Statement

Current accumulation slot design has fundamental flaws:

1. **Element type confusion** - Slot declares `bool`, runtime uses `std::vector<bool>`
2. **No storage strategy** - No distinction between copy-by-value vs reference
3. **Missing validations** - Reference storage + Transient source not caught
4. **No copy warnings** - Large value copies unreported

**Current (broken) design:**
```cpp
ACCUMULATION_INPUT_SLOT(INPUTS, bool, 1, ...)  // Element type
const auto& inputs = ctx.In(...);  // Returns std::vector<bool>
```

## Discovery Phase Findings

### Current Architecture Analysis

**Type Flow (Element → Container):**
```
Declaration:  ACCUMULATION_INPUT_SLOT(INPUTS, bool, 1, ...)
                          ↓
Slot Type:    INPUTS_Slot::Type = bool  (element type - LIE)
                          ↓
Runtime:      ctx.In(INPUTS) → std::vector<bool>  (actual type)
                          ↑
              Magic in TypedIOContext::In() using if constexpr
```

**Key Finding:** Type system lie - slot declares `bool` but storage is `std::vector<bool>`. The conversion happens via runtime magic in `TypedNodeInstance.h:188-194`.

### Usage Sites (Only 4 total)

**Production:**
1. `BoolOpNodeConfig.h:61` - `ACCUMULATION_INPUT_SLOT(INPUTS, bool, 1, ...)`

**Tests:**
2. `test_connection_concepts.cpp:53` - `ACCUMULATION_INPUT_SLOT(PASSES, PassThroughStorage, ...)`
3. `test_connection_rule.cpp:62` - `ACCUMULATION_INPUT_SLOT(PASSES, PassThroughStorage, ...)`
4. `test_connection_rule.cpp:1047` - `ACCUMULATION_INPUT_SLOT(INPUTS, bool, ...)`

**Impact:** Only 1 production usage site = ideal time to refactor before adoption grows.

### Industry Comparison

**Unreal RDG:** Explicit container types (`TArrayView<FRDGTextureRef>`)
**Unity SRP:** Explicit list types (`List<RenderRequest>`)
**VIXEN Current:** Element types (5-10 years behind)

### Assessment

**Likely Intentional:** Comment in `BoolOpNodeConfig.h:96` shows conscious trade-off:
```cpp
static_assert(std::is_same_v<INPUTS_Slot::Type, bool>);  // Element type, accumulated into std::vector<bool>
```

**Design rationale:** User ergonomics ("I accumulate bools") over type-system correctness.

**Critical flaw:** Anyone inspecting `SlotType::Type` gets wrong information, breaking generic code.

## Proposed Design

### Design Goals

1. **Type system honesty:** Slot type = actual storage type
2. **Compile-time validation:** Container-element relationship checked at declaration
3. **Storage strategy support:** Value vs Ref with lifetime validation
4. **Runtime warnings:** Large copy detection

### Proposed API

```cpp
// New macro with container type
ACCUMULATION_INPUT_SLOT_V2(
    SlotName,
    std::vector<bool>,           // Container type (explicit)
    bool,                         // Element type (for validation)
    Index,
    Nullability,
    Role,
    StorageStrategy::Value        // NEW: Value vs Ref
)
```

### Storage Strategies

```cpp
enum class SlotStorageStrategy : uint8_t {
    Value,      // Copy elements (warn if sizeof(T) * count > 1KB)
    Reference,  // Store references (require Persistent source)
    Span        // Store std::span (require Persistent source)
};
```

### Compile-Time Validations

```cpp
// 1. Container type must be iterable
static_assert(Iterable<ContainerType>,
    "Accumulation slot requires iterable container");

// 2. Container element matches declared element
static_assert(std::is_same_v<typename ContainerType::value_type, ElementType>,
    "Container must hold ElementType");

// 3. Reference storage requires validation (deferred to connection time)
// Note: Can't check source lifetime at slot declaration
```

### Runtime Validations

```cpp
// In AccumulationConnectionRule::Validate()
if (meta.storageStrategy == StorageStrategy::Reference) {
    if (ctx.sourceSlot.lifetime != ResourceLifetime::Persistent) {
        return Error("Reference storage requires Persistent source");
    }
}

if (meta.storageStrategy == StorageStrategy::Value) {
    size_t totalSize = sizeof(ElementType) * accumulatedCount;
    if (totalSize > 1024) {  // 1KB threshold
        NODE_LOG_WARNING("Accumulation copying " + std::to_string(totalSize) +
                        " bytes. Consider Reference storage for Persistent sources.");
    }
}
```

## Migration Path

### Phase 1: Add New Macro (Non-Breaking)

Add `ACCUMULATION_INPUT_SLOT_V2` to `ResourceConfig.h`. Old macro remains functional.

**Estimated:** 1 hour

### Phase 2: Migrate BoolOpNode

Update `BoolOpNodeConfig.h` to use new macro.

**Estimated:** 30 minutes

### Phase 3: Simplify TypedIOContext::In()

Remove `if constexpr (SlotType::isAccumulation)` branch - type is now explicit.

**Estimated:** 30 minutes

### Phase 4: Add Runtime Validations

Implement storage strategy checks in `AccumulationConnectionRule`.

**Estimated:** 1 hour

**Total Estimated Effort:** 3 hours

## Implementation Plan

1. **Add SlotStorageStrategy enum** to `ResourceConfig.h`
2. **Create ACCUMULATION_INPUT_SLOT_V2 macro** with compile-time validations
3. **Update BoolOpNodeConfig** to use new macro
4. **Simplify TypedNodeInstance::In()** to remove special-case handling
5. **Add runtime validations** for ref/value strategy
6. **Update tests** (3 test files with accumulation slots)
7. **Deprecate old macro** with compiler warning

## HacknPlan References

- Task #337: Refactor Accumulation Slot System
- Design Element #36: [RenderGraph] Accumulation Slot Type System
- Related: Task #334 - Move Accumulation logic to AccumulationConnectionRule

## Related Files

- `libraries/RenderGraph/include/Data/Core/ResourceConfig.h` - ACCUMULATION_INPUT_SLOT macro
- `libraries/RenderGraph/include/Data/Core/ConnectionConcepts.h` - Iterable concept
- `libraries/RenderGraph/include/Core/TypedNodeInstance.h` - ctx.In() implementation
- `libraries/RenderGraph/include/Data/Nodes/BoolOpNodeConfig.h` - Only usage site

## HacknPlan References

[To be filled after task creation]
