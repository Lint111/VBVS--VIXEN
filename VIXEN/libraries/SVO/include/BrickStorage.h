#pragma once

#include <memory>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <string>
#include "MortonCode.h"
#include "AttributeRegistry.h"  // NEW: VoxelData integration
#include "AttributeStorage.h"
#include "BrickView.h"

namespace SVO {

// ============================================================================
// Brick Indexing Strategies
// ============================================================================

/**
 * Brick voxel indexing strategies for different access patterns.
 *
 * LinearXYZ: Row-major (Z-major) ordering
 *   - idx = x + y*N + z*N²
 *   - Simple, predictable
 *   - Poor cache locality for Z-axis traversal
 *   - Best for: Sequential Z-slice processing
 *
 * LinearZYX: Z-minor (X-major) ordering
 *   - idx = z + y*N + x*N²
 *   - Better for column-based access
 *   - Best for: Vertical ray marching
 *
 * Morton: Z-order space-filling curve
 *   - Interleaved bit pattern
 *   - Excellent spatial locality (2-3× cache hit improvement)
 *   - Best for: DDA traversal, spatial queries, general-purpose
 *
 * Hilbert: Hilbert space-filling curve (future)
 *   - Even better locality than Morton (continuous curve)
 *   - More complex encoding
 *   - Best for: Maximum cache coherence
 */
enum class BrickIndexOrder {
    LinearXYZ,  // x + y*N + z*N² (default for compatibility)
    LinearZYX,  // z + y*N + x*N² (vertical coherence)
    Morton,     // Space-filling curve (2-3× better cache hits)
    // Hilbert  // Reserved for future implementation
};

// ============================================================================
// Type Extraction Helpers
// ============================================================================

// Extract array type at compile time
template<typename Layout, size_t Idx> struct GetArrayType;
template<typename Layout> struct GetArrayType<Layout, 0> { using type = typename Layout::Array0Type; };
template<typename Layout> struct GetArrayType<Layout, 1> { using type = typename Layout::Array1Type; };
template<typename Layout> struct GetArrayType<Layout, 2> { using type = typename Layout::Array2Type; };
template<typename Layout> struct GetArrayType<Layout, 3> { using type = typename Layout::Array3Type; };
template<typename Layout> struct GetArrayType<Layout, 4> { using type = typename Layout::Array4Type; };
template<typename Layout> struct GetArrayType<Layout, 5> { using type = typename Layout::Array5Type; };
template<typename Layout> struct GetArrayType<Layout, 6> { using type = typename Layout::Array6Type; };
template<typename Layout> struct GetArrayType<Layout, 7> { using type = typename Layout::Array7Type; };
template<typename Layout> struct GetArrayType<Layout, 8> { using type = typename Layout::Array8Type; };
template<typename Layout> struct GetArrayType<Layout, 9> { using type = typename Layout::Array9Type; };
template<typename Layout> struct GetArrayType<Layout, 10> { using type = typename Layout::Array10Type; };
template<typename Layout> struct GetArrayType<Layout, 11> { using type = typename Layout::Array11Type; };
template<typename Layout> struct GetArrayType<Layout, 12> { using type = typename Layout::Array12Type; };
template<typename Layout> struct GetArrayType<Layout, 13> { using type = typename Layout::Array13Type; };
template<typename Layout> struct GetArrayType<Layout, 14> { using type = typename Layout::Array14Type; };
template<typename Layout> struct GetArrayType<Layout, 15> { using type = typename Layout::Array15Type; };

template<typename Layout, size_t Idx>
using GetArrayType_t = typename GetArrayType<Layout, Idx>::type;

// ============================================================================
// Brick Storage Class (Cache-Aware, Flat Arrays)
// ============================================================================

/**
 * Cache-aware brick storage backed by AttributeRegistry.
 *
 * Bricks are dense n³ voxel grids where n = 2^depth.
 * Data managed by AttributeRegistry (VoxelData library).
 *
 * NEW (Post-Migration):
 * - Delegates to AttributeRegistry for storage
 * - Maps template indices to attribute names via BrickDataLayout::attributeNames
 * - Keeps compile-time indexed API for ray traversal performance
 * - BrickView handles actual data access behind the scenes
 *
 * Template parameter defines data layout:
 *   struct MyBrickData {
 *       static constexpr size_t numArrays = 2;
 *       using Array0Type = float;     // density
 *       using Array1Type = uint32_t;  // material
 *       static constexpr const char* attributeNames[numArrays] = {"density", "material"};
 *   };
 *
 * Usage:
 *   auto registry = std::make_shared<AttributeRegistry>();
 *   registry->registerKey("density", AttributeType::Float, 0.0f);
 *   registry->addAttribute("material", AttributeType::Uint32, 0u);
 *
 *   BrickStorage<MyData> storage(&registry, depth=3, indexOrder);
 *   uint32_t brickID = storage.allocateBrick();
 *   storage.set<0>(brickID, localIdx, value);  // Delegates to BrickView
 */
template<typename BrickDataLayout>
class BrickStorage {
public:
    // Expose type helper
    template<size_t Idx>
    using ArrayType = GetArrayType_t<BrickDataLayout, Idx>;

    /**
     * Construct brick storage backed by AttributeRegistry.
     *
     * @param registry AttributeRegistry managing voxel data (non-owning)
     * @param depthLevels Brick depth (1-10) → brick size = 2^depth
     * @param indexOrder Voxel indexing strategy (default: Morton for best cache performance)
     */
    explicit BrickStorage(VoxelData::AttributeRegistry* registry,
                         int depthLevels,
                         BrickIndexOrder indexOrder = BrickIndexOrder::Morton)
        : m_registry(registry)
        , m_depth(depthLevels)
        , m_sideLength(1 << depthLevels)
        , m_voxelsPerBrick(m_sideLength * m_sideLength * m_sideLength)
        , m_indexOrder(indexOrder)
        , m_mortonIndex(depthLevels)
    {
        static_assert(BrickDataLayout::numArrays > 0, "BrickDataLayout must define at least 1 array");

        if (!registry) {
            throw std::invalid_argument("AttributeRegistry cannot be null");
        }

        if (depthLevels < 1 || depthLevels > 10) {
            throw std::invalid_argument("Brick depth must be 1-10 (2³-1024³ voxels)");
        }

        // Verify all attributes from layout are registered
        for (size_t i = 0; i < BrickDataLayout::numArrays; ++i) {
            const char* attrName = BrickDataLayout::attributeNames[i];
            if (!m_registry->hasAttribute(attrName)) {
                throw std::invalid_argument(std::string("Attribute '") + attrName + "' not registered in AttributeRegistry");
            }
        }
    }

    ~BrickStorage() = default;

    // Brick allocation - delegates to AttributeRegistry
    uint32_t allocateBrick() {
        return m_registry->allocateBrick();
    }

    // Get/Set value in array N for brick at local voxel index
    // Maps compile-time array index → attribute name → BrickView access
    template<size_t ArrayIdx>
    GetArrayType_t<BrickDataLayout, ArrayIdx> get(uint32_t brickID, size_t localVoxelIdx) const {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");

        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;
        const char* attrName = BrickDataLayout::attributeNames[ArrayIdx];

        VoxelData::BrickView brick = m_registry->getBrick(brickID);
        return brick.get<T>(attrName, localVoxelIdx);
    }

    template<size_t ArrayIdx>
    void set(uint32_t brickID, size_t localVoxelIdx, GetArrayType_t<BrickDataLayout, ArrayIdx> value) {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");

        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;
        const char* attrName = BrickDataLayout::attributeNames[ArrayIdx];

        VoxelData::BrickView brick = m_registry->getBrick(brickID);
        brick.set<T>(attrName, localVoxelIdx, value);
    }

    /**
     * Convert 3D coordinates to flat index.
     * Internal packing order is transparent to external users.
     *
     * @param x, y, z Voxel coordinates (0-based, within brick)
     * @return Flat index into brick's data arrays
     */
    size_t getIndex(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0 ||
            x >= m_sideLength || y >= m_sideLength || z >= m_sideLength) {
            throw std::out_of_range("Brick coordinates out of range");
        }

        switch (m_indexOrder) {
            case BrickIndexOrder::Morton:
                // Z-order space-filling curve (best cache locality)
                return m_mortonIndex.getIndex(x, y, z);

            case BrickIndexOrder::LinearZYX:
                // Z-minor ordering (x-major): z + y*N + x*N²
                return static_cast<size_t>(z + y * m_sideLength + x * m_sideLength * m_sideLength);

            case BrickIndexOrder::LinearXYZ:
            default:
                // Row-major ordering: x + y*N + z*N²
                return static_cast<size_t>(x + y * m_sideLength + z * m_sideLength * m_sideLength);
        }
    }

    /**
     * Convert flat index back to 3D coordinates.
     * Inverse of getIndex().
     */
    void getCoord(size_t flatIndex, int& x, int& y, int& z) const {
        switch (m_indexOrder) {
            case BrickIndexOrder::Morton:
                m_mortonIndex.getCoord(flatIndex, x, y, z);
                break;

            case BrickIndexOrder::LinearZYX:
                // z + y*N + x*N²
                x = flatIndex / (m_sideLength * m_sideLength);
                flatIndex -= x * m_sideLength * m_sideLength;
                y = flatIndex / m_sideLength;
                z = flatIndex % m_sideLength;
                break;

            case BrickIndexOrder::LinearXYZ:
            default:
                // x + y*N + z*N²
                z = flatIndex / (m_sideLength * m_sideLength);
                flatIndex -= z * m_sideLength * m_sideLength;
                y = flatIndex / m_sideLength;
                x = flatIndex % m_sideLength;
                break;
        }
    }

    // Accessors
    int getDepth() const { return m_depth; }
    int getSideLength() const { return m_sideLength; }
    size_t getVoxelsPerBrick() const { return m_voxelsPerBrick; }
    size_t getBrickCount() const { return m_registry->getBrickCount(); }

    // Get AttributeRegistry (for advanced usage)
    VoxelData::AttributeRegistry* getRegistry() { return m_registry; }
    const VoxelData::AttributeRegistry* getRegistry() const { return m_registry; }

    // Raw array access for GPU upload (delegates to AttributeStorage)
    template<size_t ArrayIdx>
    const void* getArrayData() const {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");
        const char* attrName = BrickDataLayout::attributeNames[ArrayIdx];
        const VoxelData::AttributeStorage* storage = m_registry->getStorage(attrName);
        return storage ? storage->getGPUBuffer() : nullptr;
    }

    template<size_t ArrayIdx>
    size_t getArraySizeBytes() const {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");
        const char* attrName = BrickDataLayout::attributeNames[ArrayIdx];
        const VoxelData::AttributeStorage* storage = m_registry->getStorage(attrName);
        return storage ? storage->getSizeBytes() : 0;
    }

private:
    VoxelData::AttributeRegistry* m_registry;  // Non-owning pointer
    int m_depth;                               // Depth levels (3 → 8³ brick)
    int m_sideLength;                          // Voxels per side (2^depth)
    size_t m_voxelsPerBrick;                   // Total voxels (sideLength³)
    BrickIndexOrder m_indexOrder;              // Voxel indexing strategy
    MortonBrickIndex m_mortonIndex;            // Morton encoding/decoding helper
};

// ============================================================================
// Default Leaf Data: Density + Material
// ============================================================================

/**
 * Default brick data layout: density (float) + material ID (uint32_t).
 * Total per voxel: 8 bytes
 * 8³ brick = 512 voxels = 4KB (fits in L1 cache)
 *
 * NEW: Maps template indices to VoxelData attribute names for AttributeRegistry integration
 */
struct DefaultLeafData {
    static constexpr size_t numArrays = 2;

    using Array0Type = float;     // Density [0,1]
    using Array1Type = uint32_t;  // Material ID

    // Attribute name mapping (template index → VoxelData attribute name)
    static constexpr const char* attributeNames[numArrays] = {"density", "material"};

    // Unused slots (required for template)
    using Array2Type = void;
    using Array3Type = void;
    using Array4Type = void;
    using Array5Type = void;
    using Array6Type = void;
    using Array7Type = void;
    using Array8Type = void;
    using Array9Type = void;
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

using DefaultBrickStorage = BrickStorage<DefaultLeafData>;

} // namespace SVO
