# Integration Plan V2 - Simplified Component Architecture

**Last Updated:** November 22, 2025
**Status:** Ready for Implementation

---

## PHASE 1: Component Foundation (1-2 days)

### 1.1 Finalize VoxelComponents_v2.h ✅ DONE

**File:** `libraries/GaiaVoxelWorld/include/VoxelComponents_v2.h`

**Simplified Macros:**
```cpp
// Scalar (any type)
VOXEL_COMPONENT_SCALAR(ComponentName, LogicalName, Type, DefaultVal)

// Vec3 (float only, Gaia layout control)
VOXEL_COMPONENT_VEC3(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2)
```

**Component Declarations:**
```cpp
VOXEL_COMPONENT_SCALAR(MortonKey, "position", uint64_t, 0)
VOXEL_COMPONENT_SCALAR(Density, "density", float, 1.0f)
VOXEL_COMPONENT_SCALAR(Material, "material", uint32_t, 0)

VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)
VOXEL_COMPONENT_VEC3(Normal, "normal", x, y, z, AoS, 0.0f, 1.0f, 0.0f)
VOXEL_COMPONENT_VEC3(Emission, "emission", r, g, b, AoS, 0.0f, 0.0f, 0.0f)

struct Solid {};  // Tag component
```

**ComponentTraits (Minimal):**
```cpp
template<>
struct ComponentTraits<Color> {
    static constexpr const char* Name = Color::Name;  // ONLY this!
};

DEFINE_COMPONENT_TRAITS(Color)  // One-liner registration
```

**Type Detection (Automatic):**
```cpp
template<typename T>
concept Vec3Component = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
};

template<typename T>
concept ScalarComponent = requires(T t) { { t.value }; };
```

---

### 1.2 Replace VoxelComponents.h

**Action:** Rename v2 → production

```bash
mv libraries/GaiaVoxelWorld/include/VoxelComponents.h \
   libraries/GaiaVoxelWorld/include/VoxelComponents_OLD.h

mv libraries/GaiaVoxelWorld/include/VoxelComponents_v2.h \
   libraries/GaiaVoxelWorld/include/VoxelComponents.h
```

**Update includes:** All files using old split components (`Color_R/G/B`) need updating

---

### 1.3 Test Component Compilation

**File:** `libraries/GaiaVoxelWorld/tests/test_components.cpp`

```cpp
#include "VoxelComponents.h"
#include <gtest/gtest.h>

TEST(Components, ScalarCreation) {
    Density d{1.0f};
    EXPECT_EQ(d.value, 1.0f);
    EXPECT_STREQ(Density::Name, "density");
}

TEST(Components, Vec3Creation) {
    Color c{1.0f, 0.5f, 0.2f};
    EXPECT_EQ(c.r, 1.0f);
    EXPECT_EQ(c.g, 0.5f);
    EXPECT_EQ(c.b, 0.2f);

    glm::vec3 v = c.toVec3();
    EXPECT_EQ(v.r, 1.0f);
}

TEST(Components, GlmConversion) {
    glm::vec3 input(1, 0.5, 0.2);
    Color c(input);
    glm::vec3 output = c;  // implicit conversion
    EXPECT_EQ(input, output);
}

TEST(Components, TypeDetection) {
    static_assert(ScalarComponent<Density>);
    static_assert(Vec3Component<Color>);
    static_assert(!Vec3Component<Density>);
    static_assert(!ScalarComponent<Color>);
}
```

---

## PHASE 2: Entity Creation (2-3 days)

### 2.1 Implement ComponentRegistry

**File:** `libraries/GaiaVoxelWorld/src/ComponentRegistry.cpp`

```cpp
class ComponentRegistry {
public:
    explicit ComponentRegistry(gaia::ecs::World& world) : m_world(world) {
        // Register all components (Gaia auto-registers on first use)
        // Just build name→ID mapping
        registerName<MortonKey>();
        registerName<Density>();
        registerName<Material>();
        registerName<Color>();
        registerName<Normal>();
        registerName<Emission>();
    }

    // Get Gaia component ID by type
    template<VoxelComponent T>
    uint32_t getComponentID() const {
        return gaia::ecs::Component<T>::id(m_world);
    }

    // Get ID by name (for DynamicVoxelScalar conversion)
    uint32_t getComponentID(std::string_view name) const {
        auto it = m_nameToID.find(name);
        return (it != m_nameToID.end()) ? it->second : 0;
    }

private:
    gaia::ecs::World& m_world;
    std::unordered_map<std::string_view, uint32_t> m_nameToID;

    template<VoxelComponent T>
    void registerName() {
        m_nameToID[ComponentTraits<T>::Name] = gaia::ecs::Component<T>::id(m_world);
    }
};
```

---

### 2.2 Update GaiaVoxelWorld::createVoxel()

**File:** `libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp`

```cpp
GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(
    const glm::vec3& position,
    const VoxelData::DynamicVoxelScalar& data) {

    auto& world = m_impl->world;
    auto entity = world.add();

    // Add MortonKey
    world.add<MortonKey>(entity, MortonKey::fromPosition(position));

    // Convert DynamicVoxelScalar → Components
    for (const auto& attr : data) {
        const char* name = attr.name.c_str();

        // Check for base vec3 names first
        if (attr.name == "color" && attr.getType() == VoxelData::AttributeType::Vec3) {
            glm::vec3 v = attr.get<glm::vec3>();
            world.add<Color>(entity, Color(v));
        } else if (attr.name == "normal" && attr.getType() == VoxelData::AttributeType::Vec3) {
            glm::vec3 v = attr.get<glm::vec3>();
            world.add<Normal>(entity, Normal(v));
        } else if (attr.name == "emission" && attr.getType() == VoxelData::AttributeType::Vec3) {
            glm::vec3 v = attr.get<glm::vec3>();
            world.add<Emission>(entity, Emission(v));
        } else if (attr.name == "density") {
            world.add<Density>(entity, Density{attr.get<float>()});
        } else if (attr.name == "material") {
            world.add<Material>(entity, Material{attr.get<uint32_t>()});
        }
        // ... handle other components
    }

    // Apply solidity predicate
    if (data.passesKeyPredicate()) {
        world.add<Solid>(entity);
    }

    return entity;
}
```

**Simplified version (fixed component set):**
```cpp
GaiaVoxelWorld::EntityID GaiaVoxelWorld::createVoxel(
    const glm::vec3& position,
    float density,
    const glm::vec3& color,
    const glm::vec3& normal) {

    auto& world = m_impl->world;
    auto entity = world.add();

    world.add<MortonKey>(entity, MortonKey::fromPosition(position));
    world.add<Density>(entity, Density{density});
    world.add<Color>(entity, Color(color));
    world.add<Normal>(entity, Normal(normal));

    if (density > 0.5f) {  // Example predicate
        world.add<Solid>(entity);
    }

    return entity;
}
```

---

### 2.3 Batch Creation with Prefabs

```cpp
std::vector<EntityID> GaiaVoxelWorld::createVoxelsBatch(
    const std::vector<VoxelCreationEntry>& entries) {

    if (entries.empty()) return {};

    auto& world = m_impl->world;

    // Create prefab with all common components
    auto prefab = world.prefab();
    prefab.add<MortonKey>();
    prefab.add<Density>();
    prefab.add<Color>();
    prefab.add<Normal>();

    // Batch create via prefab cloning (single archetype)
    std::vector<EntityID> entities;
    entities.reserve(entries.size());

    for (const auto& entry : entries) {
        auto entity = world.copy(prefab);

        // Set component values
        world.set<MortonKey>(entity, MortonKey::fromPosition(entry.position));
        world.set<Density>(entity, Density{entry.request.density});
        world.set<Color>(entity, Color(entry.request.color));
        world.set<Normal>(entity, Normal(entry.request.normal));

        // Add Solid tag if needed
        if (entry.request.density > 0.5f) {
            world.add<Solid>(entity);
        }

        entities.push_back(entity);
    }

    return entities;
}
```

---

## PHASE 3: Update SVO Integration (2-3 days)

### 3.1 Update VoxelInjector to Use New Components

**File:** `libraries/SVO/src/VoxelInjection.cpp`

**OLD (split components):**
```cpp
float r = world.get<Color_R>(entity).value;
float g = world.get<Color_G>(entity).value;
float b = world.get<Color_B>(entity).value;
glm::vec3 color(r, g, b);
```

**NEW (multi-member):**
```cpp
glm::vec3 color = world.get<Color>(entity);  // Automatic conversion
```

**Update all component access sites** - 3x fewer lookups!

---

### 3.2 EntityBrickView with New Components

**File:** `libraries/GaiaVoxelWorld/src/EntityBrickView.cpp`

```cpp
class EntityBrickView {
public:
    EntityBrickView(gaia::ecs::World& world,
                    std::span<gaia::ecs::Entity, 512> entities)
        : m_world(world), m_entities(entities) {}

    // Type-safe access (compile-time)
    template<Vec3Component T>
    glm::vec3 getVec3(size_t voxelIdx) const {
        return m_world.get<T>(m_entities[voxelIdx]);
    }

    template<ScalarComponent T>
    auto getScalar(size_t voxelIdx) const {
        return m_world.get<T>(m_entities[voxelIdx]).value;
    }

    // Iterate with multiple components
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

// Usage:
EntityBrickView brick(world, entities);

// Get single component
glm::vec3 color = brick.getVec3<Color>(localIdx);
float density = brick.getScalar<Density>(localIdx);

// Iterate multiple components
brick.iterate<Density, Color>([](size_t idx, Density d, Color c) {
    // Process voxel
});
```

---

## TESTING CHECKLIST

### Phase 1
- [ ] VoxelComponents.h compiles without errors
- [ ] All ComponentTraits registered
- [ ] Concepts detect types correctly (static_assert tests)
- [ ] glm::vec3 conversion works

### Phase 2
- [ ] Single voxel creation works
- [ ] Batch creation via prefabs works
- [ ] Solid tag correctly applied
- [ ] DynamicVoxelScalar → Entity conversion works

### Phase 3
- [ ] VoxelInjector compiles with new components
- [ ] EntityBrickView iteration works
- [ ] Ray casting returns correct entity refs
- [ ] Memory usage reduced (verify with profiler)

---

## ROLLBACK PLAN

If issues arise:

1. **Revert component header:**
   ```bash
   git restore libraries/GaiaVoxelWorld/include/VoxelComponents.h
   ```

2. **Keep VoxelComponents_v2.h** for future attempts

3. **Document blockers** in GitHub issues

---

## SUCCESS CRITERIA

### Performance
- ✅ 3x fewer component accesses (Color vs Color_R/G/B)
- ✅ Batch entity creation via prefabs (5-10x faster)
- ✅ Zero string lookups in hot paths

### Code Quality
- ✅ Simplified component definitions (2 macros)
- ✅ Type detection via concepts (no bools)
- ✅ ComponentTraits = 1 field (Name only)

### Gaia Integration
- ✅ Uses GAIA_LAYOUT directly
- ✅ Natural multi-member components
- ✅ Tag-based filtering (Solid)

---

## ESTIMATED TIMELINE

- **Phase 1:** 1 day (finalize components, update includes)
- **Phase 2:** 2 days (entity creation, batch API)
- **Phase 3:** 2 days (SVO integration, EntityBrickView)

**Total:** 5 days

**Risk:** Low (isolated changes, rollback plan ready)
