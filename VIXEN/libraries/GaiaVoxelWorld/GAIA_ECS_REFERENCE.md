# Gaia ECS API Reference

**Source**: https://github.com/richardbiely/gaia-ecs

## Entity Validity Checking

### Correct API ✅
```cpp
gaia::ecs::World w;
gaia::ecs::Entity e = w.add();

// Check if entity is valid
bool isValid = w.valid(e);  // ✅ CORRECT

w.del(e);
isValid = w.valid(e);  // returns false after deletion
```

### WRONG Usage ❌
```cpp
bool isValid = e.valid();  // ❌ COMPILE ERROR - Entity has no .valid() method
```

## Key Points

1. **Entity validity** is checked via `World::valid(entity)`, NOT a method on Entity itself
2. Once deleted via `w.del(e)`, the entity is no longer valid
3. Using invalid entities with APIs triggers debug-mode asserts

## GaiaVoxelWorld Wrapper

GaiaVoxelWorld provides its own wrapper for entity validation:

```cpp
GaiaVoxelWorld world;
auto entity = world.createVoxel(pos, density, color, normal);

// Use GaiaVoxelWorld API
bool exists = world.exists(entity);  // ✅ CORRECT (wraps world.valid())
```

## Common Entity Operations

### Create Entity
```cpp
ecs::Entity e = w.add();
```

### Delete Entity
```cpp
w.del(e);
```

### Add Component
```cpp
w.add<Position>(e, 1.0f, 2.0f, 3.0f);
w.add<Density>(e, 1.0f);
```

### Check Component Exists
```cpp
bool has = w.has<Position>(e);
```

### Get Component
```cpp
const Position& pos = w.get<Position>(e);
```

### Set Component (mutable access)
```cpp
Position& pos = w.get<Position>(e);
pos.x = 5.0f;

// Or use set:
w.set<Position>(e) = Position{5.0f, 6.0f, 7.0f};
```

## Test Fixes Required

Replace all occurrences of:
```cpp
 world.exists(entity)  // ❌ WRONG
```

With:
```cpp
world.exists(entity)  // ✅ CORRECT (GaiaVoxelWorld API)
// or
world.getWorld().valid(entity)  // ✅ CORRECT (direct Gaia API)
```

## Reference Documentation

- Official Repo: https://github.com/richardbiely/gaia-ecs
- Usage Guide: https://github.com/richardbiely/gaia-ecs?tab=readme-ov-file#usage
- Wiki: https://github.com/richardbiely/gaia-ecs/wiki
