#pragma once

#include <gaia.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <variant>
#include <tuple>
#include <cstdint>
#include <iostream>

namespace GaiaVoxel {

/**
 * ECS Components for voxel data using Gaia's native multi-member support.
 *
 * Design:
 * - Vec3 types use {x,y,z} or {r,g,b} members (Gaia handles SoA internally)
 * - Macro generates component + trait metadata
 * - Natural glm::vec3 conversion via helper functions
 */

// ============================================================================
// Component Definition Macros (Simplified - Gaia Handles Layout)
// ============================================================================

// Scalar component (single value)
#define VOXEL_COMPONENT_SCALAR(ComponentName, LogicalName, Type, DefaultVal) \
    struct ComponentName { \
        static constexpr const char* Name = LogicalName; \
        Type value = DefaultVal; \
    };

// Vec3 component (float only, Gaia controls layout)
// Layout: AoS or SoA (passed directly to GAIA_LAYOUT macro)
#define VOXEL_COMPONENT_VEC3(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2) \
    struct ComponentName { \
        static constexpr const char* Name = LogicalName; \
        static constexpr const char* Suffixes[3] = {#S0, #S1, #S2}; \
        GAIA_LAYOUT(Layout); \
        \
        float S0 = D0; \
        float S1 = D1; \
        float S2 = D2; \
        \
        /* glm::vec3 conversion */ \
        ComponentName() = default; \
        ComponentName(const glm::vec3& v) : S0(v[0]), S1(v[1]), S2(v[2]) {} \
        operator glm::vec3() const { return glm::vec3(S0, S1, S2); } \
        glm::vec3 toVec3() const { return glm::vec3(S0, S1, S2); } \
    };

// Macro for integer vec3 components (glm::ivec3)
#define VOXEL_COMPONENT_IVEC3(ComponentName, LogicalName, S0, S1, S2, Layout, D0, D1, D2) \
    struct ComponentName { \
        static constexpr const char* Name = LogicalName; \
        static constexpr const char* Suffixes[3] = {#S0, #S1, #S2}; \
        GAIA_LAYOUT(Layout); \
        \
        int S0 = D0; \
        int S1 = D1; \
        int S2 = D2; \
        \
        /* glm::ivec3 conversion */ \
        ComponentName() = default; \
        ComponentName(const glm::ivec3& v) : S0(v[0]), S1(v[1]), S2(v[2]) {} \
        operator glm::ivec3() const { return glm::ivec3(S0, S1, S2); } \
        glm::ivec3 toIVec3() const { return glm::ivec3(S0, S1, S2); } \
    };

// ============================================================================
// Spatial Indexing
// ============================================================================

/**
 * Morton code - encodes 3D position in single uint64.
 * 8 bytes vs 12 bytes for glm::vec3.
 *
 * Plain struct (no member functions) for Gaia-ECS compatibility.
 * Use MortonKeyUtils free functions for encode/decode operations.
 */
struct MortonKey {
    static constexpr const char* Name = "position";
    uint64_t code = 0;
};

// Helper functions for MortonKey (pure functions, ECS system-friendly)
namespace MortonKeyUtils {
    // Decode Morton code to grid position
    glm::ivec3 decode(uint64_t code);
    glm::ivec3 decode(const MortonKey& key);

    // Decode to world position
    glm::vec3 toWorldPos(uint64_t code);
    glm::vec3 toWorldPos(const MortonKey& key);

    // Encode position to Morton code
    uint64_t encode(const glm::vec3& pos);
    uint64_t encode(const glm::ivec3& pos);

    // Create MortonKey from position (factory functions)
    MortonKey fromPosition(const glm::vec3& pos);
    MortonKey fromPosition(const glm::ivec3& pos);
}

// ============================================================================
// Spatial Transform
// ============================================================================

/**
 * Transform component - generic local-to-world transformation.
 *
 * Generic transform for any entity in the scene (meshes, cameras, volumes, etc.).
 * Makes NO assumptions about local space bounds - local space can be [-∞,∞].
 *
 * Examples:
 * - Mesh: local space may be arbitrary (e.g., [-100, 100])
 * - Camera: local space is view frustum
 * - Volume: use VolumeTransform specialization for [0,1]³ normalized space
 *
 * Usage:
 *   Transform xform;
 *   xform.localToWorld = glm::translate(...) * glm::rotate(...) * glm::scale(...);
 *   glm::vec3 worldPos = xform.toWorld(localPos);
 *   glm::vec3 localPos = xform.toLocal(worldPos);
 */
struct Transform {
    static constexpr const char* Name = "transform";

    glm::mat4 localToWorld = glm::mat4(1.0f);  // Local space → world space

    // Accessor: compute inverse on-demand
    virtual glm::mat4 getWorldToLocal() const {
        return glm::inverse(localToWorld);
    }

    // Transform point from world to local space
    virtual glm::vec3 toLocal(const glm::vec3& worldPos) const {
        return glm::vec3(getWorldToLocal() * glm::vec4(worldPos, 1.0f));
    }

    // Transform point from local to world space
    virtual glm::vec3 toWorld(const glm::vec3& localPos) const {
        return glm::vec3(localToWorld * glm::vec4(localPos, 1.0f));
    }

    // Transform direction vector (no translation)
    virtual glm::vec3 dirToLocal(const glm::vec3& worldDir) const {
        return glm::mat3(getWorldToLocal()) * worldDir;
    }

    virtual glm::vec3 dirToWorld(const glm::vec3& localDir) const {
        return glm::mat3(localToWorld) * localDir;
    }
};

/**
 * VolumeTransform - specialized transform for volumetric data structures.
 *
 * Enforces normalized [0,1]³ local space for:
 * - Sparse Voxel Octrees (SVO)
 * - 3D textures and volume grids
 * - Signed Distance Fields (SDF)
 *
 * Benefits of [0,1]³ normalized space:
 * - Simplified DDA: cell size = 1.0 / 2^level
 * - Perfect grid alignment: no floating-point drift
 * - Hardware-friendly: GPU textures use [0,1] coordinates
 * - Resolution-independent: change world bounds without rebuilding structure
 *
 * Usage:
 *   auto xform = VolumeTransform::fromWVolumeBounds(glm::vec3(-10), glm::vec3(10));
 *   glm::vec3 normPos = xform.worldToVolume(glm::vec3(5, 2, 2));  // → (0.75, 0.6, 0.6)
 *   glm::vec3 worldPos = xform.volumeToWorld(glm::vec3(0.5, 0.5, 0.5));  // → (0, 0, 0)
 */
struct VolumeTransform : public Transform {
    // Factory: create volume transform from world AABB
    // Local space is always [0,1]³, world space is [worldMin, worldMax]
    static VolumeTransform fromWorldBounds(const glm::vec3& worldMin, const glm::vec3& worldMax) {
        VolumeTransform xform;

        // localToWorld: scale [0,1]³ to world size, then translate to worldMin
        glm::vec3 scale = worldMax - worldMin;
        xform.localToWorld = glm::translate(glm::mat4(1.0f), worldMin) * glm::scale(glm::mat4(1.0f), scale);

        return xform;
    }

    // Override: world → volume space with [0,1]³ clamping
    // Positions outside world bounds are clamped to volume edges
    glm::vec3 toLocal(const glm::vec3& worldPos) const override {
        glm::vec3 volumePos = Transform::toLocal(worldPos);
        return glm::clamp(volumePos, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    // Override: volume → world space with [0,1]³ validation
    // Expects input in [0,1]³ range (asserts in debug, clamps in release)
    glm::vec3 toWorld(const glm::vec3& volumePos) const override {
        #ifndef NDEBUG
        // Debug: assert invariant (catch programmer errors)
        if (glm::any(glm::lessThan(volumePos, glm::vec3(0.0f))) ||
            glm::any(glm::greaterThan(volumePos, glm::vec3(1.0f)))) {
            std::cerr << "[VolumeTransform] ERROR: volumePos out of [0,1]³ bounds: ("
                      << volumePos.x << ", " << volumePos.y << ", " << volumePos.z << ")\n";
            // In debug builds, clamp but warn
        }
        #endif

        // Clamp to [0,1]³ for robustness (handles floating-point drift)
        glm::vec3 clampedPos = glm::clamp(volumePos, glm::vec3(0.0f), glm::vec3(1.0f));
        return Transform::toWorld(clampedPos);
    }
};

struct AABB {
    static constexpr const char* Name = "aabb";

    glm::vec3 min = glm::vec3(FLT_MAX);
    glm::vec3 max = glm::vec3(-FLT_MAX);

    bool isInitialized() const {
        return min.x != FLT_MAX && glm::all(glm::lessThanEqual(min, max));
    }

    glm::vec3 getSize() const {
        return isInitialized() ? max - min : glm::vec3(0.0f);
    }

    glm::vec3 getCenter() const {
        return isInitialized() ? (min + max) * 0.5f : glm::vec3(0.0f);
    }

    bool contains(const glm::vec3& point) const {
        if(!isInitialized()) {
            return false;
        }

        return glm::all(glm::lessThanEqual(min, point)) && glm::all(glm::lessThanEqual(point, max));
    }

    void expandToContain(const glm::vec3& point) {
        if(!isInitialized()) {
            min = point;
            max = point;
            return;
        }

        min = (glm::min)(min, point);
        max = (glm::max)(max, point);
    
    }

    void expandToContain(const AABB& other) {
        if(!other.isInitialized()) {
            return;
        }
        expandToContain(other.min);
        expandToContain(other.max);
    }

    bool contains(const AABB& other) {
        if(!isInitialized() || !other.isInitialized()) {
            return false;
        }

        return glm::all(glm::lessThanEqual(min, other.min)) && glm::all(glm::lessThanEqual(other.max, max));

    }
};


/**
 * Volume component - defines voxel volume parameters.
 *
 * Contains voxel size and helper for required depth calculation.
 *
 * Plain struct (no member functions) for Gaia-ECS compatibility.
 * Use free functions for getRequiredDepth() calculation if needed.
 */
struct Volume {
    float voxelSize = 1.0f;  // Size of a single voxel in world units

    static constexpr int MAX_DEPTH = 23; // Max depth to fit in 64-bit Morton code
    static constexpr int MIN_DEPTH = 1;  // Minimum depth
    static constexpr const char* Name = "volume";


    int getRequiredDepth(const AABB aabb) const {
        if(!aabb.isInitialized()) {
            return MIN_DEPTH;
        }
        glm::vec3 size = aabb.getSize();
        float maxExtent = (glm::max)(size.x, (glm::max)(size.y, size.z));
        int depth = static_cast<int>(std::ceil(std::log2(maxExtent / voxelSize)));
        return glm::clamp(depth, MIN_DEPTH, MAX_DEPTH);  // Clamp depth to reasonable range
    }
};

/**
 * VolumeGrid - Integer grid bounds for voxel volumes.
 *
 * COORDINATE SPACE HIERARCHY:
 *   Global Space (world) - continuous floats
 *       ↓ Entity Transform
 *   Local Space (entity-relative) - continuous floats (mesh vertices)
 *       ↓ Volume Quantization (this component)
 *   Volume Local Space - INTEGER GRID (quantized voxels)
 *       ↓ Normalization (gridMin/gridMax → [0,1]³)
 *   Normalized Volume Space - [0,1]³
 *       ↓ ESVO offset (+1)
 *   ESVO Space - [1,2]³
 *       ↓ Brick extraction
 *   Brick Local Space - 0..7 integer grid per brick
 *
 * BENEFITS:
 * - Clean separation: continuous geometry → quantized voxels
 * - No FP precision issues in volume space (it's integers)
 * - AABB defines grid extent, normalization is trivial
 * - Brick traversal naturally integer-based
 *
 * USAGE:
 *   VolumeGrid grid;
 *   grid.expandToContain(glm::ivec3(5, 3, 7));  // Add voxel at integer coord
 *   glm::vec3 normalized = grid.toNormalized(glm::ivec3(5, 3, 7));  // → [0,1]³
 *   glm::ivec3 gridPos = grid.toGrid(normalized);  // → integer coords
 */
struct VolumeGrid {
    static constexpr const char* Name = "volume_grid";

    // Integer grid bounds (inclusive min, exclusive max for standard range semantics)
    glm::ivec3 gridMin = glm::ivec3(INT_MAX);
    glm::ivec3 gridMax = glm::ivec3(INT_MIN);

    // Check if grid has been initialized with at least one point
    bool isInitialized() const {
        return gridMin.x != INT_MAX && glm::all(glm::lessThan(gridMin, gridMax));
    }

    // Get grid dimensions (number of voxels per axis)
    glm::ivec3 getGridSize() const {
        if (!isInitialized()) return glm::ivec3(0);
        return gridMax - gridMin;
    }

    // Get maximum extent (for power-of-2 padding)
    int getMaxExtent() const {
        glm::ivec3 size = getGridSize();
        return (glm::max)(size.x, (glm::max)(size.y, size.z));
    }

    // Get power-of-2 padded size (for octree alignment)
    int getPaddedExtent() const {
        int maxExt = getMaxExtent();
        if (maxExt <= 0) return 1;
        // Round up to next power of 2
        int pot = 1;
        while (pot < maxExt) pot <<= 1;
        return pot;
    }

    // Expand grid to contain a new integer coordinate
    void expandToContain(const glm::ivec3& gridPos) {
        if (!isInitialized()) {
            gridMin = gridPos;
            gridMax = gridPos + glm::ivec3(1);  // Exclusive max
            return;
        }
        gridMin = (glm::min)(gridMin, gridPos);
        gridMax = (glm::max)(gridMax, gridPos + glm::ivec3(1));
    }

    // Quantize world/local position to integer grid coordinate
    // Uses floor() for consistent grid cell assignment
    static glm::ivec3 quantize(const glm::vec3& worldPos) {
        return glm::ivec3(
            static_cast<int>(std::floor(worldPos.x)),
            static_cast<int>(std::floor(worldPos.y)),
            static_cast<int>(std::floor(worldPos.z))
        );
    }

    // Convert integer grid position to normalized [0,1]³ space
    // Uses power-of-2 padded extent for octree-aligned normalization
    glm::vec3 toNormalized(const glm::ivec3& gridPos) const {
        if (!isInitialized()) return glm::vec3(0.0f);

        int paddedExtent = getPaddedExtent();
        if (paddedExtent <= 0) return glm::vec3(0.0f);

        // Offset from grid minimum, then normalize by padded extent
        glm::vec3 offset = glm::vec3(gridPos - gridMin);
        return offset / static_cast<float>(paddedExtent);
    }

    // Convert normalized [0,1]³ position back to integer grid coordinate
    glm::ivec3 toGrid(const glm::vec3& normalized) const {
        if (!isInitialized()) return glm::ivec3(0);

        int paddedExtent = getPaddedExtent();
        glm::vec3 offset = normalized * static_cast<float>(paddedExtent);
        return gridMin + glm::ivec3(
            static_cast<int>(std::floor(offset.x)),
            static_cast<int>(std::floor(offset.y)),
            static_cast<int>(std::floor(offset.z))
        );
    }

    // Convert normalized [0,1]³ to ESVO [1,2]³ space
    static glm::vec3 toESVO(const glm::vec3& normalized) {
        return normalized + glm::vec3(1.0f);
    }

    // Convert ESVO [1,2]³ back to normalized [0,1]³
    static glm::vec3 fromESVO(const glm::vec3& esvo) {
        return esvo - glm::vec3(1.0f);
    }

    // Check if integer grid position is within bounds
    bool contains(const glm::ivec3& gridPos) const {
        if (!isInitialized()) return false;
        return glm::all(glm::greaterThanEqual(gridPos, gridMin)) &&
               glm::all(glm::lessThan(gridPos, gridMax));
    }

    // Get world-space AABB (assuming unit voxels, grid coords = world coords)
    AABB toWorldAABB() const {
        AABB aabb;
        if (isInitialized()) {
            aabb.min = glm::vec3(gridMin);
            aabb.max = glm::vec3(gridMax);
        }
        return aabb;
    }

    // Create VolumeGrid from world-space AABB (quantizes bounds)
    static VolumeGrid fromWorldAABB(const AABB& aabb) {
        VolumeGrid grid;
        if (aabb.isInitialized()) {
            grid.gridMin = quantize(aabb.min);
            grid.gridMax = quantize(aabb.max) + glm::ivec3(1);  // Exclusive max
        }
        return grid;
    }
};


// ============================================================================
// Core Voxel Attributes
// ============================================================================

// Scalar attributes
VOXEL_COMPONENT_SCALAR(Density, "density", float, 1.0f)
VOXEL_COMPONENT_SCALAR(Material, "material", uint32_t, 0)
VOXEL_COMPONENT_SCALAR(EmissionIntensity, "emission_intensity", float, 0.0f)

// Vec3 attributes with Gaia layout control
VOXEL_COMPONENT_VEC3(Color, "color", r, g, b, AoS, 1.0f, 1.0f, 1.0f)
VOXEL_COMPONENT_VEC3(Normal, "normal", x, y, z, AoS, 0.0f, 1.0f, 0.0f)
VOXEL_COMPONENT_VEC3(Emission, "emission", r, g, b, AoS, 0.0f, 0.0f, 0.0f)


// ============================================================================
// SINGLE SOURCE OF TRUTH - Component Registry
// ============================================================================

/**
 * Component access types:
 * - Value: Simple types (scalar, vec3) accessed via getComponentValue() returning extracted value
 * - Ref:   Complex types (struct with methods) accessed via getComponentRef() returning pointer
 */
enum class ComponentAccessType { Value, Ref };

/**
 * To register a new component:
 * 1. Define the component (VOXEL_COMPONENT_SCALAR or VOXEL_COMPONENT_VEC3)
 * 2. Add APPLY_MACRO(macro, ComponentName, AccessType) to FOR_EACH_COMPONENT below
 * 3. ComponentVariant, AllComponents tuple, and ComponentTraits auto-update
 *
 * This is the ONLY place you need to add component names!
 *
 * Access types:
 * - Value: Scalar/Vec3 components with simple value extraction (density, color, etc.)
 * - Ref:   Complex components returned by reference (Transform, AABB, etc.)
 */
#define APPLY_MACRO(macro, ...) macro(__VA_ARGS__)
#define FOR_EACH_COMPONENT(macro) \
    APPLY_MACRO(macro, Density, Value) \
    APPLY_MACRO(macro, Material, Value) \
    APPLY_MACRO(macro, EmissionIntensity, Value) \
    APPLY_MACRO(macro, Color, Value) \
    APPLY_MACRO(macro, Normal, Value) \
    APPLY_MACRO(macro, Emission, Value) \
    APPLY_MACRO(macro, MortonKey, Value) \
    APPLY_MACRO(macro, Transform, Ref) \
    APPLY_MACRO(macro, VolumeTransform, Ref) \
    APPLY_MACRO(macro, AABB, Ref) \
    APPLY_MACRO(macro, Volume, Ref) \
    APPLY_MACRO(macro, VolumeGrid, Ref)

// Helper macro to expand only Value components
#define FOR_EACH_VALUE_COMPONENT(macro) \
    APPLY_MACRO(macro, Density) \
    APPLY_MACRO(macro, Material) \
    APPLY_MACRO(macro, EmissionIntensity) \
    APPLY_MACRO(macro, Color) \
    APPLY_MACRO(macro, Normal) \
    APPLY_MACRO(macro, Emission) \
    APPLY_MACRO(macro, MortonKey)

// Helper macro to expand only Ref components
#define FOR_EACH_REF_COMPONENT(macro) \
    APPLY_MACRO(macro, Transform) \
    APPLY_MACRO(macro, VolumeTransform) \
    APPLY_MACRO(macro, AABB) \
    APPLY_MACRO(macro, Volume) \
    APPLY_MACRO(macro, VolumeGrid) 

namespace ComponentRegistry {
    // Visit all components with a lambda
    template<typename Visitor>
    constexpr void visitAll(Visitor&& visitor) {
        // Directly instantiate each component and call visitor
        // Note: Uses 2-arg macro (Component, AccessType) - ignores AccessType here
        #define VISIT_COMPONENT(Component, AccessType) visitor(Component{});
        FOR_EACH_COMPONENT(VISIT_COMPONENT)
        #undef VISIT_COMPONENT
    }

    // Visit only Value-type components (for DynamicVoxelScalar conversion, etc.)
    template<typename Visitor>
    constexpr void visitValueComponents(Visitor&& visitor) {
        #define VISIT_VALUE_COMPONENT(Component) visitor(Component{});
        FOR_EACH_VALUE_COMPONENT(VISIT_VALUE_COMPONENT)
        #undef VISIT_VALUE_COMPONENT
    }

    // Visit only Ref-type components (for complex type handling)
    template<typename Visitor>
    constexpr void visitRefComponents(Visitor&& visitor) {
        #define VISIT_REF_COMPONENT(Component) visitor(Component{});
        FOR_EACH_REF_COMPONENT(VISIT_REF_COMPONENT)
        #undef VISIT_REF_COMPONENT
    }

    // Find component by name (compile-time)
    template<typename Visitor>
    constexpr bool visitByName(std::string_view name, Visitor&& visitor) {
        bool found = false;
        visitAll([&](auto component) {
            using Component = std::decay_t<decltype(component)>;
            if (Component::Name == name) {
                visitor(component);
                found = true;
            }
        });
        return found;
    }

    // Find Value-type component by name
    template<typename Visitor>
    constexpr bool visitValueByName(std::string_view name, Visitor&& visitor) {
        bool found = false;
        visitValueComponents([&](auto component) {
            using Component = std::decay_t<decltype(component)>;
            if (Component::Name == name) {
                visitor(component);
                found = true;
            }
        });
        return found;
    }

} // namespace ComponentRegistry

// ============================================================================
// Component Variant Type
// ============================================================================

/**
 * Type-safe variant containing any registered component type.
 *
 * Automatically generated from FOR_EACH_COMPONENT macro registry.
 * Benefits:
 * - Zero string lookups - component type IS the identifier
 * - Type-safe - impossible to assign wrong value type
 * - Memory efficient - stores only ONE component at a time
 *
 * Memory layout:
 * - Size = sizeof(largest component) + sizeof(discriminator)
 * - Typical size: 16 bytes (12 for vec3 + 4 for discriminator)
 * - NOT a tuple (no allocation of all types simultaneously)
 *
 * Example:
 *   ComponentVariant v1 = Density{0.8f};       // holds Density only
 *   ComponentVariant v2 = Color{glm::vec3(1, 0, 0)};  // holds Color only
 */
// Note: Uses 2-arg macro (Component, AccessType) - ignores AccessType here
#define AS_VARIANT_TYPE(Component, AccessType) Component,
using ComponentVariant = std::variant<FOR_EACH_COMPONENT(AS_VARIANT_TYPE) std::monostate>;
#undef AS_VARIANT_TYPE

// ============================================================================
// Metadata Components
// ============================================================================

// NOTE: BrickReference REMOVED - Deprecated
// Brick storage is now a VIEW pattern (BrickView), not entity-stored.
// Dense brick data lives in contiguous arrays accessed via Morton offset + stride.
// See GaiaVoxelWorld.h for BrickView architecture.

/**
 * Chunk origin in world space - identifies spatial chunk (e.g., 8³ region).
 * Used for bulk voxel insertion and spatial query optimization.
 */
VOXEL_COMPONENT_IVEC3(ChunkOrigin, "chunk_origin", x, y, z, AoS, 0, 0, 0)

/**
 * Chunk metadata - references voxel data via offset into contiguous storage.
 *
 * Architecture: Chunks store OFFSET into global voxel entity array, not individual entities.
 * This enables:
 * - Cache-friendly iteration (contiguous entity IDs)
 * - Zero ChildOf relation overhead (no graph traversal)
 * - Direct indexing: voxelEntities[offset + localIdx]
 *
 * Memory: 12 bytes total (vs 8 bytes per voxel for ChildOf relations!)
 * For 512 voxels: 12 bytes vs 4096 bytes = 99.7% savings
 *
 * Format: chunkDepth^3 voxels (e.g., depth=8 → 8³ = 512 voxels)
 *
 * NOTE: Plain struct (no member functions) for Gaia-ECS compatibility.
 * Use free functions for voxelCount() calculation if needed.
 */
struct ChunkMetadata {
    static constexpr const char* Name = "chunk_metadata";

    uint32_t entityOffset = 0;  // Offset into global voxel entity array
    uint8_t  chunkDepth = 0;    // Chunk depth (8 = 8³ = 512 voxels, max 16 = 4096)
    uint8_t  flags = 0;         // Bit 0: isDirty, Bit 1-7: reserved
    uint16_t _reserved = 0;     // Reserved for future use
    uint32_t brickID = 0xFFFFFFFF; // SVO brick ID (0xFFFFFFFF if not allocated)
};

// Free functions for ChunkMetadata accessors
inline uint32_t getVoxelCount(const ChunkMetadata& chunk) {
    return static_cast<uint32_t>(chunk.chunkDepth) *
           static_cast<uint32_t>(chunk.chunkDepth) *
           static_cast<uint32_t>(chunk.chunkDepth);
}

inline bool isChunkDirty(const ChunkMetadata& chunk) {
    return (chunk.flags & 0x01) != 0;
}

inline void setChunkDirty(ChunkMetadata& chunk, bool dirty) {
    if (dirty) chunk.flags |= 0x01;
    else chunk.flags &= ~0x01;
}

/**
 * Chunk ID - spatial grouping (legacy, kept for compatibility).
 */
VOXEL_COMPONENT_SCALAR(ChunkID, "chunk_id", uint32_t, 0)

/**
 * Tag component - marks voxels that should be in octree.
 * Empty struct = zero memory overhead.
 */
struct Solid {};

// ============================================================================
// Component Traits (Auto-Generated from Single Source of Truth!)
// ============================================================================

template<typename T>
struct ComponentTraits;

// Reuse FOR_EACH_COMPONENT to generate traits
// Note: Uses 2-arg macro (Component, AccessType) - ignores AccessType here
#define GENERATE_TRAIT(Component, AccessType) \
    template<> \
    struct ComponentTraits<Component> { \
        static constexpr const char* Name = Component::Name; \
    };

FOR_EACH_COMPONENT(GENERATE_TRAIT)

#undef GENERATE_TRAIT

// Chunk components are metadata, not in main registry but need traits
template<>
struct ComponentTraits<ChunkOrigin> {
    static constexpr const char* Name = ChunkOrigin::Name;
};

template<>
struct ComponentTraits<ChunkMetadata> {
    static constexpr const char* Name = ChunkMetadata::Name;
};

template<>
struct ComponentTraits<ChunkID> {
    static constexpr const char* Name = ChunkID::Name;
};

// ============================================================================
// Type Detection (Automatic via C++20 Concepts)
// ============================================================================

// Detect scalar component (has .value member)
template<typename T>
concept HasValueMember = requires(T t) {
    { t.value };
};

// Detect vec3 component (has toVec3() method)
template<typename T>
concept HasToVec3Method = requires(const T t) {
    { t.toVec3() } -> std::convertible_to<glm::vec3>;
};

// ============================================================================
// Component Concepts (Type-Based Detection - No Bools!)
// ============================================================================

template<typename T>
concept VoxelComponent = requires {
    { ComponentTraits<T>::Name } -> std::convertible_to<const char*>;
};

// Vec3 component = has toVec3() method
template<typename T>
concept Vec3Component = VoxelComponent<T> && HasToVec3Method<T>;

// Scalar component = has .value member
template<typename T>
concept ScalarComponent = VoxelComponent<T> && HasValueMember<T>;

// Component value type extraction (for template signatures)
// Primary template - for scalar components with .value member
template<typename T, typename = void>
struct ComponentValueType {
    using type = decltype(std::declval<T>().value);
};

// Specialization for Vec3 components (have toVec3() method)
template<typename T>
struct ComponentValueType<T, std::void_t<decltype(std::declval<T>().toVec3())>> {
    using type = glm::vec3;
};

// Specialization for MortonKey (spatial indexing, not a regular component)
template<>
struct ComponentValueType<MortonKey> {
    using type = uint64_t;  // MortonKey stores a code
};

// Specializations for complex types (return themselves as values)
// These types are accessed via getComponentRef() for full functionality
template<>
struct ComponentValueType<Transform> {
    using type = Transform;  // Complex type - no simple value extraction
};

template<>
struct ComponentValueType<VolumeTransform> {
    using type = VolumeTransform;  // Complex type - no simple value extraction
};

template<>
struct ComponentValueType<AABB> {
    using type = AABB;  // Complex type - no simple value extraction
};

template<>
struct ComponentValueType<Volume> {
    using type = Volume;  // Complex type - has voxelSize but also methods
};

template<>
struct ComponentValueType<VolumeGrid> {
    using type = VolumeGrid;  // Complex type - has gridMin/Max but also methods
};

template<typename T>
using ComponentValueType_t = typename ComponentValueType<T>::type;

// Concept for complex component types (Transform, AABB, etc.)
template<typename T>
concept ComplexComponent = std::is_same_v<T, Transform> ||
                           std::is_same_v<T, VolumeTransform> ||
                           std::is_same_v<T, AABB> ||
                           std::is_same_v<T, Volume> ||
                           std::is_same_v<T, VolumeGrid>;

// Get component value (works for scalar, vec3, MortonKey, and complex types)
template<VoxelComponent T>
auto getValue(const T& component) {
    if constexpr (std::is_same_v<T, MortonKey>) {
        return component.code;  // MortonKey stores code
    } else if constexpr (ComplexComponent<T>) {
        return component;  // Complex types return themselves
    } else if constexpr (Vec3Component<T>) {
        return component.toVec3();
    } else {
        return component.value;
    }
}

// Set component value (works for both scalar and vec3)
template<ScalarComponent T, typename ValueType>
void setValue(T& component, const ValueType& value) {
    component.value = value;
}

template<Vec3Component T>
void setValue(T& component, const glm::vec3& value) {
    component = T(value);  // Uses conversion constructor
}

} // namespace GaiaVoxel
