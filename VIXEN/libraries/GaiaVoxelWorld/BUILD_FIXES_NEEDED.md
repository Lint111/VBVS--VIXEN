# GaiaVoxelWorld Build Fixes Needed

**Status**: Architecture complete, Gaia API integration needs correction.

---

## Remaining Compilation Errors

### Issue: Incorrect Gaia ECS API Usage

**Problem**: Used Entity methods (`.get()`, `.has()`) that don't exist in Gaia ECS.

**Affected Files**:
- `libraries/GaiaVoxelWorld/src/ECSBackedRegistry.cpp` - ~100+ errors
- `libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp` - Fixed ✅
- `libraries/GaiaVoxelWorld/src/EntityBrickView.cpp` - Fixed ✅

**Root Cause**: Assumed Entity has component accessors like Unity ECS, but Gaia uses World-based access.

---

## Gaia ECS API Reference

Based on Gaia documentation, correct patterns:

### Entity Creation
```cpp
// Correct
gaia::ecs::World world;
auto entity = world.add();

// Add components
world.add<Density>(entity, {1.0f});
world.add<Color_R>(entity, {1.0f});
```

### Component Access
```cpp
// WRONG (what we used)
if (entity.has<Density>()) {
    auto density = entity.get<Density>();
}

// CORRECT (Gaia API)
if (world.has<Density>(entity)) {
    auto& density = world.get<Density>(entity);
}
```

### Component Queries
```cpp
// Correct pattern for iteration
auto query = world.query().all<Density>();
query.each([&](gaia::ecs::Entity entity, const Density& density) {
    // Process density component
    if (density.value > 0.0f) {
        // ...
    }
});
```

---

## Fix Strategy

### Option 1: Minimal Fix (Recommended - 1-2 hours)
**Scope**: Fix ECSBackedRegistry.cpp Gaia API calls

1. Replace all `entity.get<T>()` with `m_world.get<T>(entity)`
2. Replace all `entity.has<T>()` with `m_world.has<T>(entity)`
3. Update entity creation calls to use `m_world.add()`
4. Test compilation

**Files to modify**:
- [ECSBackedRegistry.cpp](libraries/GaiaVoxelWorld/src/ECSBackedRegistry.cpp)

**Estimated lines**: ~50 lines need API correction

### Option 2: Defer ECSBackedRegistry (Fastest - 30 min)
**Scope**: Comment out ECSBackedRegistry, keep VoxelInjectionQueue/VoxelInjector/EntityBrickView

1. Remove `src/ECSBackedRegistry.cpp` from CMakeLists.txt
2. Add placeholder `#if 0` wrapper in ECSBackedRegistry.h
3. Test that VoxelInjectionQueue/VoxelInjector/EntityBrickView compile

**Rationale**: ECSBackedRegistry is optional (bridges old AttributeRegistry API). Core migration (Queue/Injector/BrickView) can work without it.

### Option 3: Full Gaia Integration (8-16 hours)
**Scope**: Complete Gaia ECS integration across all files

1. Study Gaia examples in `build/_deps/gaia-src/src/examples/`
2. Refactor all Entity API usage
3. Update GaiaVoxelWorld.cpp Impl to use Gaia correctly
4. Write integration tests
5. Validate performance

---

## Current Build Status

### ✅ Files Compiling (No Errors)
- `libraries/GaiaVoxelWorld/src/VoxelComponents.cpp` ✅
- `libraries/GaiaVoxelWorld/src/VoxelInjectionQueue.cpp` ✅
- `libraries/GaiaVoxelWorld/src/VoxelInjector.cpp` ✅
- `libraries/GaiaVoxelWorld/src/EntityBrickView.cpp` ✅

### 🔴 Files Failing (API Errors)
- `libraries/GaiaVoxelWorld/src/ECSBackedRegistry.cpp` - 100+ Gaia API errors
- `libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp` - Fixed in getStats(), but createVoxel() likely has same issues

---

## Recommendation

**Proceed with Option 2** (defer ECSBackedRegistry):
1. Fastest path to validate Phase 1-2 architecture
2. VoxelInjectionQueue/VoxelInjector are core to migration
3. ECSBackedRegistry was Session 2 work (bridges old API) - not critical
4. Can fix Gaia API issues in future session with examples

**Estimated time**: 30 minutes to working build
**Result**: Phases 1A-2 validated, Phase 3 (LaineKarrasOctree) can proceed

---

## Example Fix (for reference)

**BEFORE** (ECSBackedRegistry.cpp:268):
```cpp
if (entity.has<Color_R>()) {
    voxel.set("color_r", entity.get<Color_R>().value);
}
```

**AFTER**:
```cpp
if (m_world.has<Color_R>(entity)) {
    auto& color_r = m_world.get<Color_R>(entity);
    voxel.set("color_r", color_r.value);
}
```

---

## Next Session Actions

1. Choose fix strategy (Option 1 or 2)
2. Fix/defer ECSBackedRegistry
3. Test GaiaVoxelWorld library build
4. Proceed to Phase 3 (LaineKarrasOctree entity storage) or stop here
