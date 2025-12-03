# Simplified Component Macros - Gaia Handles Everything

## Design Philosophy

1. **Vec3 = always float** - If you need other types, create specialized macros
2. **Gaia controls layout** - Use `GAIA_LAYOUT(AoS)` or `GAIA_LAYOUT(SoA)` directly
3. **No redundant metadata** - Only store Name, Gaia handles the rest
4. **Simple macros** - Easy to read, easy to extend

---

## Macro Signatures

### Scalar Component

```cpp
VOXEL_COMPONENT_SCALAR(ComponentName, LogicalName, Type, DefaultValue)
```

**Example:**
```cpp
VOXEL_COMPONENT_SCALAR(Density, "density", float, 1.0f)

// Expands to:
struct Density {
    static constexpr const char* Name = "density";
    float value = 1.0f;
};
```

### Vec3 Component

```cpp
VOXEL_COMPONENT_VEC3(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2)
```

**Parameters:**
- `ComponentName` - Struct name (e.g., `Color`)
- `LogicalName` - String name (e.g., `"color"`)
- `S0, S1, S2` - Member names (e.g., `r, g, b` or `x, y, z`)
- **`Layout`** - `AoS` or `SoA` (controls `GAIA_LAYOUT` macro)
- `D0, D1, D2` - Default values (floats)

**Example:**
```cpp
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)

// Expands to:
struct Color {
    static constexpr const char* Name = "color";
    static constexpr const char* Suffixes[3] = {"r", "g", "b"};
    GAIA_LAYOUT(AoS);  // ← Gaia's macro controls layout

    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    Color() = default;
    Color(const glm::vec3& v) : r(v[0]), g(v[1]), b(v[2]) {}
    operator glm::vec3() const { return glm::vec3(r, g, b); }
    glm::vec3 toVec3() const { return glm::vec3(r, g, b); }
};
```

---

## Usage Examples

### Standard Components

```cpp
// Scalar
VOXEL_COMPONENT_SCALAR(Density, "density", float, 1.0f)
VOXEL_COMPONENT_SCALAR(Material, "material", uint32_t, 0)

// Vec3 with AoS layout (default - natural struct access)
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)
VOXEL_COMPONENT_VEC3(Normal, "normal", x, y, z, AoS, 0.0f, 1.0f, 0.0f)

// Vec3 with SoA layout (SIMD-optimized by Gaia)
VOXEL_COMPONENT_VEC3(Velocity, "velocity", vx, vy, vz, SoA, 0.0f, 0.0f, 0.0f)
```

### When to Use AoS vs SoA

**AoS (Array of Structs)** - Default choice:
```cpp
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)
```

**Use when:**
- ✅ Accessing all 3 components together per entity
- ✅ Natural OOP-style code (`color.r`, `color.g`, `color.b`)
- ✅ Components fit in cache line (12 bytes for vec3)

**Example:**
```cpp
// Processing one entity at a time
auto& color = world.get<Color>(entity);
color.r *= brightness;
color.g *= brightness;
color.b *= brightness;
```

**SoA (Struct of Arrays)** - SIMD optimization:
```cpp
VOXEL_COMPONENT_VEC3(Velocity, "velocity", vx, vy, vz, SoA, 0.0f, 0.0f, 0.0f)
```

**Use when:**
- ✅ Processing one component across many entities (SIMD)
- ✅ Bulk operations on single channel
- ✅ Physics simulations (velocity, acceleration)

**Example:**
```cpp
// Gaia can SIMD-optimize this loop with SoA layout
query.each([](Velocity& v) {
    v.vx *= 0.99f;  // All vx values contiguous in memory
});
```

---

## Extending for Custom Types

### Creating a Double-Precision Vec3

If you need `double` instead of `float`:

```cpp
#define VOXEL_COMPONENT_VEC3D(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2) \
    struct ComponentName { \
        static constexpr const char* Name = LogicalName; \
        static constexpr const char* Suffixes[3] = {#S0, #S1, #S2}; \
        GAIA_LAYOUT(Layout); \
        \
        double S0 = D0; \
        double S1 = D1; \
        double S2 = D2; \
        \
        ComponentName() = default; \
        ComponentName(const glm::dvec3& v) : S0(v[0]), S1(v[1]), S2(v[2]) {} \
        operator glm::dvec3() const { return glm::dvec3(S0, S1, S2); } \
        glm::dvec3 toVec3() const { return glm::dvec3(S0, S1, S2); } \
    };

// Usage
VOXEL_COMPONENT_VEC3D(PositionHP, "position_hp", x, y, z, AoS, 0.0, 0.0, 0.0)
```

### Creating an Integer Vec3

```cpp
#define VOXEL_COMPONENT_VEC3I(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2) \
    struct ComponentName { \
        static constexpr const char* Name = LogicalName; \
        static constexpr const char* Suffixes[3] = {#S0, #S1, #S2}; \
        GAIA_LAYOUT(Layout); \
        \
        int32_t S0 = D0; \
        int32_t S1 = D1; \
        int32_t S2 = D2; \
        \
        ComponentName() = default; \
        ComponentName(const glm::ivec3& v) : S0(v[0]), S1(v[1]), S2(v[2]) {} \
        operator glm::ivec3() const { return glm::ivec3(S0, S1, S2); } \
        glm::ivec3 toVec3() const { return glm::ivec3(S0, S1, S2); } \
    };

// Usage
VOXEL_COMPONENT_VEC3I(GridCoord, "grid_coord", gx, gy, gz, AoS, 0, 0, 0)
```

---

## Component Traits (Minimal)

**Only Name is stored:**
```cpp
template<>
struct ComponentTraits<Color> {
    static constexpr const char* Name = Color::Name;  // "color"
};
```

**Type detection via concepts:**
```cpp
// Automatic - no manual flags!
static_assert(Vec3Component<Color>);      // has toVec3() → true
static_assert(ScalarComponent<Density>);  // has .value → true
```

---

## What Gaia's GAIA_LAYOUT Macro Does

**From Gaia ECS documentation:**
```cpp
// AoS layout (default for multi-member components)
struct Color {
    GAIA_LAYOUT(AoS);
    float r, g, b;
};
// Memory: [{r0,g0,b0}, {r1,g1,b1}, {r2,g2,b2}, ...]

// SoA layout (explicit optimization)
struct Velocity {
    GAIA_LAYOUT(SoA);
    float vx, vy, vz;
};
// Memory (Gaia internal): [vx0,vx1,vx2,...], [vy0,vy1,vy2,...], [vz0,vz1,vz2,...]
```

**Benefits:**
- ✅ Gaia handles memory management
- ✅ Layout hint for optimizer
- ✅ No manual splitting required
- ✅ Single component definition

---

## Summary

**Simplified Macro System:**
```cpp
// Scalar (any type)
VOXEL_COMPONENT_SCALAR(Name, "name", Type, Default)

// Vec3 (float only, Gaia layout control)
VOXEL_COMPONENT_VEC3(Name, "name", s0, s1, s2, AoS/SoA, d0, d1, d2)
```

**Key Simplifications:**
- ❌ No custom base type parameter (float only, create new macro if needed)
- ❌ No DataLayout enum (use Gaia's AoS/SoA directly)
- ❌ No redundant metadata (Name only)
- ✅ Gaia's `GAIA_LAYOUT` macro controls layout
- ✅ Simple, readable, extendable

**When You Need Other Types:**
- Create `VOXEL_COMPONENT_VEC3D` for double
- Create `VOXEL_COMPONENT_VEC3I` for int32_t
- Pattern is clear and easy to replicate

This is clean, minimal, and lets Gaia do what it does best!
