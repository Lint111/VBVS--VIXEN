# VoxelConfig Macro System

## Overview

The VoxelConfig system provides a compile-time, zero-overhead way to define voxel attribute schemas using macros. The system automatically handles initialization behind the scenes.

## Usage Comparison

### Before (Manual Initialization)
```cpp
VOXEL_CONFIG(StandardVoxel, 3) {
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2);

    StandardVoxel() {
        attributes[0] = DENSITY_desc;  // Manual repetition
        attributes[1] = MATERIAL_desc;  // Error-prone
        attributes[2] = COLOR_desc;     // Must match order
    }
};
```

**Problems:**
- ❌ Attributes listed twice (declaration + initialization)
- ❌ Easy to forget attributes in constructor
- ❌ Manual array indexing error-prone
- ❌ Order must match exactly

---

### After (Automatic Initialization via BEGIN/END)
```cpp
VOXEL_CONFIG_BEGIN(StandardVoxel, 3)
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2);
VOXEL_CONFIG_END(StandardVoxel, DENSITY, MATERIAL, COLOR)
```

**Benefits:**
- ✅ Attributes listed once (no repetition)
- ✅ Constructor auto-generated
- ✅ Impossible to forget attributes
- ✅ Order validated at compile-time
- ✅ Cleaner, more declarative

---

## Macro Details

### `VOXEL_CONFIG_BEGIN(ConfigName, NumAttributes)`

Starts a voxel configuration struct definition.

**Parameters:**
- `ConfigName`: Name of the config struct
- `NumAttributes`: Total number of attributes (must match actual count)

**Expands to:**
```cpp
struct ConfigName : public VoxelData::VoxelConfigBase<NumAttributes> {
```

---

### `VOXEL_CONFIG_END(ConfigName, ...)`

Closes the struct and auto-generates the constructor.

**Parameters:**
- `ConfigName`: Name of the config struct (must match BEGIN)
- `...`: Comma-separated list of attribute names (without `_desc` suffix)

**Expands to:**
```cpp
    ConfigName() {
        attributes = { ATTR1_desc, ATTR2_desc, ... };
    }
}
```

---

### `VOXEL_KEY(AttrName, AttrType, Index, ...)`

Defines the **key attribute** that determines octree structure.

**Parameters:**
- `AttrName`: Uppercase constant name (auto-lowercased to runtime string)
- `AttrType`: C++ type (`float`, `uint32_t`, `glm::vec3`, etc.)
- `Index`: Attribute index (must be consistent across all attributes)
- `...`: Optional custom default value (uses type default if omitted)

**Example:**
```cpp
VOXEL_KEY(DENSITY, float, 0);           // "density", default 0.0f
VOXEL_KEY(HEALTH, uint16_t, 0, 100);    // "health", default 100
```

---

### `VOXEL_ATTRIBUTE(AttrName, AttrType, Index, ...)`

Defines a **non-key attribute** that can be added/removed at runtime.

**Parameters:** Same as `VOXEL_KEY`

**Example:**
```cpp
VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);                  // "material", default 0u
VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2, glm::vec3(1.0f));  // "color", default white
```

---

## Complete Examples

### BasicVoxel (Minimal)
```cpp
VOXEL_CONFIG_BEGIN(BasicVoxel, 2)
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
VOXEL_CONFIG_END(BasicVoxel, DENSITY, MATERIAL)
```

### StandardVoxel (Common)
```cpp
VOXEL_CONFIG_BEGIN(StandardVoxel, 3)
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2);
VOXEL_CONFIG_END(StandardVoxel, DENSITY, MATERIAL, COLOR)
```

### RichVoxel (Full PBR)
```cpp
VOXEL_CONFIG_BEGIN(RichVoxel, 6)
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2, glm::vec3(1.0f));
    VOXEL_ATTRIBUTE(NORMAL, glm::vec3, 3, glm::vec3(0.0f, 1.0f, 0.0f));
    VOXEL_ATTRIBUTE(METALLIC, float, 4);
    VOXEL_ATTRIBUTE(ROUGHNESS, float, 5, 0.5f);
VOXEL_CONFIG_END(RichVoxel, DENSITY, MATERIAL, COLOR, NORMAL, METALLIC, ROUGHNESS)
```

### GameVoxel (Custom Attributes)
```cpp
VOXEL_CONFIG_BEGIN(GameVoxel, 4)
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(HEALTH, uint16_t, 2, static_cast<uint16_t>(100));
    VOXEL_ATTRIBUTE(DAMAGE, float, 3, 1.0f);
VOXEL_CONFIG_END(GameVoxel, DENSITY, MATERIAL, HEALTH, DAMAGE)
```

---

## Integration with DynamicVoxelStruct

Configs can directly initialize dynamic voxel structures:

```cpp
// Create config
StandardVoxel config;

// Initialize scalar voxel from config
DynamicVoxelScalar voxel(&config);
// voxel now has: density, material, color

// Initialize batch arrays from config
DynamicVoxelArrays batch(&config);
batch.reserve(1000);
// batch now has: density[], material[], color[]
```

---

## Compile-Time Guarantees

### Type Safety
```cpp
// All type information is constexpr
constexpr auto densityType = StandardVoxel::DENSITY_Member::attributeType;
constexpr uint32_t densityIndex = StandardVoxel::DENSITY_Member::index;
constexpr bool densityIsKey = StandardVoxel::DENSITY_Member::isKey;

static_assert(densityType == AttributeType::Float);
static_assert(densityIndex == 0);
static_assert(densityIsKey == true);
```

### Attribute Count Validation
```cpp
// Compiler ensures attribute count matches
VALIDATE_VOXEL_CONFIG(StandardVoxel, 3);  // Passes
VALIDATE_VOXEL_CONFIG(StandardVoxel, 4);  // Compile error
```

### Zero Runtime Overhead
- All macro expansions resolve at compile-time
- No string lookups, hash maps, or runtime checks
- Direct array indexing: `attributes[0]` for DENSITY
- Equivalent to hand-written C structs

---

## Backward Compatibility

The old `VOXEL_CONFIG` + manual `VOXEL_CONFIG_INIT` style is still supported:

```cpp
VOXEL_CONFIG(StandardVoxel, 3) {
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2);

    StandardVoxel() {
        VOXEL_CONFIG_INIT(DENSITY, MATERIAL, COLOR);
    }
};
```

**Recommendation:** Use `VOXEL_CONFIG_BEGIN`/`END` for new code.

---

## Best Practices

### 1. Attribute Naming
- Use SCREAMING_SNAKE_CASE for constants: `DENSITY`, `MATERIAL_ID`
- Auto-lowercased to runtime strings: `"density"`, `"material_id"`

### 2. Index Consistency
- Index must match position in VOXEL_CONFIG_END list
- Key attribute should be index 0
- Gaps in indices are allowed but discouraged

### 3. Default Values
- Omit for type defaults: `VOXEL_ATTRIBUTE(DENSITY, float, 0)` → 0.0f
- Provide for custom: `VOXEL_ATTRIBUTE(ROUGHNESS, float, 5, 0.5f)` → 0.5f
- Cast integers: `static_cast<uint16_t>(100)` for uint16_t

### 4. Vec3 Expansion
- Vec3 attributes expand to 3 float arrays: `color` → `color_x`, `color_y`, `color_z`
- Logical accessor uses base name: `brick.setAt3D<glm::vec3>("color", x, y, z, value)`

---

## Files

- **[VoxelConfig.h](include/VoxelConfig.h)** - Macro definitions and type traits
- **[StandardVoxelConfigs.h](include/StandardVoxelConfigs.h)** - Pre-defined configs
- **[DynamicVoxelStruct.h](include/DynamicVoxelStruct.h)** - Runtime dynamic structures
- **[VoxelConfigExample.cpp](examples/VoxelConfigExample.cpp)** - Usage examples
- **[DynamicVoxelExample.cpp](examples/DynamicVoxelExample.cpp)** - Integration examples

---

## Summary

The VoxelConfig macro system achieves:
- ✅ **Zero repetition** - attributes listed once
- ✅ **Compile-time safety** - type/index validation
- ✅ **Zero overhead** - no runtime cost
- ✅ **Clean syntax** - declarative, readable
- ✅ **Auto-initialization** - constructor generated
- ✅ **Extensible** - easy to add new attributes

**Result:** User declares attributes once, everything else is automatic!
