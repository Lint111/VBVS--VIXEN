# GLSL to C++ Type & Function Mapping

Complete reference for translating GLSL shader code to C++ mirror classes.

## Type Mappings

### Scalar Types

| GLSL | C++ | Notes |
|------|-----|-------|
| `bool` | `bool` | Direct |
| `int` | `int32_t` | Use fixed-width |
| `uint` | `uint32_t` | Use fixed-width |
| `float` | `float` | Direct |
| `double` | `double` | Direct |

### Vector Types

| GLSL | C++ (GLM) | Header |
|------|-----------|--------|
| `bvec2` | `glm::bvec2` | `<glm/glm.hpp>` |
| `bvec3` | `glm::bvec3` | |
| `bvec4` | `glm::bvec4` | |
| `ivec2` | `glm::ivec2` | |
| `ivec3` | `glm::ivec3` | |
| `ivec4` | `glm::ivec4` | |
| `uvec2` | `glm::uvec2` | |
| `uvec3` | `glm::uvec3` | |
| `uvec4` | `glm::uvec4` | |
| `vec2` | `glm::vec2` | |
| `vec3` | `glm::vec3` | |
| `vec4` | `glm::vec4` | |
| `dvec2` | `glm::dvec2` | |
| `dvec3` | `glm::dvec3` | |
| `dvec4` | `glm::dvec4` | |

### Matrix Types

| GLSL | C++ (GLM) |
|------|-----------|
| `mat2` | `glm::mat2` |
| `mat3` | `glm::mat3` |
| `mat4` | `glm::mat4` |
| `mat2x3` | `glm::mat2x3` |
| `mat2x4` | `glm::mat2x4` |
| `mat3x2` | `glm::mat3x2` |
| `mat3x4` | `glm::mat3x4` |
| `mat4x2` | `glm::mat4x2` |
| `mat4x3` | `glm::mat4x3` |

## Function Mappings

### Trigonometric

| GLSL | C++ (GLM) |
|------|-----------|
| `radians(x)` | `glm::radians(x)` |
| `degrees(x)` | `glm::degrees(x)` |
| `sin(x)` | `glm::sin(x)` |
| `cos(x)` | `glm::cos(x)` |
| `tan(x)` | `glm::tan(x)` |
| `asin(x)` | `glm::asin(x)` |
| `acos(x)` | `glm::acos(x)` |
| `atan(y, x)` | `glm::atan(y, x)` |
| `atan(x)` | `glm::atan(x)` |
| `sinh(x)` | `glm::sinh(x)` |
| `cosh(x)` | `glm::cosh(x)` |
| `tanh(x)` | `glm::tanh(x)` |

### Exponential

| GLSL | C++ (GLM) |
|------|-----------|
| `pow(x, y)` | `glm::pow(x, y)` |
| `exp(x)` | `glm::exp(x)` |
| `log(x)` | `glm::log(x)` |
| `exp2(x)` | `glm::exp2(x)` |
| `log2(x)` | `glm::log2(x)` |
| `sqrt(x)` | `glm::sqrt(x)` |
| `inversesqrt(x)` | `glm::inversesqrt(x)` |

### Common

| GLSL | C++ (GLM) |
|------|-----------|
| `abs(x)` | `glm::abs(x)` |
| `sign(x)` | `glm::sign(x)` |
| `floor(x)` | `glm::floor(x)` |
| `trunc(x)` | `glm::trunc(x)` |
| `round(x)` | `glm::round(x)` |
| `roundEven(x)` | `glm::roundEven(x)` |
| `ceil(x)` | `glm::ceil(x)` |
| `fract(x)` | `glm::fract(x)` |
| `mod(x, y)` | `glm::mod(x, y)` |
| `min(x, y)` | `glm::min(x, y)` |
| `max(x, y)` | `glm::max(x, y)` |
| `clamp(x, lo, hi)` | `glm::clamp(x, lo, hi)` |
| `mix(a, b, t)` | `glm::mix(a, b, t)` |
| `step(edge, x)` | `glm::step(edge, x)` |
| `smoothstep(a, b, x)` | `glm::smoothstep(a, b, x)` |
| `isnan(x)` | `glm::isnan(x)` |
| `isinf(x)` | `glm::isinf(x)` |

### Geometric

| GLSL | C++ (GLM) |
|------|-----------|
| `length(x)` | `glm::length(x)` |
| `distance(a, b)` | `glm::distance(a, b)` |
| `dot(a, b)` | `glm::dot(a, b)` |
| `cross(a, b)` | `glm::cross(a, b)` |
| `normalize(x)` | `glm::normalize(x)` |
| `faceforward(N, I, Nref)` | `glm::faceforward(N, I, Nref)` |
| `reflect(I, N)` | `glm::reflect(I, N)` |
| `refract(I, N, eta)` | `glm::refract(I, N, eta)` |

### Matrix

| GLSL | C++ (GLM) |
|------|-----------|
| `matrixCompMult(a, b)` | `glm::matrixCompMult(a, b)` |
| `outerProduct(a, b)` | `glm::outerProduct(a, b)` |
| `transpose(m)` | `glm::transpose(m)` |
| `determinant(m)` | `glm::determinant(m)` |
| `inverse(m)` | `glm::inverse(m)` |

### Vector Relational

| GLSL | C++ (GLM) |
|------|-----------|
| `lessThan(a, b)` | `glm::lessThan(a, b)` |
| `lessThanEqual(a, b)` | `glm::lessThanEqual(a, b)` |
| `greaterThan(a, b)` | `glm::greaterThan(a, b)` |
| `greaterThanEqual(a, b)` | `glm::greaterThanEqual(a, b)` |
| `equal(a, b)` | `glm::equal(a, b)` |
| `notEqual(a, b)` | `glm::notEqual(a, b)` |
| `any(x)` | `glm::any(x)` |
| `all(x)` | `glm::all(x)` |
| `not(x)` | `glm::not_(x)` |

### Integer / Bitwise

| GLSL | C++ |
|------|-----|
| `bitfieldExtract(val, off, bits)` | Custom (see below) |
| `bitfieldInsert(base, ins, off, bits)` | Custom (see below) |
| `bitfieldReverse(x)` | Custom (see below) |
| `bitCount(x)` | `__builtin_popcount(x)` or `std::popcount(x)` (C++20) |
| `findLSB(x)` | `__builtin_ctz(x)` or custom |
| `findMSB(x)` | `31 - __builtin_clz(x)` or custom |

### Bit Function Implementations

```cpp
// bitfieldExtract
inline uint32_t bitfieldExtract(uint32_t value, int offset, int bits) {
    uint32_t mask = (1u << bits) - 1u;
    return (value >> offset) & mask;
}

inline int32_t bitfieldExtract(int32_t value, int offset, int bits) {
    uint32_t mask = (1u << bits) - 1u;
    int32_t result = (value >> offset) & mask;
    // Sign extend
    if (result & (1 << (bits - 1))) {
        result |= ~mask;
    }
    return result;
}

// bitfieldInsert
inline uint32_t bitfieldInsert(uint32_t base, uint32_t insert, int offset, int bits) {
    uint32_t mask = ((1u << bits) - 1u) << offset;
    return (base & ~mask) | ((insert << offset) & mask);
}

// bitfieldReverse
inline uint32_t bitfieldReverse(uint32_t x) {
    x = ((x & 0x55555555u) << 1) | ((x & 0xAAAAAAAAu) >> 1);
    x = ((x & 0x33333333u) << 2) | ((x & 0xCCCCCCCCu) >> 2);
    x = ((x & 0x0F0F0F0Fu) << 4) | ((x & 0xF0F0F0F0u) >> 4);
    x = ((x & 0x00FF00FFu) << 8) | ((x & 0xFF00FF00u) >> 8);
    return (x << 16) | (x >> 16);
}

// findLSB (returns -1 if x == 0)
inline int findLSB(uint32_t x) {
    if (x == 0) return -1;
    return __builtin_ctz(x);
}

// findMSB (returns -1 if x == 0)
inline int findMSB(uint32_t x) {
    if (x == 0) return -1;
    return 31 - __builtin_clz(x);
}
```

### Float/Int Bit Casting

| GLSL | C++ (GLM) |
|------|-----------|
| `floatBitsToInt(x)` | `glm::floatBitsToInt(x)` |
| `floatBitsToUint(x)` | `glm::floatBitsToUint(x)` |
| `intBitsToFloat(x)` | `glm::intBitsToFloat(x)` |
| `uintBitsToFloat(x)` | `glm::uintBitsToFloat(x)` |

Or use `std::bit_cast<>` (C++20):
```cpp
float f = 3.14f;
uint32_t u = std::bit_cast<uint32_t>(f);
```

### Packing/Unpacking

| GLSL | C++ (GLM) | Header |
|------|-----------|--------|
| `packSnorm2x16(v)` | `glm::packSnorm2x16(v)` | `<glm/gtc/packing.hpp>` |
| `unpackSnorm2x16(p)` | `glm::unpackSnorm2x16(p)` | |
| `packUnorm2x16(v)` | `glm::packUnorm2x16(v)` | |
| `unpackUnorm2x16(p)` | `glm::unpackUnorm2x16(p)` | |
| `packHalf2x16(v)` | `glm::packHalf2x16(v)` | |
| `unpackHalf2x16(p)` | `glm::unpackHalf2x16(p)` | |

## Buffer/Texture Access

### SSBO Access

```glsl
// GLSL
layout(set = 0, binding = 0) buffer NodeBuffer {
    uint nodes[];
};
uint node = nodes[index];
```

```cpp
// C++
std::span<const uint32_t> m_nodes;
uint32_t node = m_nodes[index];
```

### Image Load/Store

```glsl
// GLSL
layout(set = 0, binding = 1, rgba8) uniform image2D outputImage;
imageStore(outputImage, ivec2(x, y), color);
vec4 c = imageLoad(outputImage, ivec2(x, y));
```

```cpp
// C++
std::vector<glm::vec4> m_outputImage;
uint32_t m_width, m_height;

void imageStore(glm::ivec2 coord, glm::vec4 color) {
    if (coord.x >= 0 && coord.x < m_width &&
        coord.y >= 0 && coord.y < m_height) {
        m_outputImage[coord.y * m_width + coord.x] = color;
    }
}

glm::vec4 imageLoad(glm::ivec2 coord) {
    if (coord.x >= 0 && coord.x < m_width &&
        coord.y >= 0 && coord.y < m_height) {
        return m_outputImage[coord.y * m_width + coord.x];
    }
    return glm::vec4(0.0f);
}
```

### Texture Sampling (Simplified)

```cpp
// Simplified nearest-neighbor sampling
glm::vec4 textureSample(const Texture2D& tex, glm::vec2 uv) {
    int x = int(uv.x * tex.width) % tex.width;
    int y = int(uv.y * tex.height) % tex.height;
    return tex.pixels[y * tex.width + x];
}
```

## Built-in Variables

### Compute Shader

| GLSL | C++ Mirror |
|------|------------|
| `gl_GlobalInvocationID` | Pass as parameter: `execute(glm::uvec3 globalId, ...)` |
| `gl_LocalInvocationID` | Pass as parameter if needed |
| `gl_WorkGroupID` | Pass as parameter if needed |
| `gl_WorkGroupSize` | Define as constant in class |
| `gl_NumWorkGroups` | Pass as parameter if needed |

### Fragment Shader

| GLSL | C++ Mirror |
|------|------------|
| `gl_FragCoord` | Pass as parameter |
| `gl_FrontFacing` | Pass as parameter |
| `gl_PointCoord` | Pass as parameter |

## Swizzling

GLM supports GLSL-style swizzling:

```cpp
#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>

glm::vec4 v(1, 2, 3, 4);
glm::vec3 xyz = v.xyz();  // Note: function call syntax in GLM
glm::vec2 xy = v.xy();
glm::vec4 wzyx = v.wzyx();

// Or use glm::swizzle
glm::vec3 xyz = glm::vec3(v.x, v.y, v.z);
```

## Atomic Operations

| GLSL | C++ |
|------|-----|
| `atomicAdd(mem, val)` | `std::atomic<uint32_t>::fetch_add(val)` |
| `atomicMin(mem, val)` | Custom with compare_exchange |
| `atomicMax(mem, val)` | Custom with compare_exchange |
| `atomicAnd(mem, val)` | `std::atomic<uint32_t>::fetch_and(val)` |
| `atomicOr(mem, val)` | `std::atomic<uint32_t>::fetch_or(val)` |
| `atomicXor(mem, val)` | `std::atomic<uint32_t>::fetch_xor(val)` |
| `atomicExchange(mem, val)` | `std::atomic<uint32_t>::exchange(val)` |
| `atomicCompSwap(mem, cmp, val)` | `std::atomic<uint32_t>::compare_exchange_strong(cmp, val)` |

Note: For single-threaded mirror testing, atomics can be replaced with regular operations.
