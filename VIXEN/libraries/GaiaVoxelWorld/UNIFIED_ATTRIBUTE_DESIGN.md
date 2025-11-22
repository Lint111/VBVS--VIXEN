# Unified Attribute System Design

**Goal**: Merge AttributeRegistry with Gaia ECS to eliminate duplicate attribute declarations while preserving existing API compatibility.

---

## Current Architecture (Problem)

### Duplicate Declarations

**VoxelData (AttributeRegistry):**
```cpp
registry->registerKey("density", AttributeType::Float, 0.0f);
registry->addAttribute("color_r", AttributeType::Float, 1.0f);
registry->addAttribute("color_g", AttributeType::Float, 1.0f);
registry->addAttribute("color_b", AttributeType::Float, 1.0f);
```

**GaiaVoxel (ECS Components):**
```cpp
struct Density { float value = 1.0f; };
struct Color_R { float value = 1.0f; };
struct Color_G { float value = 1.0f; };
struct Color_B { float value = 1.0f; };
```

**Issue**: Same data structure declared twice with different syntax. Any schema change requires updates in both systems.

---

## Unified Architecture (Solution)

### Single Source of Truth: Gaia ECS Components

**Key Principle**: Gaia ECS components are the canonical attribute schema. AttributeRegistry becomes a **facade** that exposes ECS data through the existing VoxelData API.

---

## Design: ECS-Backed AttributeRegistry

### 1. Component Registration Bridge

**Problem**: AttributeRegistry uses string-based attribute registration. Gaia ECS uses compile-time component types.

**Solution**: Template-based component registration with automatic ECS mapping.

```cpp
// New API: Register ECS component as attribute
template<typename TComponent>
AttributeIndex registerComponent(const std::string& name, bool isKey = false) {
    // Get Gaia component ID
    auto componentID = gaia::ecs::Component<TComponent>::id();

    // Determine AttributeType from TComponent
    AttributeType type = TypeToAttributeType<TComponent>::value;

    // Create descriptor
    AttributeDescriptor desc(name, type, TComponent{}, m_nextAttributeIndex++, isKey);

    // Map string name → ECS component ID
    m_nameToComponentID[name] = componentID;
    m_componentIDToName[componentID] = name;

    // Store descriptor
    m_descriptorByIndex.push_back(desc);

    return desc.index;
}
```

**Usage**:
```cpp
// Old AttributeRegistry API (still works via DynamicVoxelScalar)
registry->registerKey("density", AttributeType::Float, 0.0f);

// New unified API (preferred)
registry->registerComponent<GaiaVoxel::Density>("density", true);
registry->registerComponent<GaiaVoxel::Color_R>("color_r");
registry->registerComponent<GaiaVoxel::Color_G>("color_g");
registry->registerComponent<GaiaVoxel::Color_B>("color_b");
```

---

### 2. BrickView → Entity Mapping

**Problem**: BrickView expects 512 contiguous voxels in SoA arrays. Gaia ECS stores sparse entities.

**Solution**: BrickAllocation tracks entity IDs instead of slot indices.

```cpp
struct BrickAllocation {
    uint32_t brickID;
    std::array<gaia::ecs::Entity, 512> entities;  // 8³ voxels

    // OLD: std::unordered_map<std::string, uint32_t> attributeSlots;
    // NEW: Entities own their components, no separate slots needed
};
```

**BrickView API Changes**:
```cpp
// OLD: Direct memory access to AttributeStorage slots
float density = brick.get<float>("density", voxelIdx);

// NEW: Entity component access
gaia::ecs::Entity entity = brick.getEntity(voxelIdx);
if (entity.valid() && entity.has<Density>()) {
    float density = entity.get<Density>().value;
}
```

**API Compatibility**: Add wrapper methods to preserve existing interface:
```cpp
template<typename T>
T BrickView::get(const std::string& name, size_t index) const {
    auto entity = getEntity(index);
    if (!entity.valid()) return T{};

    auto componentID = m_registry->getComponentID(name);
    return getComponentValue<T>(entity, componentID);
}
```

---

### 3. AttributeStorage Elimination

**Current**: AttributeStorage maintains separate SoA arrays (density[], color_r[], etc.)

**New**: Gaia ECS **IS** the storage. AttributeStorage becomes a thin query wrapper.

```cpp
class AttributeStorage {
public:
    // OLD: std::vector<float> m_data;
    // NEW: Query Gaia ECS components

    template<typename T>
    std::vector<T> getAllValues(gaia::ecs::World& world) const {
        std::vector<T> results;
        auto query = world.query().all<TComponent>();
        query.each([&](gaia::ecs::Entity entity) {
            results.push_back(entity.get<TComponent>().value);
        });
        return results;
    }
};
```

**Impact**: Zero-copy access. Queries iterate ECS archetypes directly.

---

### 4. DynamicVoxelScalar → Entity Conversion

**Problem**: VoxelInjectionQueue stores `DynamicVoxelScalar` copies (64+ bytes).

**Solution**: Convert `DynamicVoxelScalar` to entity ID on enqueue (16 bytes).

```cpp
class GaiaBackedRegistry : public AttributeRegistry {
public:
    // Create entity from DynamicVoxelScalar
    gaia::ecs::Entity createEntityFromDynamic(const DynamicVoxelScalar& voxel) {
        auto entity = m_world.add();

        // Add MortonKey component
        glm::vec3 pos = getPositionFromVoxel(voxel);
        entity.add<MortonKey>(MortonKey::fromPosition(pos));

        // Map each attribute to ECS component
        for (const auto& [name, value] : voxel.getValues()) {
            addComponentFromAttribute(entity, name, value);
        }

        return entity;
    }

private:
    void addComponentFromAttribute(gaia::ecs::Entity entity,
                                   const std::string& name,
                                   const std::any& value) {
        auto componentID = m_nameToComponentID[name];

        if (name == "density") {
            entity.add<Density>(Density{std::any_cast<float>(value)});
        } else if (name == "color_r") {
            entity.add<Color_R>(Color_R{std::any_cast<float>(value)});
        }
        // ... etc for all components
    }
};
```

---

## Implementation Strategy

### Phase 1: Registry-Component Bridge (2 hours)

**Files to Create:**
- `libraries/GaiaVoxelWorld/include/ECSBackedRegistry.h`
- `libraries/GaiaVoxelWorld/src/ECSBackedRegistry.cpp`

**API:**
```cpp
class ECSBackedRegistry : public AttributeRegistry {
public:
    ECSBackedRegistry(gaia::ecs::World& world);

    // Template registration (NEW)
    template<typename TComponent>
    AttributeIndex registerComponent(const std::string& name, bool isKey);

    // Create entity from DynamicVoxelScalar
    gaia::ecs::Entity createEntity(const DynamicVoxelScalar& voxel);

    // Query entities → DynamicVoxelScalar
    DynamicVoxelScalar getVoxelFromEntity(gaia::ecs::Entity entity) const;

private:
    gaia::ecs::World& m_world;
    std::unordered_map<std::string, uint32_t> m_nameToComponentID;
    std::unordered_map<uint32_t, std::string> m_componentIDToName;
};
```

**Integration Point:**
```cpp
// VoxelInjectionQueue.cpp
void VoxelInjectionQueue::enqueue(const glm::vec3& pos, const DynamicVoxelScalar& voxel) {
    // OLD: Store copy of voxel (64+ bytes)
    // m_ringBuffer[writeIdx] = {pos, voxel};

    // NEW: Convert to entity, store ID only (16 bytes)
    auto entity = m_registry->createEntity(voxel);
    m_ringBuffer[writeIdx] = {MortonKey::fromPosition(pos), entity.id()};
}
```

---

### Phase 2: BrickView Entity Access (3 hours)

**Modify BrickView:**
```cpp
class BrickView {
public:
    // NEW: Entity-based access
    gaia::ecs::Entity getEntity(size_t voxelIdx) const;

    template<typename TComponent>
    typename TComponent::ValueType getComponent(size_t voxelIdx) const {
        auto entity = getEntity(voxelIdx);
        if (!entity.valid() || !entity.has<TComponent>()) {
            return TComponent{}.value;
        }
        return entity.get<TComponent>().value;
    }

    // OLD API: Compatibility wrapper
    template<typename T>
    T get(const std::string& name, size_t index) const {
        // Delegate to getComponent<T>
        auto componentID = m_registry->getComponentID(name);
        return getComponentByID<T>(index, componentID);
    }

private:
    std::array<gaia::ecs::Entity, 512> m_entities;  // NEW
    const ECSBackedRegistry* m_registry;
};
```

**BrickAllocation Changes:**
```cpp
struct BrickAllocation {
    uint32_t brickID;

    // OLD: Per-attribute slot indices
    // std::unordered_map<std::string, uint32_t> attributeSlots;

    // NEW: Entity array (512 entities, one per voxel)
    std::vector<gaia::ecs::Entity> entities;
};
```

---

### Phase 3: VoxelInjector Integration (2 hours)

**Update VoxelInjector:**
```cpp
void VoxelInjector::processBatch(const std::vector<MortonKey>& keys,
                                const std::vector<gaia::ecs::EntityID>& entityIDs) {
    // Group by brick coordinate
    std::unordered_map<BrickCoord, std::vector<size_t>> brickGroups;
    for (size_t i = 0; i < keys.size(); i++) {
        glm::ivec3 brickCoord = keys[i].toGridPos() / 8;
        brickGroups[brickCoord].push_back(i);
    }

    // Process each brick
    for (const auto& [brickCoord, indices] : brickGroups) {
        // Get or create brick
        uint32_t brickID = getOrCreateBrick(brickCoord);
        BrickView brick = m_registry->getBrick(brickID);

        // Fill brick from entities
        for (size_t i : indices) {
            glm::ivec3 gridPos = keys[i].toGridPos();
            glm::ivec3 localPos = gridPos % 8;

            // Store entity reference in brick
            brick.setEntity(localPos.x, localPos.y, localPos.z, entityIDs[i]);
        }
    }
}
```

---

### Phase 4: Memory Optimization Validation (1 hour)

**Measure memory savings:**

**OLD Queue Entry:**
```cpp
struct QueueEntry {
    glm::vec3 position;              // 12 bytes
    DynamicVoxelScalar voxel;        // 52+ bytes (map overhead + attributes)
};
// Total: 64+ bytes
```

**NEW Queue Entry:**
```cpp
struct QueueEntry {
    MortonKey key;                   // 8 bytes
    gaia::ecs::EntityID entityID;    // 8 bytes
};
// Total: 16 bytes
```

**Savings**: 75% reduction (64 → 16 bytes per voxel)

**OLD Brick Storage:**
```cpp
// Per-attribute SoA arrays (512 voxels × 4 attributes × 4 bytes)
Density:  512 × 4 = 2048 bytes
Color_R:  512 × 4 = 2048 bytes
Color_G:  512 × 4 = 2048 bytes
Color_B:  512 × 4 = 2048 bytes
// Total: 8192 bytes/brick
```

**NEW Brick Storage:**
```cpp
// Entity references (512 × 8 bytes) + Gaia ECS overhead
std::vector<gaia::ecs::Entity> entities;  // 4096 bytes
// Actual component data lives in Gaia archetypes (sparse SoA)
// Only occupied voxels allocate components
// Total: ~4096 bytes + (occupied_count × 16 bytes)
```

**Sparse savings** (10% occupancy):
- OLD: 8192 bytes (all 512 voxels)
- NEW: 4096 + (51 × 16) = 4096 + 816 = 4912 bytes
- **40% reduction**

---

## Attribute Type Mapping

### C++ Type → Gaia Component

```cpp
template<typename T> struct TypeToAttributeType;

// Scalar types
template<> struct TypeToAttributeType<Density> {
    static constexpr AttributeType value = AttributeType::Float;
    using ValueType = float;
};

template<> struct TypeToAttributeType<Material> {
    static constexpr AttributeType value = AttributeType::Uint32;
    using ValueType = uint32_t;
};

// Vec3 components (split into 3 floats)
template<> struct TypeToAttributeType<Color_R> {
    static constexpr AttributeType value = AttributeType::Float;
    static constexpr bool isVec3Component = true;
    static constexpr size_t componentIndex = 0;  // R
};
```

### AttributeType → Component Access

```cpp
std::any getComponentValue(gaia::ecs::Entity entity,
                          const std::string& name,
                          AttributeType type) {
    switch (type) {
        case AttributeType::Float: {
            if (name == "density") return entity.get<Density>().value;
            if (name == "color_r") return entity.get<Color_R>().value;
            // ... etc
        }
        case AttributeType::Uint32: {
            if (name == "material") return entity.get<Material>().id;
        }
        case AttributeType::Vec3: {
            // Reconstruct vec3 from split components
            if (name == "color") {
                return glm::vec3(
                    entity.get<Color_R>().value,
                    entity.get<Color_G>().value,
                    entity.get<Color_B>().value
                );
            }
        }
    }
}
```

---

## Component Schema Registration

### Auto-Registration Pattern

```cpp
// Define attribute schema once
struct VoxelSchema {
    static void registerAll(ECSBackedRegistry& registry) {
        // Key attribute
        registry.registerComponent<Density>("density", true);

        // Color (split vec3)
        registry.registerComponent<Color_R>("color_r");
        registry.registerComponent<Color_G>("color_g");
        registry.registerComponent<Color_B>("color_b");

        // Normal (split vec3)
        registry.registerComponent<Normal_X>("normal_x");
        registry.registerComponent<Normal_Y>("normal_y");
        registry.registerComponent<Normal_Z>("normal_z");

        // Material
        registry.registerComponent<Material>("material");
    }
};
```

**Usage**:
```cpp
// Application startup
gaia::ecs::World world;
ECSBackedRegistry registry(world);
VoxelSchema::registerAll(registry);

// Now use registry normally
VoxelInjector injector(&registry);
```

---

## Migration Path

### Backward Compatibility

**Existing Code**:
```cpp
registry->registerKey("density", AttributeType::Float, 0.0f);
registry->addAttribute("color", AttributeType::Vec3, glm::vec3(1.0f));

BrickView brick = registry->getBrick(brickID);
float density = brick.get<float>("density", 42);
```

**Still Works**: `ECSBackedRegistry` implements full `AttributeRegistry` interface.

**New Code**:
```cpp
registry->registerComponent<Density>("density", true);
registry->registerComponent<Color_R>("color_r");

BrickView brick = registry->getBrick(brickID);
auto entity = brick.getEntity(42);
float density = entity.get<Density>().value;
```

**Migration Timeline**:
1. Week 1: Implement `ECSBackedRegistry` with compatibility wrappers
2. Week 2: Update VoxelInjectionQueue to use entity IDs
3. Week 3: Update BrickView to use entity storage
4. Week 4: Deprecate old AttributeStorage (keep for legacy tests)

---

## Files to Create/Modify

### New Files
1. `libraries/GaiaVoxelWorld/include/ECSBackedRegistry.h` (250 lines)
2. `libraries/GaiaVoxelWorld/src/ECSBackedRegistry.cpp` (400 lines)
3. `libraries/GaiaVoxelWorld/include/ComponentTypeTraits.h` (150 lines)
4. `libraries/GaiaVoxelWorld/src/VoxelSchema.cpp` (80 lines)

### Modified Files
1. `libraries/VoxelData/include/BrickView.h` - Add entity access API
2. `libraries/VoxelData/src/BrickView.cpp` - Entity storage backend
3. `libraries/SVO/src/VoxelInjectionQueue.cpp` - Entity-based queue
4. `libraries/SVO/src/VoxelInjection.cpp` - Entity-based batch processing

### Total LOC
- New: ~880 lines
- Modified: ~600 lines
- **Total effort: 7-8 hours**

---

## Benefits Summary

### 1. Single Source of Truth
- ✅ Attribute schema defined once (Gaia components)
- ✅ No duplicate declarations
- ✅ Schema changes propagate automatically

### 2. Memory Efficiency
- ✅ 75% reduction in queue entry size (64 → 16 bytes)
- ✅ 40% reduction in brick storage (sparse occupancy)
- ✅ Zero-copy access via entity references

### 3. Performance
- ✅ Lock-free parallel access (Gaia ECS archetypes)
- ✅ SIMD-friendly SoA layout (Gaia native)
- ✅ Cache-coherent iteration (component arrays)

### 4. Flexibility
- ✅ Runtime attribute addition/removal (still supported)
- ✅ Custom key predicates (still supported)
- ✅ Observer pattern (still supported)

### 5. API Compatibility
- ✅ Existing VoxelData API preserved
- ✅ Gradual migration path
- ✅ Zero breaking changes

---

## Next Steps

**Session Plan**:
1. ✅ Design document complete (this file)
2. Implement `ECSBackedRegistry` (2 hours)
3. Update `BrickView` entity storage (2 hours)
4. Integrate with `VoxelInjectionQueue` (1.5 hours)
5. Write integration tests (1.5 hours)
6. Measure memory/performance gains (1 hour)

**Total: 8 hours** → Ready for production use.
