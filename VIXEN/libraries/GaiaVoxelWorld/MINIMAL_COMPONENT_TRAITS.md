# Minimal Component Traits - Type Detection via Concepts

## The Problem with Bools

**OLD Design (Manual Flags):**
```cpp
template<>
struct ComponentTraits<Color> {
    static constexpr const char* Name = "color";
    static constexpr bool IsVec3 = true;         // ❌ Manual flag
    static constexpr bool IsScalar = false;      // ❌ Redundant
    using ValueType = glm::vec3;                 // ❌ Redundant
};
```

**Problems:**
- ❌ Duplicate information (compiler already knows the structure)
- ❌ Manual synchronization (change component struct, must update traits)
- ❌ Error-prone (forgot to set `IsVec3 = true`? Silent bugs!)
- ❌ Bloated metadata

---

## NEW Design: Automatic Type Detection

**ComponentTraits stores ONLY Name:**
```cpp
template<>
struct ComponentTraits<Color> {
    static constexpr const char* Name = "color";  // ✅ Only metadata needed
};
```

**Type detection via C++20 concepts:**
```cpp
// Detect vec3 component (has toVec3() method)
template<typename T>
concept Vec3Component = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
};

// Detect scalar component (has .value member)
template<typename T>
concept ScalarComponent = requires(T t) {
    { t.value };
};
```

**Benefits:**
- ✅ Compiler automatically detects type from structure
- ✅ No manual synchronization needed
- ✅ Compile-time errors if used incorrectly
- ✅ Zero runtime overhead
- ✅ Single source of truth (component struct itself)

---

## Usage Examples

### 1. Type-Safe Generic Functions

```cpp
// Works with ANY vec3 component (Color, Normal, Emission)
template<Vec3Component T>
glm::vec3 getAsVec3(const T& component) {
    return component.toVec3();  // Guaranteed to exist by concept
}

// Works with ANY scalar component (Density, Material, etc.)
template<ScalarComponent T>
auto getScalarValue(const T& component) {
    return component.value;  // Guaranteed to exist by concept
}

// Usage
Color color{1.0f, 0.5f, 0.2f};
Density density{1.0f};

glm::vec3 v = getAsVec3(color);     // ✅ Compiles
auto d = getScalarValue(density);   // ✅ Compiles

// glm::vec3 v2 = getAsVec3(density);  // ❌ Compile error: Density is not Vec3Component
```

### 2. Compile-Time Type Checking

```cpp
// Static assertions work at compile-time
static_assert(Vec3Component<Color>);       // ✅ true
static_assert(Vec3Component<Normal>);      // ✅ true
static_assert(ScalarComponent<Density>);   // ✅ true
static_assert(ScalarComponent<Material>);  // ✅ true

static_assert(!Vec3Component<Density>);    // ✅ true (Density is NOT vec3)
static_assert(!ScalarComponent<Color>);    // ✅ true (Color is NOT scalar)
```

### 3. Automatic Dispatch

```cpp
// Generic getValue that works for ANY component type
template<VoxelComponent T>
auto getValue(const T& component) {
    if constexpr (Vec3Component<T>) {
        return component.toVec3();  // Returns glm::vec3
    } else if constexpr (ScalarComponent<T>) {
        return component.value;     // Returns scalar (float, uint32, etc.)
    }
}

// Usage - automatic dispatch based on type
Color color{1, 0.5, 0.2};
Density density{1.0f};

glm::vec3 colorVec = getValue(color);    // Calls toVec3()
float densityVal = getValue(density);    // Accesses .value
```

### 4. Concept-Constrained Iteration

```cpp
// Only accept vec3 components
template<Vec3Component T>
void normalizeComponents(gaia::ecs::World& world) {
    auto query = world.query().all<T>();
    query.each([](T& comp) {
        glm::vec3 v = comp.toVec3();
        v = glm::normalize(v);
        comp = T(v);  // Set back via constructor
    });
}

// Usage
normalizeComponents<Normal>(world);  // ✅ Works - Normal is vec3
normalizeComponents<Color>(world);   // ✅ Works - Color is vec3
// normalizeComponents<Density>(world);  // ❌ Compile error - Density is not vec3
```

---

## How Type Detection Works

### Vec3 Component Detection

**Component structure:**
```cpp
struct Color {
    static constexpr const char* Name = "color";
    float r, g, b;

    Color(const glm::vec3& v) : r(v.r), g(v.g), b(v.b) {}
    glm::vec3 toVec3() const { return glm::vec3(r, g, b); }  // ← Detected!
};
```

**Concept checks for `toVec3()` method:**
```cpp
template<typename T>
concept Vec3Component = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
    //   ^^^^^^^
    //   Checks if this method exists and returns glm::vec3
};
```

**Compiler reasoning:**
1. Does `Color` have a `toVec3()` method? **Yes**
2. Does it return something convertible to `glm::vec3`? **Yes**
3. Therefore, `Vec3Component<Color>` is **true**

### Scalar Component Detection

**Component structure:**
```cpp
struct Density {
    static constexpr const char* Name = "density";
    float value = 1.0f;  // ← Detected!
};
```

**Concept checks for `.value` member:**
```cpp
template<typename T>
concept ScalarComponent = requires(T t) {
    { t.value };
    //   ^^^^^
    //   Checks if this member exists
};
```

**Compiler reasoning:**
1. Does `Density` have a `value` member? **Yes**
2. Therefore, `ScalarComponent<Density>` is **true**

---

## Comparison: Manual Flags vs Automatic Detection

### Adding a New Component

**Manual Flags (OLD):**
```cpp
// Step 1: Define component
struct Velocity {
    float vx, vy, vz;
    glm::vec3 toVec3() const { return {vx, vy, vz}; }
};

// Step 2: Define traits (MUST REMEMBER!)
template<>
struct ComponentTraits<Velocity> {
    static constexpr const char* Name = "velocity";
    static constexpr bool IsVec3 = true;  // ← Forgot this? Silent bugs!
};
```

**Automatic Detection (NEW):**
```cpp
// Step 1: Define component (DONE!)
VOXEL_COMPONENT_VEC3(Velocity, "velocity", vx, vy, vz, 0, 0, 0)

// Step 2: Register name (one line)
DEFINE_COMPONENT_TRAITS(Velocity)

// Type detection is automatic!
static_assert(Vec3Component<Velocity>);  // ✅ Already works
```

### Changing Component Type

**Manual Flags (OLD):**
```cpp
// Change Density from float to vec3
struct Density {
    float x, y, z;  // Changed!
    glm::vec3 toVec3() const { return {x, y, z}; }
};

// MUST REMEMBER to update traits manually
template<>
struct ComponentTraits<Density> {
    static constexpr bool IsVec3 = true;  // ← Must change this!
    // Forgot? Compile errors or silent bugs!
};
```

**Automatic Detection (NEW):**
```cpp
// Change Density from scalar to vec3
struct Density {
    float x, y, z;  // Changed!
    glm::vec3 toVec3() const { return {x, y, z}; }
};

// Traits automatically updated (no changes needed!)
static_assert(Vec3Component<Density>);  // ✅ true now
static_assert(!ScalarComponent<Density>);  // ✅ false now
```

---

## Summary

**ComponentTraits = 1 field (Name only):**
```cpp
template<>
struct ComponentTraits<Color> {
    static constexpr const char* Name = "color";
};
```

**Type detection = automatic via concepts:**
```cpp
template<typename T>
concept Vec3Component = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
};

template<typename T>
concept ScalarComponent = requires(T t) { { t.value }; };
```

**Benefits:**
- ✅ No manual `IsVec3` bool
- ✅ No manual `ValueType` typedef
- ✅ No manual `IsScalar` flag
- ✅ Compiler enforces correctness
- ✅ Single source of truth
- ✅ Zero runtime overhead
- ✅ Compile-time errors for misuse

**This is the C++20 way - let the compiler do the work!**
