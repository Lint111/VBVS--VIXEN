# Test Compilation Fixes Needed

Based on build errors from `temp/build-errors.txt`, the following issues need fixing:

## 1. Missing STL Includes

**Files affected:** All test files

**Add to each test file:**
```cpp
#include <algorithm>    // For std::sort
#include <unordered_set> // For std::unordered_set
```

## 2. Gaia Entity API ✅ CONFIRMED FIX

**Issue:** ` world.exists(entity)` method not found

**Root cause:** Gaia ECS Entity does NOT have a `.valid()` method. Validity is checked via `World::valid(entity)`.

**From Official Gaia ECS Docs:**
> "Verifying that an entity is valid can be done by calling **World::valid**."

**Correct API:**
```cpp
gaia::ecs::World w;
gaia::ecs::Entity e = w.add();
bool isValid = w.valid(e);  // ✅ CORRECT
```

**Wrong API (used in tests):**
```cpp
bool isValid =  world.exists(entity);  // ❌ COMPILE ERROR
```

**Fix for tests:** Use GaiaVoxelWorld wrapper:
```cpp
// Replace:
EXPECT_TRUE( world.exists(entity));  // ❌ WRONG

// With:
EXPECT_TRUE(world.exists(entity));  // ✅ CORRECT
```

**Reference:** See [GAIA_ECS_REFERENCE.md](GAIA_ECS_REFERENCE.md) for full API documentation

## 3. DynamicVoxelScalar Namespace Issue

**Error:**
```
'DynamicVoxelScalar': is not a member of '`global namespace''
'VoxelData': the symbol to the left of a '::' must be a type
```

**Current code:**
```cpp
std::vector<std::pair<glm::vec3, ::VoxelData::DynamicVoxelScalar>> batch;
```

**Fix:** Add proper include and namespace:
```cpp
#include "DynamicVoxelStruct.h"  // Already added
// Use fully qualified:
std::vector<std::pair<glm::vec3, VoxelData::DynamicVoxelScalar>> batch;
```

## 4. GoogleTest Assertion Issues

**Error:**
```
'testing::AssertionResult': no appropriate default constructor available
'gtest_ar_': const object must be initialized
```

**Cause:** `EXPECT_TRUE( world.exists(entity))` fails when ` world.exists(entity)` doesn't compile

**Fix:** Use `EXPECT_TRUE(world.exists(entity))` instead (from GaiaVoxelWorld API)

## 5. Test File Fixes Summary

### test_gaia_voxel_world.cpp
- ✅ Add `<algorithm>` - already fixed
- Replace all ` world.exists(entity)` with `world.exists(entity)`
- Fix namespace: `::VoxelData::` → `VoxelData::`

### test_voxel_injection_queue.cpp
- Add `<algorithm>`
- Replace all ` world.exists(entity)` with `world.exists(entity)`

### test_voxel_injector.cpp
- Add `<algorithm>`
- Replace all ` world.exists(entity)` with `world.exists(entity)` (in MockSVO if needed)

### test_entity_brick_view.cpp
- Add `<algorithm>`, `<unordered_set>`
- Replace all ` world.exists(entity)` with `world.exists(entity)`

## 6. Recommended Approach

**Create test helper macros:**
```cpp
// In test files:
#define ASSERT_ENTITY_VALID(world, entity) \
    ASSERT_TRUE((world).exists(entity))

#define EXPECT_ENTITY_VALID(world, entity) \
    EXPECT_TRUE((world).exists(entity))

#define EXPECT_ENTITY_INVALID(world, entity) \
    EXPECT_FALSE((world).exists(entity))
```

Then use:
```cpp
EXPECT_ENTITY_VALID(world, entity);  // Instead of EXPECT_TRUE( world.exists(entity))
```

## 7. PDB Lock Workaround

**Still blocked by:** MSVC PDB lock (see BUILD_WORKAROUND.md)

**Once fixed**, apply test fixes above and rebuild.
