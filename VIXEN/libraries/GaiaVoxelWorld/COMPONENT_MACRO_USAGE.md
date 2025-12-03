# Component Macro Usage Guide

## Vec3 Component Macro Features

### Full Signature

```cpp
VOXEL_COMPONENT_VEC3_EX(
    ComponentName,    // Type name (e.g., Color)
    LogicalName,      // String name (e.g., "color")
    S0, S1, S2,      // Suffix names (e.g., r, g, b)
    BaseType,         // Underlying type (float, double, int32_t, etc.)
    Layout,           // DataLayout::AoS or DataLayout::SoA
    D0, D1, D2       // Default values
)
```

### Simplified Signature (Defaults: float, AoS)

```cpp
VOXEL_COMPONENT_VEC3(
    ComponentName,    // Type name
    LogicalName,      // String name
    S0, S1, S2,      // Suffix names
    D0, D1, D2       // Default values
)
// Equivalent to:
// VOXEL_COMPONENT_VEC3_EX(ComponentName, LogicalName, S0, S1, S2,
//                         float, DataLayout::AoS, D0, D1, D2)
```

---

## Example 1: Standard Color (Float, AoS)

```cpp
// RGB color with float precision, AoS layout
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, 1.0f, 1.0f, 1.0f)

// Expands to:
struct Color {
    static constexpr const char* Name = "color";
    static constexpr const char* Suffixes[3] = {"r", "g", "b"};
    static constexpr DataLayout PreferredLayout = DataLayout::AoS;
    using ComponentType = float;

    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    // Conversion methods...
    Color(const glm::vec3& v);
    operator glm::vec3() const;
    glm::vec3 toVec3() const;
};

// Usage:
Color white{1.0f, 1.0f, 1.0f};
Color red = Color(glm::vec3(1, 0, 0));
glm::vec3 v = white.toVec3();
```

---

## Example 2: High-Precision Normal (Double)

```cpp
// Normal vector with double precision for scientific simulations
VOXEL_COMPONENT_VEC3_EX(NormalHP, "normal_hp", x, y, z,
                        double, DataLayout::AoS, 0.0, 1.0, 0.0)

// Expands to:
struct NormalHP {
    static constexpr const char* Name = "normal_hp";
    static constexpr const char* Suffixes[3] = {"x", "y", "z"};
    static constexpr DataLayout PreferredLayout = DataLayout::AoS;
    using ComponentType = double;

    double x = 0.0;
    double y = 1.0;
    double z = 0.0;

    // Conversion methods return glm::dvec3
    glm::dvec3 toVec3() const;
};

// Usage:
NormalHP normal{0.0, 1.0, 0.0};
glm::dvec3 v = normal.toVec3();  // Returns glm::dvec3
```

---

## Example 3: Integer Velocity (int32_t)

```cpp
// Discrete velocity for grid-based physics
VOXEL_COMPONENT_VEC3_EX(VelocityGrid, "velocity_grid", vx, vy, vz,
                        int32_t, DataLayout::AoS, 0, 0, 0)

// Expands to:
struct VelocityGrid {
    static constexpr const char* Name = "velocity_grid";
    static constexpr const char* Suffixes[3] = {"vx", "vy", "vz"};
    static constexpr DataLayout PreferredLayout = DataLayout::AoS;
    using ComponentType = int32_t;

    int32_t vx = 0;
    int32_t vy = 0;
    int32_t vz = 0;

    // Conversion methods return glm::ivec3
    glm::ivec3 toVec3() const;
};

// Usage:
VelocityGrid vel{10, -5, 3};
glm::ivec3 v = vel.toVec3();  // Returns glm::ivec3
```

---

## Example 4: SoA Layout Hint

```cpp
// Color with SoA hint for SIMD processing
VOXEL_COMPONENT_VEC3_EX(ColorSoA, "color_soa", r, g, b,
                        float, DataLayout::SoA, 1.0f, 1.0f, 1.0f)

// Expands to:
struct ColorSoA {
    static constexpr const char* Name = "color_soa";
    static constexpr const char* Suffixes[3] = {"r", "g", "b"};
    static constexpr DataLayout PreferredLayout = DataLayout::SoA;  // ← SoA hint
    using ComponentType = float;

    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    // ... conversion methods
};
```

**Note:** Gaia ECS ultimately controls the actual layout. The `PreferredLayout` is a **hint** that can be used by:
- Custom allocators
- Serialization systems
- Performance profiling tools
- Documentation generators

---

## Data Layout Explanation

### AoS (Array of Structs) - Default

**Memory layout:**
```
Entity 0: [r0, g0, b0] (12 bytes)
Entity 1: [r1, g1, b1] (12 bytes)
Entity 2: [r2, g2, b2] (12 bytes)
...
```

**When to use:**
- ✅ Accessing all 3 components of one entity together
- ✅ Cache-friendly for per-entity operations
- ✅ Natural struct access (`color.r`, `color.g`, `color.b`)

**Example:**
```cpp
// Process individual entities
auto& color = world.get<Color>(entity);
color.r *= 1.1f;  // All components in same cache line
color.g *= 1.1f;
color.b *= 1.1f;
```

### SoA (Struct of Arrays)

**Memory layout (conceptual):**
```
R channel: [r0, r1, r2, r3, ...] (contiguous)
G channel: [g0, g1, g2, g3, ...] (contiguous)
B channel: [b0, b1, b2, b3, ...] (contiguous)
```

**When to use:**
- ✅ SIMD operations on one component across many entities
- ✅ Processing channels independently
- ✅ GPU-style parallel operations

**Example (conceptual):**
```cpp
// Process red channel across all entities (SIMD-friendly)
for (auto& color : colors) {
    color.r *= brightness;  // Vectorizable if SoA layout
}
```

**Implementation Note:**
To get true SoA with Gaia, split into separate components:
```cpp
struct Color_R { float value; };
struct Color_G { float value; };
struct Color_B { float value; };

// Gaia stores as 3 separate arrays (true SoA)
```

The `PreferredLayout = DataLayout::SoA` hint indicates this component **should** be split for optimal performance in certain scenarios.

---

## Component Type Metadata

All generated components have metadata accessible at compile-time:

```cpp
// Name (for debugging/serialization)
const char* name = Color::Name;  // "color"

// Suffixes (for attribute expansion)
const char* r_suffix = Color::Suffixes[0];  // "r"
const char* g_suffix = Color::Suffixes[1];  // "g"
const char* b_suffix = Color::Suffixes[2];  // "b"

// Layout preference
DataLayout layout = Color::PreferredLayout;  // DataLayout::AoS

// Component base type
using Type = Color::ComponentType;  // float
```

---

## Generic Programming with Base Types

```cpp
template<typename VecComp>
void processVec3Component(VecComp& comp) {
    using BaseType = typename VecComp::ComponentType;

    if constexpr (std::is_same_v<BaseType, float>) {
        // Float-specific processing
        comp.r = std::clamp(comp.r, 0.0f, 1.0f);
    } else if constexpr (std::is_same_v<BaseType, double>) {
        // Double-specific processing
        comp.x = std::clamp(comp.x, -1.0, 1.0);
    } else if constexpr (std::is_integral_v<BaseType>) {
        // Integer-specific processing
        comp.vx = std::max<BaseType>(comp.vx, 0);
    }
}

// Works with any vec3 component type
Color color;
NormalHP normalHP;
VelocityGrid velocity;

processVec3Component(color);      // Uses float path
processVec3Component(normalHP);   // Uses double path
processVec3Component(velocity);   // Uses integer path
```

---

## Automatic glm Type Selection

The macro automatically returns the correct glm type based on `BaseType`:

| BaseType | toVec3() Returns |
|----------|------------------|
| `float` | `glm::vec3` |
| `double` | `glm::dvec3` |
| `int32_t` | `glm::ivec3` |
| `uint32_t` | `glm::uvec3` |
| Other | `glm::vec<3, BaseType>` |

**Usage:**
```cpp
Color color;
auto v1 = color.toVec3();  // glm::vec3 (float)

NormalHP normalHP;
auto v2 = normalHP.toVec3();  // glm::dvec3 (double)

VelocityGrid velocity;
auto v3 = velocity.toVec3();  // glm::ivec3 (int32_t)
```

---

## Summary

**Macro Features:**
- ✅ Customizable base type (float, double, int, etc.)
- ✅ Layout hint (AoS or SoA)
- ✅ Custom suffix names per component type
- ✅ Automatic glm conversion
- ✅ Compile-time metadata
- ✅ Generic programming support

**Default Behavior:**
- Base type: `float`
- Layout: `DataLayout::AoS`
- Conversion: Automatic based on base type

**When to Use Extended Macro:**
- Need double precision → Use `VOXEL_COMPONENT_VEC3_EX` with `double`
- Need integer coords → Use `VOXEL_COMPONENT_VEC3_EX` with `int32_t`
- Want SoA hint → Use `VOXEL_COMPONENT_VEC3_EX` with `DataLayout::SoA`

**When to Use Simple Macro:**
- Standard float RGB color → Use `VOXEL_COMPONENT_VEC3`
- Standard float XYZ normal → Use `VOXEL_COMPONENT_VEC3`
- Most common use cases → Use `VOXEL_COMPONENT_VEC3`
