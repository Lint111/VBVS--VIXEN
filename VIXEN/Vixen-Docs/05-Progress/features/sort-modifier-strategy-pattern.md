---
tags: [design, future-work, modifiers, architecture]
status: planned
priority: medium
created: 2026-01-06
sprint: TBD
---

# Sort Modifier Strategy Pattern

**Status:** Planned (Future Work)
**Current Issue:** `AccumulationSortConfig` only sets sort key (int32_t), no strategy
**Proposed Solution:** Strategy-based `AccumulationSortModifier` with multiple sort behaviors

---

## Problem Statement

Current `AccumulationSortConfig` implementation is incomplete:

```cpp
// Current: Only priority number, no sort direction
AccumulationSortConfig(5)  // What does this mean?
// - Should 5 come before 10? (ascending)
// - Should 5 come after 10? (descending)
// - Custom comparison logic?
```

**Architectural Issue:**
- Adding new sort types requires new modifier classes
- Can't compose different sort strategies
- Unrealistic to stack multiple sort modifiers
- No way to express custom sorting logic

**File:** [AccumulationSortConfig.h:38-62](../../libraries/RenderGraph/include/Connection/Modifiers/AccumulationSortConfig.h#L38-L62)

---

## Proposed Design

### Strategy-Based Modifier

Single modifier class with pluggable strategies:

```cpp
enum class SortOrder {
    Ascending,   // 0 < 5 < 10
    Descending,  // 10 > 5 > 0
    Custom       // User-provided comparator
};

class AccumulationSortModifier : public ConnectionModifier {
public:
    // Simple key + order
    AccumulationSortModifier(int32_t sortKey, SortOrder order = SortOrder::Ascending)
        : sortKey_(sortKey), order_(order) {}

    // Custom comparator (for complex sorting)
    template<typename ElementType, typename Comparator>
    explicit AccumulationSortModifier(Comparator&& comp)
        : order_(SortOrder::Custom)
        , customComparator_(std::forward<Comparator>(comp)) {}

    ConnectionResult PreResolve(ConnectionContext& ctx) override {
        ctx.metadata["sortKey"] = sortKey_;
        ctx.metadata["sortOrder"] = order_;
        if (order_ == SortOrder::Custom) {
            ctx.metadata["customComparator"] = customComparator_;
        }
        return ConnectionResult::Success();
    }

    [[nodiscard]] uint32_t Priority() const override { return 40; }
    [[nodiscard]] std::string_view Name() const override { return "AccumulationSortModifier"; }

private:
    int32_t sortKey_ = 0;
    SortOrder order_ = SortOrder::Ascending;
    std::function<bool(const void*, const void*)> customComparator_;
};
```

### Helper Functions

Clean API matching existing patterns (`GroupKey()`, `ExtractField()`):

```cpp
// Simple ascending/descending
inline auto SortAscending(int32_t key) {
    return std::make_unique<AccumulationSortModifier>(key, SortOrder::Ascending);
}

inline auto SortDescending(int32_t key) {
    return std::make_unique<AccumulationSortModifier>(key, SortOrder::Descending);
}

// Custom comparator
template<typename Func>
inline auto SortBy(Func&& comparator) {
    return std::make_unique<AccumulationSortModifier>(std::forward<Func>(comparator));
}
```

---

## Usage Examples

### Simple Priority Sort

```cpp
// Ascending: Lower priority first
batch.Connect(taskGenerator, TaskGenConfig::OUTPUT,
              scheduler, SchedulerConfig::TASKS,
              SortAscending(5));

// Descending: Higher priority first
batch.Connect(taskGenerator, TaskGenConfig::OUTPUT,
              scheduler, SchedulerConfig::TASKS,
              SortDescending(10));
```

### Custom Sort Logic

```cpp
// Sort by timestamp
batch.Connect(passGenerator, PassGenConfig::DISPATCH_PASS,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              SortBy([](const DispatchPass& a, const DispatchPass& b) {
                  return a.timestamp < b.timestamp;
              }));

// Multi-criteria sort: priority then timestamp
batch.Connect(passGenerator, PassGenConfig::DISPATCH_PASS,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              SortBy([](const DispatchPass& a, const DispatchPass& b) {
                  if (a.priority != b.priority)
                      return a.priority > b.priority;  // Higher priority first
                  return a.timestamp < b.timestamp;    // Earlier timestamp first
              }));
```

### Combining with Other Modifiers

```cpp
// Group + Sort within groups
batch.Connect(passGenerator, PassGenConfig::DISPATCH_PASS,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              GroupKey(&DispatchPass::groupId),
              SortBy([](const DispatchPass& a, const DispatchPass& b) {
                  return a.priority > b.priority;
              }));
```

---

## Benefits

| Benefit | Description |
|---------|-------------|
| **Extensibility** | Add sort strategies without new classes |
| **Expressiveness** | Clear intent (ascending vs descending vs custom) |
| **Flexibility** | Lambda comparators for complex logic |
| **Consistency** | Matches `GroupKey()` helper pattern |
| **Realistic** | Won't stack multiple sort modifiers on same connection |
| **Type Safety** | Compile-time checks for comparator signatures |

---

## Implementation Plan

### Phase 1: Design & API (4h)
1. Finalize strategy enum values
2. Design type-erased comparator storage
3. Define helper function signatures
4. Write API documentation

### Phase 2: Implementation (8h)
1. Create `AccumulationSortModifier` class
2. Implement strategy-based sorting in `AccumulationConnectionRule`
3. Add helper functions (`SortAscending`, `SortDescending`, `SortBy`)
4. Update existing `AccumulationSortConfig` callsites (if any)

### Phase 3: Testing (6h)
1. Unit tests for each strategy
2. Integration tests with accumulation slots
3. Test custom comparator lambda capture
4. Test combination with `GroupKeyModifier`

### Phase 4: Documentation (2h)
1. Update modifier documentation
2. Add usage examples to RenderGraph docs
3. Create migration guide from `AccumulationSortConfig`

**Total Estimate:** 20 hours

---

## Migration Path

### Backward Compatibility

Option 1: Deprecate `AccumulationSortConfig`
```cpp
class AccumulationSortConfig : public RuleConfig {
    [[deprecated("Use AccumulationSortModifier with SortAscending()")]]
    explicit AccumulationSortConfig(int32_t key);
};
```

Option 2: Keep as alias
```cpp
inline auto AccumulationSortConfig(int32_t key) {
    return SortAscending(key);  // Default to ascending
}
```

### Migration Examples

```cpp
// Old (current)
ConnectionMeta{}.With<AccumulationSortConfig>(5)

// New (strategy-based)
SortAscending(5)  // Clear intent
SortDescending(5) // Or descending
```

---

## Technical Considerations

### Type Erasure for Custom Comparators

Store comparators in `ConnectionContext::metadata`:

```cpp
// Option 1: std::function (type-erased, some overhead)
std::function<bool(const void*, const void*)> comparator_;

// Option 2: std::any (fully type-erased)
std::any comparator_;

// Option 3: Virtual interface (zero-overhead polymorphism)
struct ComparatorBase {
    virtual bool Compare(const void*, const void*) const = 0;
};
```

**Recommendation:** Start with `std::function` for simplicity, optimize later if needed.

### Runtime Sorting Implementation

AccumulationConnectionRule needs to sort accumulated elements:

```cpp
void AccumulationConnectionRule::SortAccumulatedData(
    std::vector<ElementType>& elements,
    const ConnectionContext& ctx
) {
    if (ctx.metadata.contains("sortOrder")) {
        SortOrder order = std::any_cast<SortOrder>(ctx.metadata.at("sortOrder"));

        switch (order) {
            case SortOrder::Ascending: {
                int32_t key = std::any_cast<int32_t>(ctx.metadata.at("sortKey"));
                std::sort(elements.begin(), elements.end(),
                    [key](const auto& a, const auto& b) {
                        return GetSortKey(a) < GetSortKey(b);
                    });
                break;
            }
            case SortOrder::Custom: {
                auto comp = std::any_cast<Comparator>(ctx.metadata.at("customComparator"));
                std::sort(elements.begin(), elements.end(), comp);
                break;
            }
            // ... other cases
        }
    }
}
```

---

## Related Work

### Similar Patterns in Codebase

**GroupKeyModifier:** Uses member pointer + helper function
```cpp
GroupKey(&DispatchPass::groupId)
```

**FieldExtractionModifier:** Uses member pointer + helper function
```cpp
ExtractField(&CameraData::cameraPos)
```

**Proposed Pattern:** Matches established conventions
```cpp
SortBy([](const T& a, const T& b) { return a.field < b.field; })
```

### Sprint 6.1 Context

This design addresses the limitation discovered during Sprint 6.1 (MultiDispatchNode):
- GROUP_INPUTS accumulation slot works ✅
- GroupKeyModifier partitions by group ✅
- Sorting within groups not yet implemented ⏳

Future use case:
```cpp
// Partition by group, sort by priority within each group
batch.Connect(passGenerator, PassGenConfig::DISPATCH_PASS,
              multiDispatch, MultiDispatchConfig::GROUP_INPUTS,
              GroupKey(&DispatchPass::groupId),
              SortDescending(/* priority field */));
```

---

## Open Questions

1. **Sort Stability:** Should sorting be stable? (preserve relative order of equal elements)
   - Recommendation: Yes, use `std::stable_sort` for deterministic behavior

2. **Performance:** Impact of sorting on accumulation?
   - Small arrays (< 100 elements): Negligible
   - Large arrays: Profile and optimize if needed

3. **Multi-field Sort:** Support compound keys?
   ```cpp
   SortBy(Field(&Pass::priority, Descending),
          Field(&Pass::timestamp, Ascending))
   ```
   - Recommendation: Not yet, use lambda for now

4. **Default Behavior:** What if no sort modifier specified?
   - Recommendation: Insertion order (current behavior)

---

## Success Criteria

- [ ] Single modifier class handles all sort strategies
- [ ] Helper functions match existing patterns
- [ ] Custom comparators work with lambdas
- [ ] Combines cleanly with other modifiers
- [ ] Zero breaking changes to existing code
- [ ] Documentation and examples complete
- [ ] Tests cover all strategies and edge cases

---

## References

**Current Implementation:**
- [AccumulationSortConfig.h](../../libraries/RenderGraph/include/Connection/Modifiers/AccumulationSortConfig.h)
- [AccumulationConnectionRule](../../libraries/RenderGraph/include/Connection/Rules/AccumulationConnectionRule.h)

**Related Modifiers:**
- [GroupKeyModifier.h](../../libraries/RenderGraph/include/Connection/Modifiers/GroupKeyModifier.h) - Sprint 6.1
- [FieldExtractionModifier.h](../../libraries/RenderGraph/include/Connection/Modifiers/FieldExtractionModifier.h)

**Design Patterns:**
- [variadic-modifier-api.md](variadic-modifier-api.md) - Sprint 6.0.1
- [accumulation-slot-proper-design.md](accumulation-slot-proper-design.md) - Sprint 6.0.2

---

**Created:** 2026-01-06
**Author:** Architecture discussion during Sprint 6.1
**Sprint:** Future work (post-6.1)
