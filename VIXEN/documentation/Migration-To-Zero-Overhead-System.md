# Migration to Zero-Overhead Type System

**Goal**: Replace current ResourceVariant system with compile-time-only ZeroOverheadResource while maintaining backward compatibility.

---

## Migration Strategy

### Phase 1: Add New System Alongside Old (Non-Breaking)

**Status**: ✅ Complete

Files added:
- `ZeroOverheadTypeSystem.h` - Core compile-time type system
- `AutoTypeDecomposition.h` - C++ type → compile-time tag mapping
- Tests for verification

**Impact**: Zero - old code unchanged

---

### Phase 2: Create Compatibility Bridge

**Action**: Create drop-in replacement header

**File**: `RenderGraph/include/Data/Core/ResourceV3.h`

```cpp
#pragma once

// Drop-in replacement for ResourceVariant.h
#include "ZeroOverheadTypeSystem.h"

namespace Vixen::RenderGraph {

// Alias for backward compatibility
using Resource = ZeroOverheadResource;

// ResourceSlot keeps same API, just uses new backend
template<typename T>
using ResourceSlot = TransparentResourceSlot<T>;

} // namespace Vixen::RenderGraph
```

---

### Phase 3: Update Include Paths (Incremental)

**Action**: Replace includes incrementally

```cpp
// Old
#include "Data/Core/ResourceVariant.h"

// New
#include "Data/Core/ResourceV3.h"
```

**Files to update** (29 files with ResourceVariant):
1. `RenderGraph/include/Data/Core/ResourceConfig.h`
2. `RenderGraph/include/Core/NodeInstance.h`
3. `RenderGraph/include/Nodes/DescriptorSetNode.h`
4. ... (see grep results)

**Strategy**: Update one file at a time, test after each change

---

### Phase 4: Clean Up Experimental Files

**Files to delete** (created during exploration):

1. **Type wrapper experiments**:
   - `TypeWrappers.h` - Runtime wrappers (replaced by compile-time tags)
   - `CompositeTypes.h` - Runtime composites (not needed)
   - `StructComposition.h` - Complex struct system (overkill)

2. **Migration scaffolding**:
   - `ResourceVariantMigration.h` - Temporary bridge
   - `ResourceVariantV2.h` - Intermediate version
   - `ResourceVariantV2Integration.h` - Integration layer
   - `TransparentTypeSystem.h` - Superseded by ZeroOverhead

3. **Validation experiments**:
   - `TypeValidation.h` - Runtime cache (not needed for compile-time)
   - `TypePattern.h` - Runtime pattern matching (compile-time tags better)

4. **Test files for removed features**:
   - `test_type_wrappers.cpp` - Tests runtime wrappers
   - `test_type_validation.cpp` - Tests runtime validation cache
   - `test_migration_compat.cpp` - Tests migration bridge

**Keep**:
- ✅ `ZeroOverheadTypeSystem.h` - Final system
- ✅ `AutoTypeDecomposition.h` - Type normalization (if still needed)
- ✅ `test_zero_overhead.cpp` - Tests final system
- ✅ `test_transparent_types.cpp` - Tests user-facing API

---

### Phase 5: Simplify Final System

**Action**: Inline AutoTypeDecomposition into ZeroOverheadTypeSystem

**Rationale**: Since everything is compile-time, keep it in one file

**Final file structure**:
```
RenderGraph/include/Data/Core/
├── ResourceTypes.h          (enums, unchanged)
├── ResourceV3.h             (new: zero-overhead system)
└── VariantDescriptors.h     (descriptors, unchanged)
```

---

## Detailed Migration Steps

### Step 1: Create ResourceV3.h (Drop-in Replacement)

```cpp
#pragma once

#include "Data/Core/ResourceTypes.h"
#include "Data/VariantDescriptors.h"
#include <variant>
#include <type_traits>

namespace Vixen::RenderGraph {

// ============================================================================
// COMPILE-TIME TYPE REGISTRY
// ============================================================================

template<typename T>
struct IsRegisteredType : std::false_type {};

#define REGISTER_TYPE(Type) \
    template<> struct IsRegisteredType<Type> : std::true_type {}

// Register all Vulkan types
REGISTER_TYPE(VkImage);
REGISTER_TYPE(VkBuffer);
// ... (all existing types)

// ============================================================================
// COMPILE-TIME TYPE TAGS
// ============================================================================

template<typename T> struct ValueTag { using storage_type = T; };
template<typename T> struct RefTag { using storage_type = T*; };
template<typename T> struct PtrTag { using storage_type = T*; };
// ... (as in ZeroOverheadTypeSystem.h)

// ============================================================================
// RESOURCE CLASS
// ============================================================================

class Resource {
    // ... (ZeroOverheadResource implementation)
};

} // namespace Vixen::RenderGraph
```

### Step 2: Update One File

**Example**: `RenderGraph/include/Core/NodeInstance.h`

```diff
- #include "Data/Core/ResourceVariant.h"
+ #include "Data/Core/ResourceV3.h"

// No other changes needed - API compatible!
```

**Test**: Build and run tests

### Step 3: Repeat for All Files

**Automated**:
```bash
# Find all files including ResourceVariant.h
grep -r "ResourceVariant.h" RenderGraph/include --files-with-matches

# Replace include (review each file first!)
sed -i 's|Data/Core/ResourceVariant.h|Data/Core/ResourceV3.h|g' <file>
```

**Manual verification**: Check each file compiles

### Step 4: Remove Old Files

```bash
# Move to archive
mkdir -p documentation/archive/old-type-system

# Archive old system
mv RenderGraph/include/Data/Core/ResourceVariant.h documentation/archive/old-type-system/
mv RenderGraph/include/Data/Core/ResourceTypeTraits.h documentation/archive/old-type-system/

# Archive experimental files
mv RenderGraph/include/Data/Core/TypeWrappers.h documentation/archive/experiments/
mv RenderGraph/include/Data/Core/CompositeTypes.h documentation/archive/experiments/
# ... (all experimental files)
```

### Step 5: Rename ResourceV3.h → ResourceVariant.h

**After all files migrated**:

```bash
mv RenderGraph/include/Data/Core/ResourceV3.h \
   RenderGraph/include/Data/Core/ResourceVariant.h

# Update all includes back to original name
sed -i 's|ResourceV3.h|ResourceVariant.h|g' RenderGraph/**/*.h
```

**Result**: Same file name, new implementation!

---

## Testing Strategy

**After each phase**:

1. **Build**: Ensure no compile errors
   ```bash
   cmake --build build --config Debug
   ```

2. **Unit tests**: Run all tests
   ```bash
   ctest --test-dir build -C Debug
   ```

3. **Specific tests**: Focus on resource handling
   ```bash
   build/RenderGraph/tests/Debug/test_zero_overhead.exe
   build/RenderGraph/tests/Debug/test_resourcevariant_handles.exe
   ```

4. **Integration**: Run full render graph tests
   ```bash
   build/RenderGraph/tests/Debug/test_graph_topology.exe
   ```

---

## Rollback Plan

**If issues arise**:

1. **Revert includes**: Change back to `ResourceVariant.h`
2. **Keep old files**: Don't delete until fully migrated
3. **Feature flag**: Add `#define USE_OLD_RESOURCE_SYSTEM` to toggle

---

## Files to Clean Up (Summary)

### Delete After Migration

**Runtime wrapper experiments** (10 files):
- `TypeWrappers.h`
- `CompositeTypes.h`
- `StructComposition.h`
- `TypePattern.h`
- `TypeValidation.h`
- `ResourceVariantV2.h`
- `ResourceVariantV2Integration.h`
- `ResourceVariantMigration.h`
- `TransparentTypeSystem.h`
- `AutoTypeDecomposition.h` (inline into final system)

**Test files for removed features** (3 files):
- `test_type_wrappers.cpp`
- `test_type_validation.cpp`
- `test_migration_compat.cpp`

### Keep

**Core system** (1 file):
- `ZeroOverheadTypeSystem.h` → renamed to `ResourceVariant.h`

**Tests** (2 files):
- `test_zero_overhead.cpp`
- `test_transparent_types.cpp`

---

## Success Criteria

✅ All existing tests pass
✅ No runtime overhead compared to old system
✅ Natural C++ syntax (`T&`, `T*`) works
✅ Compile-time type validation works
✅ Code compiles with no warnings
✅ Binary size same or smaller

---

## Timeline

**Estimated**: 2-4 hours

1. Create ResourceV3.h: 30 min
2. Update includes (29 files): 1-2 hours
3. Test after each update: ongoing
4. Clean up experimental files: 30 min
5. Final testing: 30 min

---

## Next Steps

1. ✅ Create ResourceV3.h drop-in replacement
2. ⏳ Update first file (NodeInstance.h) and test
3. ⏳ Incrementally update remaining 28 files
4. ⏳ Archive experimental files
5. ⏳ Rename ResourceV3 → ResourceVariant
6. ✅ Done!
