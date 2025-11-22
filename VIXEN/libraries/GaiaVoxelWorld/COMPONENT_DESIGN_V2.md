# Component Design V2 - Simplified Gaia ECS Integration

**Last Updated:** November 22, 2025
**Status:** ✅ Finalized Design

---

## Design Principles

1. **Gaia handles layout** - Use `GAIA_LAYOUT(AoS)` or `GAIA_LAYOUT(SoA)` directly
2. **Vec3 = float only** - Create specialized macros for other types
3. **Minimal metadata** - ComponentTraits stores only Name
4. **Type detection via concepts** - No manual bools or flags
5. **Natural glm::vec3 conversion** - Automatic via constructors/operators

---

## Macro Definitions

### Scalar Component

```cpp
VOXEL_COMPONENT_SCALAR(ComponentName, LogicalName, Type, DefaultValue)

// Example:
VOXEL_COMPONENT_SCALAR(Density, "density", float, 1.0f)

// Expands to:
struct Density {
    static constexpr const char* Name = "density";
    float value = 1.0f;
};
```

### Vec3 Component (Float Only)

```cpp
VOXEL_COMPONENT_VEC3(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2)

// Example:
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)

// Expands to:
struct Color {
    static constexpr const char* Name = "color";
    static constexpr const char* Suffixes[3] = {"r", "g", "b"};
    GAIA_LAYOUT(AoS);  // Gaia's native layout control

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

## Component Declarations

```cpp
// Spatial indexing
VOXEL_COMPONENT_SCALAR(MortonKey, "position", uint64_t, 0)

// Scalar attributes
VOXEL_COMPONENT_SCALAR(Density, "density", float, 1.0f)
VOXEL_COMPONENT_SCALAR(Material, "material", uint32_t, 0)
VOXEL_COMPONENT_SCALAR(EmissionIntensity, "emission_intensity", float, 0.0f)

// Vec3 attributes with Gaia layout control
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)
VOXEL_COMPONENT_VEC3(Normal, "normal", x, y, z, AoS, 0.0f, 1.0f, 0.0f)
VOXEL_COMPONENT_VEC3(Emission, "emission", r, g, b, AoS, 0.0f, 0.0f, 0.0f)

// Tag component (zero memory)
struct Solid {};
```

---

## Component Traits (Minimal)

**Only Name is stored:**
```cpp
template<>
struct ComponentTraits<Color> {
    static constexpr const char* Name = Color::Name;  // "color"
};

// Registered via macro:
DEFINE_COMPONENT_TRAITS(Color)
```

**Type detection via C++20 concepts:**
```cpp
// Automatic - no manual flags!
template<typename T>
concept Vec3Component = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
};

template<typename T>
concept ScalarComponent = requires(T t) {
    { t.value };
};

// Usage:
static_assert(Vec3Component<Color>);      // true
static_assert(ScalarComponent<Density>);  // true
```

---

## Usage Examples

### Creating Entities

```cpp
gaia::ecs::World world;
auto entity = world.add();

// Add MortonKey for position
world.add<MortonKey>(entity, MortonKey::fromPosition(pos));

// Add scalar components
world.add<Density>(entity, Density{1.0f});
world.add<Material>(entity, Material{42});

// Add vec3 components (natural conversion)
world.add<Color>(entity, Color(glm::vec3(1.0f, 0.5f, 0.2f)));
world.add<Normal>(entity, Normal{.x=0, .y=1, .z=0});

// Add tag for octree inclusion
world.add<Solid>(entity);
```

### Accessing Components

```cpp
// Get scalar value
float density = world.get<Density>(entity).value;

// Get vec3 (automatic conversion)
glm::vec3 color = world.get<Color>(entity);  // operator glm::vec3()

// Or explicit
Color colorComp = world.get<Color>(entity);
glm::vec3 colorVec = colorComp.toVec3();

// Direct member access
auto& normal = world.get<Normal>(entity);
normal.x = 0.0f;
normal.y = 1.0f;
normal.z = 0.0f;
```

### Querying Entities

```cpp
// Query solid voxels (tag-based filtering - FAST!)
auto solidQuery = world.query().all<Solid, MortonKey, Density>();
solidQuery.each([](Solid, MortonKey key, Density d) {
    // Process only solid voxels
});

// Query with vec3 components
auto colorQuery = world.query().all<Color, Normal>();
colorQuery.each([](Color& c, Normal& n) {
    // Gaia ensures contiguous memory access
    glm::vec3 color = c.toVec3();
    glm::vec3 normal = n.toVec3();
});
```

---

## Gaia Layout Control

### AoS (Array of Structs) - Default

```cpp
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)

// Memory layout:
// Entity 0: {r0, g0, b0}  (12 bytes)
// Entity 1: {r1, g1, b1}  (12 bytes)
// ...
```

**Use for:**
- ✅ Accessing all 3 components together
- ✅ Natural OOP-style code (`color.r`, `color.g`, `color.b`)
- ✅ Cache-friendly per-entity operations

### SoA (Struct of Arrays) - SIMD Hint

```cpp
VOXEL_COMPONENT_VEC3(Velocity, "velocity", vx, vy, vz, SoA, 0.0f, 0.0f, 0.0f)

// Memory layout (Gaia internal):
// vx array: [vx0, vx1, vx2, vx3, ...]
// vy array: [vy0, vy1, vy2, vy3, ...]
// vz array: [vz0, vz1, vz2, vz3, ...]
```

**Use for:**
- ✅ SIMD operations on one component across many entities
- ✅ Bulk channel processing
- ✅ Physics simulations

**Example:**
```cpp
// Gaia can SIMD-optimize this with SoA layout
auto velocityQuery = world.query().all<Velocity>();
velocityQuery.each([](Velocity& v) {
    v.vx *= 0.99f;  // All vx values contiguous
});
```

---

## Extending for Custom Types

### Double-Precision Vec3

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

// Usage:
VOXEL_COMPONENT_VEC3D(PositionHP, "position_hp", x, y, z, AoS, 0.0, 0.0, 0.0)
```

### Integer Vec3

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

// Usage:
VOXEL_COMPONENT_VEC3I(GridOffset, "grid_offset", dx, dy, dz, AoS, 0, 0, 0)
```

---

## Benefits

### 1. Simplicity
- ✅ Two macros: `VOXEL_COMPONENT_SCALAR` and `VOXEL_COMPONENT_VEC3`
- ✅ No complex template parameters
- ✅ Easy to read and understand

### 2. Gaia Native
- ✅ Uses `GAIA_LAYOUT` directly (not abstracted away)
- ✅ Gaia handles all memory management
- ✅ SoA optimization transparent to user

### 3. Type Safety
- ✅ Concepts detect types automatically
- ✅ No manual `IsVec3` bool flags
- ✅ Compile-time errors for misuse

### 4. Extensibility
- ✅ Clear pattern for custom types (`_VEC3D`, `_VEC3I`)
- ✅ Just copy macro and change type
- ✅ No complex inheritance or traits

### 5. Performance
- ✅ Zero overhead abstraction
- ✅ glm::vec3 conversion is inline
- ✅ Gaia's SoA layout enables SIMD

---

## Migration from Old Split Components

**Old (Manual Split):**
```cpp
struct Color_R { float value; };
struct Color_G { float value; };
struct Color_B { float value; };

world.add<Color_R>(entity, Color_R{1.0f});
world.add<Color_G>(entity, Color_G{0.5f});
world.add<Color_B>(entity, Color_B{0.2f});

float r = world.get<Color_R>(entity).value;
float g = world.get<Color_G>(entity).value;
float b = world.get<Color_B>(entity).value;
```

**New (Multi-Member):**
```cpp
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)

world.add<Color>(entity, Color(glm::vec3(1.0f, 0.5f, 0.2f)));

glm::vec3 color = world.get<Color>(entity);  // Automatic conversion
```

**Benefits:**
- 3x fewer component accesses
- Natural glm::vec3 API
- Still cache-friendly (12-byte struct)

---

## Summary

**ComponentTraits = 1 field:**
```cpp
static constexpr const char* Name;
```

**Type detection = automatic:**
```cpp
concept Vec3Component = has toVec3() method
concept ScalarComponent = has .value member
```

**Macros = simple:**
```cpp
VOXEL_COMPONENT_SCALAR(Name, "name", Type, Default)
VOXEL_COMPONENT_VEC3(Name, "name", s0, s1, s2, AoS/SoA, d0, d1, d2)
```

**Gaia handles:**
- ✅ Memory layout (AoS or SoA)
- ✅ Archetype management
- ✅ SIMD optimization
- ✅ Thread-safe access

This is the C++20 + Gaia ECS way - simple, type-safe, performant!
