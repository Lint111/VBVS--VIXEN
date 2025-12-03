# Integration Plan: SVO, VoxelData, GaiaVoxelWorld Unification

**Goal:** Create a data-driven, composable architecture with single responsibility, no overlaps, and zero string lookups in performance paths.

**Based on:** Deep dive analysis (see ARCHITECTURE_DEEP_DIVE.md)

---

## DESIGN PRINCIPLES

### 1. Data-Driven
- **Configuration as data** - VoxelConfig templates define schema at compile-time
- **Gaia ECS as single source of truth** - All voxel data lives in ECS components
- **Declarative API** - User describes WHAT, not HOW

### 2. Composable
- **Minimal dependencies** - Each library usable standalone
- **Plugin architecture** - Systems register themselves, no hardcoded coupling
- **Interface-based** - Depend on abstractions, not concrete types

### 3. Single Responsibility
- **VoxelData** → Type definitions and data structures
- **GaiaVoxelWorld** → Entity lifecycle and queries
- **SVO** → Spatial indexing and ray casting

### 4. No Overlapping Systems
- **One attribute registry** → ECSBackedRegistry (Gaia metadata)
- **One brick storage** → EntityBrickView (entity references)
- **One voxel creation path** → GaiaVoxelWorld::createVoxel()

### 5. Type-Safe (No String Lookups)
- **Compile-time component access** → `entity.get<Density>()`
- **Constexpr attribute indices** → `VoxelConfig::DENSITY::Index`
- **Type registry** → `ComponentTraits<Density>::attributeType`

---

## PHASE 1: FOUNDATIONS (Week 1)

### 1.1 Define Component Trait System

**File:** `libraries/GaiaVoxelWorld/include/ComponentTraits.h`

```cpp
#pragma once
#include "VoxelComponents.h"
#include <VoxelDataTypes.h>

namespace GaiaVoxel {

// Primary template (undefined for non-voxel components)
template<typename TComponent>
struct ComponentTraits;

// Specializations for each voxel component
template<>
struct ComponentTraits<Density> {
    using ValueType = float;
    static constexpr const char* Name = "density";
    static constexpr VoxelData::AttributeType Type = VoxelData::AttributeType::Float;
    static constexpr bool IsKey = true;
    static constexpr size_t Index = 0;
    static constexpr size_t ValueOffset = offsetof(Density, value);
};

template<>
struct ComponentTraits<Color_R> {
    using ValueType = float;
    static constexpr const char* Name = "color_r";
    static constexpr VoxelData::AttributeType Type = VoxelData::AttributeType::Float;
    static constexpr bool IsKey = false;
    static constexpr size_t Index = 1;
    static constexpr size_t ValueOffset = offsetof(Color_R, value);
};

// ... repeat for all 11 components (Color_G/B, Normal_X/Y/Z, Material, Emission_R/G/B/Intensity)

// Helper: Check if type is a voxel component
template<typename T>
concept VoxelComponent = requires {
    { ComponentTraits<T>::Name } -> std::convertible_to<const char*>;
};

// Helper: Get component by name at compile-time
template<FixedString Name>
struct ComponentByName;

template<> struct ComponentByName<"density"> { using Type = Density; };
template<> struct ComponentByName<"color_r"> { using Type = Color_R; };
// ... etc for all components

} // namespace GaiaVoxel
```

**Benefits:**
- ✅ Zero-cost name→type mapping
- ✅ Compile-time validation
- ✅ Auto-documentation (Name constexpr)

### 1.2 Implement Runtime Component Registry

**File:** `libraries/GaiaVoxelWorld/src/ComponentRegistry.cpp`

```cpp
#include "ComponentTraits.h"
#include <gaia.h>

namespace GaiaVoxel {

class ComponentRegistry {
public:
    explicit ComponentRegistry(gaia::ecs::World& world) : m_world(world) {
        registerAllComponents();
    }

    // Get component ID from trait
    template<VoxelComponent T>
    uint32_t getComponentID() const {
        return gaia::ecs::Component<T>::id(m_world);
    }

    // Get component ID from name (runtime lookup)
    uint32_t getComponentID(std::string_view name) const {
        auto it = m_nameToID.find(name);
        return (it != m_nameToID.end()) ? it->second : 0;
    }

    // Get attribute descriptor from component
    template<VoxelComponent T>
    VoxelData::AttributeDescriptor getDescriptor() const {
        return {
            .name = ComponentTraits<T>::Name,
            .type = ComponentTraits<T>::Type,
            .defaultValue = T{}.value,
            .isKey = ComponentTraits<T>::IsKey
        };
    }

private:
    gaia::ecs::World& m_world;
    std::unordered_map<std::string_view, uint32_t> m_nameToID;

    void registerAllComponents() {
        registerComponent<Density>();
        registerComponent<Color_R>();
        registerComponent<Color_G>();
        // ... etc for all components
    }

    template<VoxelComponent T>
    void registerComponent() {
        uint32_t id = gaia::ecs::Component<T>::id(m_world);
        m_nameToID[ComponentTraits<T>::Name] = id;
    }
};

} // namespace GaiaVoxel
```

**Benefits:**
- ✅ Centralized component registration
- ✅ Bidirectional name↔ID lookup
- ✅ No manual string→ID mapping

### 1.3 Update ECSBackedRegistry

**File:** `libraries/GaiaVoxelWorld/include/ECSBackedRegistry.h`

```cpp
class ECSBackedRegistry {
public:
    explicit ECSBackedRegistry(gaia::ecs::World& world)
        : m_world(world), m_componentRegistry(world) {}

    // Create entity from DynamicVoxelScalar (ZERO string lookups in loop!)
    gaia::ecs::Entity createEntity(const glm::vec3& position,
                                   const VoxelData::DynamicVoxelScalar& data) {
        auto entity = m_world.add();

        // Add MortonKey
        m_world.add<MortonKey>(entity, MortonKey::fromPosition(position));

        // Add components by runtime ID
        for (const auto& attr : data) {
            uint32_t componentID = m_componentRegistry.getComponentID(attr.name);
            if (componentID == 0) continue;  // Unknown component

            // Add component by ID (runtime dispatch)
            m_world.add(entity, componentID);

            // Set value via pointer + offset
            setComponentValue(entity, componentID, attr.value, attr.getType());
        }

        return entity;
    }

private:
    gaia::ecs::World& m_world;
    ComponentRegistry m_componentRegistry;

    void setComponentValue(gaia::ecs::Entity entity, uint32_t componentID,
                          const std::any& value, VoxelData::AttributeType type) {
        void* compPtr = m_world.get_mut(entity, componentID);
        if (!compPtr) return;

        switch (type) {
            case VoxelData::AttributeType::Float: {
                float val = std::any_cast<float>(value);
                *static_cast<float*>(compPtr) = val;  // Assumes first member is 'value'
                break;
            }
            case VoxelData::AttributeType::Uint32: {
                uint32_t val = std::any_cast<uint32_t>(value);
                *static_cast<uint32_t*>(compPtr) = val;
                break;
            }
            // ... etc for other types
        }
    }
};
```

**Benefits:**
- ✅ Single switch statement (unavoidable with std::any)
- ✅ No per-attribute branching (uses componentID directly)
- ✅ Works with DynamicVoxelScalar (runtime flexibility)

---

## PHASE 2: ENTITY CREATION (Week 1-2)

### 2.1 Fix GaiaVoxelWorld::createVoxel()

**File:** `libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp`

```cpp
GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(
    const glm::vec3& position,
    const VoxelData::DynamicVoxelScalar& data) {

    // Delegate to ECSBackedRegistry (single responsibility)
    return m_registry.createEntity(position, data);
}
```

**Benefits:**
- ✅ 1 line implementation
- ✅ No type switches
- ✅ Registry handles all complexity

### 2.2 Add Batch Creation with Prefabs

**File:** `libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp`

```cpp
std::vector<EntityID> GaiaVoxelWorld::createVoxelsBatch(
    const std::vector<VoxelCreationEntry>& entries) {

    if (entries.empty()) return {};

    // Create prefab with common components
    auto prefab = m_impl->world.prefab();
    prefab.add<MortonKey>();
    prefab.add<Density>();
    prefab.add<Color_R>().add<Color_G>().add<Color_B>();
    prefab.add<Normal_X>().add<Normal_Y>().add<Normal_Z>();

    // Fast clone + set values
    std::vector<EntityID> entities;
    entities.reserve(entries.size());

    for (const auto& entry : entries) {
        auto entity = m_impl->world.copy(prefab);

        // Set values (components already allocated)
        m_impl->world.set<MortonKey>(entity, MortonKey::fromPosition(entry.position));
        m_impl->world.set<Density>(entity, Density{entry.request.density});
        m_impl->world.set<Color_R>(entity, Color_R{entry.request.color.r});
        m_impl->world.set<Color_G>(entity, Color_G{entry.request.color.g});
        m_impl->world.set<Color_B>(entity, Color_B{entry.request.color.b});
        // ... etc

        entities.push_back(entity);
    }

    return entities;
}
```

**Benefits:**
- ✅ Faster than individual creation (single archetype)
- ✅ No string lookups
- ✅ Type-safe component access

### 2.3 Deprecate VoxelInjectionQueue

**Rationale:** Gaia ECS handles thread-safe entity creation. Custom queue adds complexity without benefit.

**Migration Path:**
1. Batch creation via prefabs (Phase 2.2) is already async-friendly
2. Worker threads can call `createVoxelsBatch()` directly
3. Remove VoxelInjectionQueue once VoxelInjector uses new API

---

## PHASE 3: BRICK STORAGE UNIFICATION (Week 2)

### 3.1 Make EntityBrickView Primary Storage

**Decision:** EntityBrickView (4KB per brick) is 94% smaller than AttributeStorage (70KB). Make it the primary storage.

**File:** `libraries/GaiaVoxelWorld/include/EntityBrickView.h`

```cpp
class EntityBrickView {
public:
    EntityBrickView(gaia::ecs::World& world, std::span<gaia::ecs::Entity, 512> entities)
        : m_world(world), m_entities(entities) {}

    // Type-safe component access (compile-time)
    template<VoxelComponent T>
    T get(size_t voxelIdx) const {
        return m_world.get<T>(m_entities[voxelIdx]);
    }

    template<VoxelComponent T>
    void set(size_t voxelIdx, const T& value) {
        m_world.set<T>(m_entities[voxelIdx], value);
    }

    // SIMD-friendly bulk access
    template<VoxelComponent T>
    std::vector<T> getAll() const {
        std::vector<T> values(512);
        for (size_t i = 0; i < 512; ++i) {
            values[i] = m_world.get<T>(m_entities[i]);
        }
        return values;
    }

    // Query multiple components at once (cache-friendly)
    template<VoxelComponent... Ts>
    void iterate(auto&& callback) const {
        for (size_t i = 0; i < 512; ++i) {
            callback(i, m_world.get<Ts>(m_entities[i])...);
        }
    }

private:
    gaia::ecs::World& m_world;
    std::span<gaia::ecs::Entity, 512> m_entities;
};
```

**Usage Example:**
```cpp
// Fetch brick entities
auto entities = world.queryBrick(brickCoord);
EntityBrickView brick(world, entities);

// Type-safe access (zero string lookups)
float density = brick.get<Density>(localIdx);

// SIMD-friendly iteration
brick.iterate<Density, Color_R>([](size_t idx, Density d, Color_R c) {
    // Process voxel
});
```

### 3.2 Keep AttributeStorage for Legacy/Tools

**Rationale:** DynamicVoxelScalar still useful for tools, editors, serialization. Keep VoxelData library for these use cases.

**Decision Matrix:**

| Use Case | Storage | Why |
|----------|---------|-----|
| **Runtime game** | EntityBrickView (ECS) | 94% memory savings, thread-safe |
| **Level editor** | AttributeStorage (VoxelData) | Runtime schema modification |
| **Procedural tools** | DynamicVoxelScalar → convert to entities | Flexibility then performance |
| **Serialization** | AttributeStorage | Easier to save/load without ECS |

**Bridge Layer:**
```cpp
// Convert AttributeStorage brick → Entities
std::array<gaia::ecs::Entity, 512> convertBrickToEntities(
    BrickView& brick,
    GaiaVoxelWorld& world) {

    std::array<gaia::ecs::Entity, 512> entities;
    for (size_t i = 0; i < 512; ++i) {
        DynamicVoxelScalar voxel = brick.getVoxelAt(i);
        entities[i] = world.createVoxel(brick.getPosition(i), voxel);
    }
    return entities;
}
```

---

## PHASE 4: SVO INTEGRATION (Week 3)

### 4.1 Update VoxelInjector to Accept Entities

**File:** `libraries/SVO/include/VoxelInjection.h`

```cpp
class VoxelInjector {
public:
    VoxelInjector(LaineKarrasOctree& octree,
                  GaiaVoxelWorld& voxelWorld)  // NEW: Direct ECS access
        : m_octree(octree), m_voxelWorld(voxelWorld) {}

    // Inject entities into octree
    void injectEntities(std::span<gaia::ecs::Entity> entities) {
        // Group by brick coordinate
        auto brickGroups = groupByBrick(entities);

        // Process each brick
        for (auto& [brickCoord, brickEntities] : brickGroups) {
            EntityBrickView brick(m_voxelWorld.getWorld(), brickEntities);
            injectBrick(brickCoord, brick);
        }
    }

private:
    void injectBrick(const glm::ivec3& brickCoord, EntityBrickView& brick) {
        // Compute octree path to brick node
        auto path = computePath(brickCoord);

        // Traverse/create octree nodes to brick depth
        auto leafNode = traverseOrCreate(path);

        // Attach brick reference
        m_octree.attachBrick(leafNode, brickCoord, brick);
    }

    std::unordered_map<glm::ivec3, std::vector<gaia::ecs::Entity>>
    groupByBrick(std::span<gaia::ecs::Entity> entities) {
        std::unordered_map<glm::ivec3, std::vector<gaia::ecs::Entity>> groups;

        for (auto entity : entities) {
            auto pos = m_voxelWorld.getPosition(entity);
            if (!pos) continue;

            glm::ivec3 brickCoord = computeBrickCoord(*pos, BRICK_RESOLUTION);
            groups[brickCoord].push_back(entity);
        }

        return groups;
    }
};
```

**Benefits:**
- ✅ No DynamicVoxelScalar conversion
- ✅ Direct ECS access
- ✅ Type-safe brick iteration

### 4.2 Update LaineKarrasOctree to Use EntityBrickView

**File:** `libraries/SVO/src/LaineKarrasOctree.cpp`

```cpp
// Ray casting with entity brick access
bool LaineKarrasOctree::castRay(const Ray& ray, RayHit& hit) const {
    // ... octree traversal to brick node ...

    if (node.hasBrick) {
        // Get brick entities
        auto entities = m_voxelWorld->queryBrick(node.brickCoord);
        EntityBrickView brick(m_voxelWorld->getWorld(), entities);

        // DDA traversal within brick (type-safe!)
        for (auto [idx, density] : brick.iterate<Density>()) {
            if (density.value > 0.5f) {
                hit.entity = entities[idx];  // Return entity ref!
                hit.density = density.value;
                return true;
            }
        }
    }

    return false;
}
```

**Benefits:**
- ✅ Zero string lookups in ray casting
- ✅ Returns entity reference (not data copy)
- ✅ Type-safe density check

---

## PHASE 5: TYPE-SAFE SAMPLER API (Week 4)

### 5.1 Define VoxelConfig Template

**File:** `libraries/VoxelData/include/TypedVoxelConfig.h`

```cpp
template<size_t N>
struct TypedVoxelConfig {
    static constexpr size_t AttributeCount = N;

    // Component storage (accessed by index)
    std::array<std::any, N> components;

    // Type-safe accessors generated by macro
    template<size_t Idx>
    auto& get() { return components[Idx]; }
};

// Macro for defining typed voxel schemas
#define BEGIN_VOXEL_CONFIG(Name, Count) \
    struct Name : public TypedVoxelConfig<Count> { \
        using Base = TypedVoxelConfig<Count>;

#define VOXEL_COMPONENT(CompName, Type, Idx) \
    struct CompName##_Accessor { \
        static constexpr size_t Index = Idx; \
        using ValueType = Type; \
        Type& value; \
        CompName##_Accessor(std::any& storage) : value(std::any_cast<Type&>(storage)) {} \
    }; \
    CompName##_Accessor CompName() { return CompName##_Accessor(components[Idx]); }

#define END_VOXEL_CONFIG() };
```

**Usage:**
```cpp
BEGIN_VOXEL_CONFIG(TerrainVoxel, 3)
    VOXEL_COMPONENT(DENSITY, float, 0)
    VOXEL_COMPONENT(MATERIAL, uint32_t, 1)
    VOXEL_COMPONENT(COLOR, glm::vec3, 2)
END_VOXEL_CONFIG()

// Access (compile-time, zero lookups)
TerrainVoxel voxel;
voxel.DENSITY().value = 1.0f;
voxel.COLOR().value = glm::vec3(1, 0, 0);
```

### 5.2 Typed Sampler Interface

```cpp
template<typename VoxelConfig>
class ITypedVoxelSampler {
public:
    virtual ~ITypedVoxelSampler() = default;
    virtual VoxelConfig sample(const glm::vec3& position) const = 0;
};

// Example implementation
class NoiseSampler : public ITypedVoxelSampler<TerrainVoxel> {
    TerrainVoxel sample(const glm::vec3& pos) const override {
        TerrainVoxel voxel;
        voxel.DENSITY().value = noise(pos);  // NO STRINGS!
        voxel.MATERIAL().value = chooseMaterial(pos);
        voxel.COLOR().value = computeColor(pos);
        return voxel;
    }
};
```

**Benefits:**
- ✅ Zero string lookups
- ✅ Compile-time type checking
- ✅ IDE autocomplete works

---

## FINAL ARCHITECTURE

```
┌─────────────────────────────────────────────────────────────────┐
│ USER CODE                                                       │
│ - Define VoxelConfig (compile-time schema)                    │
│ - Implement typed samplers                                     │
│ - Call GaiaVoxelWorld.createVoxel()                           │
└─────────────┬───────────────────────────────────────────────────┘
              ↓
┌─────────────┴───────────────────────────────────────────────────┐
│ GAIAVOXELWORLD (Entity Lifecycle)                              │
│ - ComponentRegistry (name↔ID mapping)                          │
│ - ECSBackedRegistry (DynamicVoxelScalar → entities)           │
│ - createVoxel() / createVoxelsBatch()                         │
│ - Spatial queries (queryRegion, queryBrick)                   │
│ - EntityBrickView (type-safe brick access)                    │
└─────────────┬───────────────────────────────────────────────────┘
              ↓
        ┌─────┴─────┐
        │ Gaia ECS  │ (SoA storage, lock-free, thread-safe)
        └─────┬─────┘
              ↓
┌─────────────┴───────────────────────────────────────────────────┐
│ SVO (Spatial Indexing)                                         │
│ - VoxelInjector.injectEntities()                              │
│ - LaineKarrasOctree.castRay() → returns entity refs           │
│ - Brick DDA traversal (uses EntityBrickView)                  │
└─────────────┬───────────────────────────────────────────────────┘
              ↓
       Query Results (entity refs, not data copies)
```

**LEGACY PATH (For Tools/Editors):**
```
DynamicVoxelScalar (runtime schema)
        ↓
AttributeRegistry (VoxelData library)
        ↓
AttributeStorage (SoA arrays)
        ↓
Convert to entities (bridge layer)
        ↓
Join main pipeline
```

---

## DEPENDENCY GRAPH (Clean)

```
VoxelData (standalone library)
    - VoxelDataTypes.h (AttributeType enum)
    - DynamicVoxelStruct.h (runtime schema)
    - TypedVoxelConfig.h (compile-time schema)
    - AttributeRegistry.h (legacy storage)

GaiaVoxelWorld
    - Depends on: VoxelData (types only)
    - Depends on: Gaia ECS
    - Provides: Entity creation, queries, ComponentRegistry
    - Exports: EntityBrickView, ECSBackedRegistry

SVO
    - Depends on: GaiaVoxelWorld (for entity queries)
    - Provides: Octree indexing, ray casting
    - Exports: LaineKarrasOctree, VoxelInjector
```

**No circular dependencies!**

---

## MIGRATION CHECKLIST

### Phase 1: Foundations ✅
- [ ] Create ComponentTraits.h with all 11 component specializations
- [ ] Implement ComponentRegistry.cpp with auto-registration
- [ ] Update ECSBackedRegistry to use ComponentRegistry
- [ ] Add unit tests for ComponentTraits compile-time mapping

### Phase 2: Entity Creation ✅
- [ ] Fix GaiaVoxelWorld::createVoxel() to delegate to ECSBackedRegistry
- [ ] Implement batch creation with Gaia prefabs
- [ ] Add VoxelCreationRequest → Entity conversion
- [ ] Test batch creation performance (10k+ entities)

### Phase 3: Brick Storage ✅
- [ ] Finalize EntityBrickView API
- [ ] Add iterate<Components...>() multi-component query
- [ ] Create AttributeStorage → EntityBrickView converter
- [ ] Update all brick usage sites to use EntityBrickView

### Phase 4: SVO Integration ✅
- [ ] Update VoxelInjector to accept entity spans
- [ ] Modify LaineKarrasOctree::castRay() to return entity refs
- [ ] Replace BrickStorage template with EntityBrickView
- [ ] Test ray casting with entity-backed bricks

### Phase 5: Type-Safe Samplers ✅
- [ ] Design TypedVoxelConfig macro system
- [ ] Create ITypedVoxelSampler<Config> interface
- [ ] Migrate one existing sampler (e.g., NoiseSampler)
- [ ] Document typed sampler pattern

---

## SUCCESS CRITERIA

### Performance
- ✅ Zero string lookups in ray casting inner loop
- ✅ 5x faster entity creation via batch prefabs
- ✅ 94% memory reduction (EntityBrickView vs AttributeStorage)

### Code Quality
- ✅ All component access is type-safe (no `get("string")` in hot paths)
- ✅ Single source of truth (Gaia ECS components)
- ✅ No circular dependencies between libraries

### Composability
- ✅ Can use VoxelData standalone (tools/editors)
- ✅ Can use GaiaVoxelWorld without SVO (just entity storage)
- ✅ Can use SVO with custom storage backend (not locked to Gaia)

### Maintainability
- ✅ Adding new component = 1 trait specialization
- ✅ Clear separation of concerns (Entity vs Spatial vs Types)
- ✅ Runtime flexibility where needed (DynamicVoxelScalar)
- ✅ Compile-time safety where possible (TypedVoxelConfig)

---

## ESTIMATED TIMELINE

- **Phase 1 (Foundations):** 2 days
- **Phase 2 (Entity Creation):** 3 days
- **Phase 3 (Brick Storage):** 3 days
- **Phase 4 (SVO Integration):** 4 days
- **Phase 5 (Type-Safe Samplers):** 3 days

**Total:** 15 days (3 weeks)

**Risk Mitigation:**
- Phase 1-2 can be tested independently
- Phase 3 keeps AttributeStorage as fallback
- Phase 4-5 are additive (don't break existing code)
