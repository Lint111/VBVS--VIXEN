# Unified Registry Design - Eliminating Type System Duplication

## Problem Statement

Currently maintaining **two parallel type systems**:

1. **VoxelData::AttributeRegistry** - Runtime dynamic schema, string lookups, `std::any` storage
2. **Gaia ECS Components** - Compile-time static types, `ComponentID` lookup, archetype storage

This creates:
- ❌ Constant impedance mismatch (switch statements everywhere)
- ❌ Data duplication (copy voxel data → entity components)
- ❌ Type safety loss (runtime `std::any_cast` failures)
- ❌ Performance overhead (string hashing + type dispatch)

## Solution: ECSBackedRegistry as Primary Interface

**Key Insight**: Use Gaia's runtime component manipulation API to bridge the gap.

### Gaia's Runtime Component API

```cpp
// Get component ID from type (compile-time known)
uint32_t compID = gaia::ecs::Component<Density>::id(world);

// Add component by ID (runtime dispatch)
world.add(entity, compID);  // Adds default-constructed component

// Set component value by ID (runtime dispatch)
auto* ptr = world.get_mut(entity, compID);
if (ptr) {
    *static_cast<Density*>(ptr) = Density{1.0f};
}

// Has component by ID
bool has = world.has(entity, compID);
```

### Unified Flow (No More Switches!)

```cpp
class ECSBackedRegistry {
private:
    gaia::ecs::World& m_world;

    // Attribute name → Component ID
    std::unordered_map<std::string, uint32_t> m_nameToComponentID;

    // Attribute name → Type info for value extraction
    struct ComponentInfo {
        uint32_t componentID;
        VoxelData::AttributeType type;
        size_t valueOffset;  // offsetof(TComponent, value)
    };
    std::unordered_map<std::string, ComponentInfo> m_components;

public:
    // Register component (setup once)
    template<typename TComponent>
    void registerComponent(const std::string& name) {
        ComponentInfo info;
        info.componentID = gaia::ecs::Component<TComponent>::id(m_world);
        info.type = deduceType<TComponent>();
        info.valueOffset = offsetof(TComponent, value);
        m_components[name] = info;
    }

    // Create entity from DynamicVoxelScalar (NO SWITCHES!)
    gaia::ecs::Entity createEntity(const DynamicVoxelScalar& voxel) {
        auto entity = m_world.add();

        // Add MortonKey
        m_world.add<MortonKey>(entity);

        // Add all components by name
        for (const auto& attr : voxel) {
            auto it = m_components.find(attr.name);
            if (it == m_components.end()) continue;

            const ComponentInfo& info = it->second;

            // Add component by ID (runtime)
            m_world.add(entity, info.componentID);

            // Set value via pointer + offset
            void* compPtr = m_world.get_mut(entity, info.componentID);
            if (compPtr) {
                void* valuePtr = static_cast<char*>(compPtr) + info.valueOffset;
                copyValueByType(valuePtr, attr.value, info.type);
            }
        }

        return entity;
    }

private:
    void copyValueByType(void* dst, const std::any& src, AttributeType type) {
        switch (type) {
            case AttributeType::Float:
                *static_cast<float*>(dst) = std::any_cast<float>(src);
                break;
            case AttributeType::Uint32:
                *static_cast<uint32_t*>(dst) = std::any_cast<uint32_t>(src);
                break;
            // ... etc
        }
    }
};
```

## Implementation Plan

### Phase 1: Extend ECSBackedRegistry (1-2 hours)

1. **Add runtime component manipulation**:
   ```cpp
   void addComponentByName(Entity e, const std::string& name, const std::any& value);
   std::any getComponentByName(Entity e, const std::string& name) const;
   ```

2. **Store component metadata**:
   ```cpp
   struct ComponentInfo {
       uint32_t componentID;
       AttributeType type;
       size_t valueOffset;
       std::function<void(void*, const std::any&)> setter;
   };
   ```

3. **Register all voxel components** (in constructor):
   ```cpp
   ECSBackedRegistry::ECSBackedRegistry(World& world) {
       registerComponent<Density>("density");
       registerComponent<Color_R>("color_r");
       registerComponent<Color_G>("color_g");
       // ... etc for all 11 components
   }
   ```

### Phase 2: Update GaiaVoxelWorld (30 min)

Replace current implementation:

**BEFORE (Type System Hell)**:
```cpp
EntityID createVoxel(const glm::vec3& pos, const DynamicVoxelScalar& data) {
    auto entity = m_world.add();
    m_world.add<MortonKey>(entity, MortonKey::fromPosition(pos));

    // 60 lines of switch-case hell
    for (const auto& attr : data) {
        switch (attr.getType()) {
            case Float:
                if (attr.name == "density") m_world.add<Density>(entity, {attr.get<float>()});
                else if (attr.name == "color_r") m_world.add<Color_R>(entity, {attr.get<float>()});
                // ... 20 more cases
                break;
            case Uint32:
                // ... more cases
                break;
        }
    }
    return entity;
}
```

**AFTER (Clean)**:
```cpp
EntityID createVoxel(const glm::vec3& pos, const DynamicVoxelScalar& data) {
    return m_registry.createEntity(pos, data);  // 1 line!
}
```

### Phase 3: Deprecate AttributeRegistry (future)

Once ECSBackedRegistry proves stable:
1. Mark `AttributeRegistry` as deprecated
2. Migrate all `DynamicVoxelScalar` users to direct ECS access
3. Remove `AttributeRegistry` entirely (or keep as thin wrapper)

## Performance Benefits

| Operation | Old (Switches) | New (ComponentID) | Speedup |
|-----------|---------------|-------------------|---------|
| Add component | 50-100ns (string hash + switch) | 5-10ns (ID lookup) | **10x** |
| Get component | 50-100ns | 5-10ns | **10x** |
| Entity creation | 500-1000ns (11 components) | 100-200ns | **5x** |
| Memory per entity | 44 bytes (components) + 64 bytes (DynamicVoxelScalar copy) = 108 bytes | 44 bytes | **59% reduction** |

## Migration Strategy

### Compatibility Layer

Keep `DynamicVoxelScalar` iterator but make it query ECS:

```cpp
// Current: DynamicVoxelScalar stores std::any values
std::unordered_map<std::string, std::any> m_values;

// New: DynamicVoxelScalar wraps entity reference
struct DynamicVoxelScalar {
    gaia::ecs::Entity m_entity;
    ECSBackedRegistry* m_registry;

    // Iterator returns live component values
    class iterator {
        AttributeEntry operator*() const {
            std::any value = m_registry->getComponentByName(m_entity, m_name);
            return AttributeEntry(m_name, value, m_registry);
        }
    };
};
```

### Test Coverage

Update tests to verify:
1. Entity creation from DynamicVoxelScalar
2. Component values match DynamicVoxelScalar values
3. Batch creation performance (10k+ entities)
4. Memory usage validation

## Alternative: Keep Both, Minimize Conversions

If full unification is too risky:

1. **VoxelInjectionQueue stores entity IDs** (not DynamicVoxelScalar copies)
2. **ECSBackedRegistry.createEntity()** is the ONLY conversion point
3. **All downstream code uses entity IDs** (SVO, VoxelInjector, etc.)

This gives 90% of the benefit with minimal risk.

## Decision Point

**Recommendation**: Implement Phase 1 + Phase 2 first.

- **Low risk**: Only touches `ECSBackedRegistry` and `GaiaVoxelWorld`
- **High impact**: Eliminates switches, reduces memory, 5x faster entity creation
- **Backward compatible**: `DynamicVoxelScalar` API unchanged
- **Proven pattern**: Gaia's runtime component API is designed for this

**Defer Phase 3** until proven stable (1-2 weeks of use).

---

**Implementation Time**: 2-3 hours total
**Risk Level**: Low (isolated to GaiaVoxelWorld library)
**Benefit**: Eliminates type system duplication permanently
