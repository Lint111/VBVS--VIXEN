# Gaia ECS Usage Guide for GaiaVoxelWorld

This document explains how Gaia ECS is used within the GaiaVoxelWorld library to provide efficient, cache-friendly voxel data management.

---

## What is Gaia ECS?

Gaia-ECS is an archetype-based Entity Component System (ECS) written in C++17. It organizes data around **composition** rather than inheritance, enabling:
- **Cache-friendly iteration** - Components stored in contiguous SoA (Structure-of-Arrays) chunks
- **Lock-free parallelism** - Designed for multi-threaded access without manual synchronization
- **Minimal overhead** - Zero runtime type information, compile-time component registration

---

## Core Concepts

### World
The central ECS manager. One world instance manages all entities and components.

```cpp
gaia::ecs::World world;  // Create ECS world
```

### Entities
Lightweight IDs referencing component data. Entities themselves have no data—only references.

```cpp
auto entity = world.add();       // Create entity
bool valid =  world.exists(entity);     // Check validity
world.del(entity);                // Delete entity
```

### Components
Plain structs holding data. No inheritance, no virtual methods.

```cpp
struct Position { float x, y, z; };
struct Density { float value; };

entity.add<Position>(0.0f, 0.0f, 0.0f);
entity.add<Density>(1.0f);
```

**Constraints:**
- Max **32 components** per entity
- Max **4095 bytes** per component
- Must be **default-constructible**

---

## Basic Usage

### Entity Creation and Component Assignment

```cpp
// Create voxel entity
auto voxel = world.add();
voxel.add<Position>(1.0f, 2.0f, 3.0f);
voxel.add<Density>(1.0f);
voxel.add<Color>(1.0f, 0.0f, 0.0f);  // Red
```

### Component Access

```cpp
// Read component
auto pos = voxel.get<Position>();
std::cout << "Position: " << pos.x << ", " << pos.y << ", " << pos.z << "\n";

// Modify component
voxel.set<Density>(Density{0.5f});

// Check component existence
if (voxel.has<Color>()) {
    auto color = voxel.get<Color>();
}
```

### Bulk Operations (Builder Pattern)

Minimize archetype transitions by batching component operations:

```cpp
world.build(voxel)
    .add<Position>(0.0f, 0.0f, 0.0f)
    .add<Density>(1.0f)
    .add<Color>(1.0f, 0.0f, 0.0f)
    .add<Normal>(0.0f, 1.0f, 0.0f);
```

**Why?** Each `add()` or `del()` triggers archetype movement. Builders execute **one** transition instead of multiple.

---

## Query System

### Basic Queries

Iterate all entities with specific components:

```cpp
// Query all voxels with Position and Density
auto query = world.query().all<Position, Density>();
query.each([](gaia::ecs::Entity entity) {
    auto pos = entity.get<Position>();
    auto density = entity.get<Density>();

    // Process voxel
    std::cout << "Voxel at (" << pos.x << ", " << pos.y << ", " << pos.z
              << ") density=" << density.value << "\n";
});
```

### Conditional Queries

```cpp
// Query voxels that have Color OR Normal
auto query = world.query().any<Color, Normal>();

// Query voxels that DON'T have BrickReference
auto query = world.query().none<BrickReference>();
```

### Chunk-Based Iteration (High Performance)

Gaia stores components in **chunks** (8-16 KiB) for cache efficiency:

```cpp
auto query = world.query().all<Position, Density>();
query.each([](gaia::ecs::Iter& it) {
    // Iterate chunk (multiple entities)
    auto positions = it.view<Position>();  // Array view
    auto densities = it.view<Density>();

    for (size_t i = 0; i < it.size(); ++i) {
        // SIMD-friendly: Data is contiguous
        positions[i].x += 1.0f;
        densities[i].value *= 0.9f;
    }
});
```

**Performance:** Chunk iteration enables **SIMD vectorization** and superior cache locality.

---

## GaiaVoxelWorld Implementation Patterns

### Entity Creation (Zero-Copy)

```cpp
// GaiaVoxelWorld::createVoxel() implementation
auto entity = world.add();
entity.add<Position>(position);
entity.add<Density>(density);
entity.add<Color>(color);
entity.add<Normal>(normal);
entity.add<SpatialHash>(computeMortonCode(position));

return entity;  // Return lightweight entity ID
```

### Spatial Queries (Brick-Based)

```cpp
// Query all voxels in brick region
std::vector<gaia::ecs::Entity> GaiaVoxelWorld::queryBrick(
    const glm::ivec3& brickCoord,
    int brickResolution) {

    std::vector<gaia::ecs::Entity> results;
    glm::vec3 brickMin = glm::vec3(brickCoord) * float(brickResolution);
    glm::vec3 brickMax = brickMin + glm::vec3(brickResolution);

    auto query = world.query().all<Position>();
    query.each([&](gaia::ecs::Entity entity) {
        auto pos = entity.get<Position>().toVec3();
        if (pos.x >= brickMin.x && pos.x <= brickMax.x &&
            pos.y >= brickMin.y && pos.y <= brickMax.y &&
            pos.z >= brickMin.z && pos.z <= brickMax.z) {
            results.push_back(entity);
        }
    });

    return results;
}
```

### VoxelInjectionQueue Integration

**Old approach (copies data):**
```cpp
struct VoxelEntry {
    glm::vec3 position;
    DynamicVoxelScalar attributes;  // FULL COPY (~64+ bytes)
};
```

**New approach (references only):**
```cpp
struct VoxelEntry {
    glm::vec3 position;             // 12 bytes
    gaia::ecs::Entity entityID;     // 8 bytes
};

// Enqueue creates entity, stores ID
auto entity = gaiaWorld.createVoxel(pos, density, color, normal);
queue.enqueue(pos, entity);  // Just reference!

// Worker threads read components
auto query = gaiaWorld.queryBrick(brickCoord, 8);
for (auto entity : query) {
    auto pos = entity.get<Position>();
    auto density = entity.get<Density>();
    brick.setVoxel(pos, density);  // Direct read, no copy
}
```

---

## Performance Best Practices

### 1. Use Chunk Iteration for Bulk Processing

```cpp
// ❌ SLOW: Per-entity iteration
query.each([](gaia::ecs::Entity entity) {
    auto pos = entity.get<Position>();
    // Process one entity at a time
});

// ✅ FAST: Chunk iteration (SIMD-friendly)
query.each([](gaia::ecs::Iter& it) {
    auto positions = it.view<Position>();
    for (size_t i = 0; i < it.size(); ++i) {
        // Process contiguous array (vectorizable)
    }
});
```

### 2. Batch Component Modifications

```cpp
// ❌ SLOW: Multiple archetype transitions
entity.add<Position>(0, 0, 0);
entity.add<Density>(1.0f);
entity.add<Color>(1, 0, 0);

// ✅ FAST: Single transition
world.build(entity)
    .add<Position>(0, 0, 0)
    .add<Density>(1.0f)
    .add<Color>(1, 0, 0);
```

### 3. Avoid Large Components

Store **references/pointers** to large data instead of embedding it:

```cpp
// ❌ BAD: Embed large data in component
struct VoxelMesh {
    std::vector<Vertex> vertices;  // Violates 4KB limit!
};

// ✅ GOOD: Store reference
struct VoxelMesh {
    uint32_t meshID;  // Reference to external mesh storage
};
```

### 4. Silent Set for Non-Critical Updates

Use `sset()` instead of `set()` to skip change notifications:

```cpp
// Normal set (triggers observers, updates version)
entity.set<Position>(newPos);

// Silent set (no notifications, faster)
entity.sset<Position>(newPos);
```

---

## Thread Safety

Gaia ECS is **thread-safe by design**:
- Component arrays are **immutable** during queries
- **Copy-on-write** semantics for structural changes
- **Lock-free** reads during iteration

**Example: Parallel brick processing**
```cpp
// Multiple worker threads can safely read same entities
std::vector<std::thread> workers;
for (int i = 0; i < 8; ++i) {
    workers.emplace_back([&]() {
        auto query = gaiaWorld.queryBrick(brickCoord, 8);
        for (auto entity : query) {
            auto density = entity.get<Density>();  // Thread-safe read
            // Process voxel
        }
    });
}
```

---

## Comparison: AttributeRegistry vs GaiaVoxelWorld

| Aspect | AttributeRegistry (Old) | GaiaVoxelWorld (New) |
|--------|------------------------|---------------------|
| **Storage** | `std::vector<T>` per attribute | Gaia ECS chunks (SoA) |
| **Memory** | ~64+ bytes/voxel (queue copy) | ~16 bytes/voxel (ID reference) |
| **Access** | Hash map lookup | Direct array indexing |
| **Thread Safety** | Manual mutex locking | Lock-free by design |
| **Cache Efficiency** | Scattered memory | Contiguous chunks |
| **Spatial Queries** | Linear scan | Morton code + chunk filtering |

---

## Further Reading

- **Gaia ECS GitHub**: https://github.com/richardbiely/gaia-ecs
- **Documentation**: https://github.com/richardbiely/gaia-ecs/wiki
- **VoxelComponents.h**: Component definitions for voxel data
- **GaiaVoxelWorld.h**: High-level ECS wrapper API

---

## Summary

Gaia ECS provides **zero-copy, cache-friendly, thread-safe** voxel data management:
1. **Entities** are lightweight IDs (~8 bytes)
2. **Components** stored in contiguous SoA chunks
3. **Queries** enable efficient spatial filtering
4. **Thread-safe** parallel iteration without locks

This replaces AttributeRegistry's vector-based storage with a scalable, performant backend suitable for real-time voxel engines.
