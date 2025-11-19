#pragma once

#include <memory>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <string>
#include "MortonCode.h"

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
// Cache Budget Report
// ============================================================================

struct CacheBudgetReport {
    size_t brickSizeBytes;          // Total brick size in bytes
    size_t cacheBudgetBytes;        // User-specified cache budget
    size_t bytesRemaining;          // Remaining cache space (if fits)
    size_t bytesOverBudget;         // Overflow amount (if exceeds)
    bool fitsInCache;               // true if brick ≤ budget
    float utilizationPercent;       // Cache utilization (0-100+)

    std::string toString() const;
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
 * Cache-aware object-of-arrays brick storage with flat allocation.
 *
 * Bricks are dense n³ voxel grids where n = 2^depth.
 * Data stored as flat arrays (not std::vector) for zero overhead.
 *
 * Cache-aware design:
 * - User specifies cache budget (e.g., 32KB for L1, 256KB for L2)
 * - Reports if brick fits in cache or predicts misses
 * - Supports arbitrary array count (not limited to 8)
 *
 * Template parameter defines data layout:
 *   struct MyBrickData {
 *       static constexpr size_t numArrays = 3;
 *       using Array0Type = float;     // density
 *       using Array1Type = uint32_t;  // material
 *       using Array2Type = uint16_t;  // normal
 *   };
 *
 * Usage:
 *   BrickStorage<MyData> storage(depth=3, capacity=1024, cacheBytes=32768);
 *   auto report = storage.getCacheBudgetReport();
 *   if (!report.fitsInCache) {
 *       // Handle cache miss prediction
 *   }
 *
 *   uint32_t brickID = storage.allocateBrick();
 *   storage.set<0>(brickID, localIdx, value);
 */
template<typename BrickDataLayout>
class BrickStorage {
public:
    // Expose type helper
    template<size_t Idx>
    using ArrayType = GetArrayType_t<BrickDataLayout, Idx>;

    /**
     * Construct brick storage with cache budget.
     *
     * @param depthLevels Brick depth (1-10) → brick size = 2^depth
     * @param initialCapacity Initial brick count
     * @param cacheBudgetBytes Cache size in bytes (e.g., 32768 for 32KB L1)
     *                         Use 0 to disable cache validation
     * @param indexOrder Voxel indexing strategy (default: Morton for best cache performance)
     */
    explicit BrickStorage(int depthLevels,
                         size_t initialCapacity = 256,
                         size_t cacheBudgetBytes = 0,
                         BrickIndexOrder indexOrder = BrickIndexOrder::Morton)
        : m_depth(depthLevels)
        , m_sideLength(1 << depthLevels)
        , m_voxelsPerBrick(m_sideLength * m_sideLength * m_sideLength)
        , m_capacity(initialCapacity)
        , m_brickCount(0)
        , m_cacheBudgetBytes(cacheBudgetBytes)
        , m_indexOrder(indexOrder)
        , m_mortonIndex(depthLevels)
    {
        static_assert(BrickDataLayout::numArrays > 0, "BrickDataLayout must define at least 1 array");

        if (depthLevels < 1 || depthLevels > 10) {
            throw std::invalid_argument("Brick depth must be 1-10 (2³-1024³ voxels)");
        }

        // Allocate all arrays
        allocateArrays(std::make_index_sequence<BrickDataLayout::numArrays>{});
    }

    ~BrickStorage() {
        // Free all arrays
        freeArrays(std::make_index_sequence<BrickDataLayout::numArrays>{});
    }

    // Brick allocation
    uint32_t allocateBrick() {
        if (m_brickCount >= m_capacity) {
            grow();
        }
        return m_brickCount++;
    }

    // Get/Set value in array N for brick at local voxel index
    template<size_t ArrayIdx>
    GetArrayType_t<BrickDataLayout, ArrayIdx> get(uint32_t brickID, size_t localVoxelIdx) const {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");
        validateAccess(brickID, localVoxelIdx);

        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;
        const T* array = static_cast<const T*>(m_arrays[ArrayIdx]);
        return array[brickID * m_voxelsPerBrick + localVoxelIdx];
    }

    template<size_t ArrayIdx>
    void set(uint32_t brickID, size_t localVoxelIdx, GetArrayType_t<BrickDataLayout, ArrayIdx> value) {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");
        validateAccess(brickID, localVoxelIdx);

        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;
        T* array = static_cast<T*>(m_arrays[ArrayIdx]);
        array[brickID * m_voxelsPerBrick + localVoxelIdx] = value;
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
    size_t getBrickCount() const { return m_brickCount; }
    size_t getCapacity() const { return m_capacity; }

    // Cache budget analysis
    CacheBudgetReport getCacheBudgetReport() const {
        size_t brickSize = calculateBrickSizeBytes();

        CacheBudgetReport report;
        report.brickSizeBytes = brickSize;
        report.cacheBudgetBytes = m_cacheBudgetBytes;

        if (m_cacheBudgetBytes == 0) {
            // No budget specified
            report.fitsInCache = true;
            report.bytesRemaining = 0;
            report.bytesOverBudget = 0;
            report.utilizationPercent = 0.0f;
        } else if (brickSize <= m_cacheBudgetBytes) {
            // Fits in cache
            report.fitsInCache = true;
            report.bytesRemaining = m_cacheBudgetBytes - brickSize;
            report.bytesOverBudget = 0;
            report.utilizationPercent = (brickSize * 100.0f) / m_cacheBudgetBytes;
        } else {
            // Cache miss predicted
            report.fitsInCache = false;
            report.bytesRemaining = 0;
            report.bytesOverBudget = brickSize - m_cacheBudgetBytes;
            report.utilizationPercent = (brickSize * 100.0f) / m_cacheBudgetBytes;
        }

        return report;
    }

    // Raw array access for GPU upload (array N)
    template<size_t ArrayIdx>
    const void* getArrayData() const {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");
        return m_arrays[ArrayIdx];
    }

    template<size_t ArrayIdx>
    size_t getArraySizeBytes() const {
        static_assert(ArrayIdx < BrickDataLayout::numArrays, "Array index out of bounds");
        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;
        return m_brickCount * m_voxelsPerBrick * sizeof(T);
    }

private:
    // Calculate single brick size (all arrays)
    size_t calculateBrickSizeBytes() const {
        return calculateBrickSizeBytesImpl(std::make_index_sequence<BrickDataLayout::numArrays>{});
    }

    template<size_t... Indices>
    size_t calculateBrickSizeBytesImpl(std::index_sequence<Indices...>) const {
        return (... + (m_voxelsPerBrick * sizeof(GetArrayType_t<BrickDataLayout, Indices>)));
    }

    // Allocate single flat array
    template<size_t ArrayIdx>
    void allocateArray() {
        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;
        m_arrays[ArrayIdx] = static_cast<void*>(new T[m_capacity * m_voxelsPerBrick]{});
    }

    // Allocate all arrays
    template<size_t... Indices>
    void allocateArrays(std::index_sequence<Indices...>) {
        (allocateArray<Indices>(), ...);
    }

    // Free single array
    template<size_t ArrayIdx>
    void freeArray() {
        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;
        delete[] static_cast<T*>(m_arrays[ArrayIdx]);
        m_arrays[ArrayIdx] = nullptr;
    }

    // Free all arrays
    template<size_t... Indices>
    void freeArrays(std::index_sequence<Indices...>) {
        (freeArray<Indices>(), ...);
    }

    // Grow capacity
    void grow() {
        size_t newCapacity = m_capacity * 2;
        growArrays(newCapacity, std::make_index_sequence<BrickDataLayout::numArrays>{});
        m_capacity = newCapacity;
    }

    template<size_t... Indices>
    void growArrays(size_t newCapacity, std::index_sequence<Indices...>) {
        (growArray<Indices>(newCapacity), ...);
    }

    template<size_t ArrayIdx>
    void growArray(size_t newCapacity) {
        using T = GetArrayType_t<BrickDataLayout, ArrayIdx>;

        // Allocate new larger array
        T* newArray = new T[newCapacity * m_voxelsPerBrick]{};

        // Copy existing data
        T* oldArray = static_cast<T*>(m_arrays[ArrayIdx]);
        std::copy(oldArray, oldArray + m_brickCount * m_voxelsPerBrick, newArray);

        // Free old array
        delete[] oldArray;

        m_arrays[ArrayIdx] = newArray;
    }

    void validateAccess(uint32_t brickID, size_t localVoxelIdx) const {
        if (brickID >= m_brickCount) {
            throw std::out_of_range("Brick ID exceeds allocated count");
        }
        if (localVoxelIdx >= m_voxelsPerBrick) {
            throw std::out_of_range("Local voxel index exceeds brick size");
        }
    }

    int m_depth;                     // Depth levels (3 → 8³ brick)
    int m_sideLength;                // Voxels per side (2^depth)
    size_t m_voxelsPerBrick;         // Total voxels (sideLength³)
    size_t m_capacity;               // Current brick capacity
    size_t m_brickCount;             // Active brick count
    size_t m_cacheBudgetBytes;       // Cache size budget (0 = no limit)
    BrickIndexOrder m_indexOrder;    // Voxel indexing strategy
    MortonBrickIndex m_mortonIndex;  // Morton encoding/decoding helper

    // Flat array storage: m_arrays[N] = T* where T = ArrayNType
    // No vector overhead, direct pointer arithmetic
    void* m_arrays[16] = {nullptr}; // Support up to 16 arrays (can be increased)
};

// ============================================================================
// Cache Budget Report Implementation
// ============================================================================

inline std::string CacheBudgetReport::toString() const {
    char buffer[512];
    if (cacheBudgetBytes == 0) {
        snprintf(buffer, sizeof(buffer),
                 "Brick size: %zu bytes (no cache budget specified)",
                 brickSizeBytes);
    } else if (fitsInCache) {
        snprintf(buffer, sizeof(buffer),
                 "✓ Brick fits in cache\n"
                 "  Brick size:     %zu bytes\n"
                 "  Cache budget:   %zu bytes\n"
                 "  Remaining:      %zu bytes (%.1f%% utilized)",
                 brickSizeBytes, cacheBudgetBytes, bytesRemaining, utilizationPercent);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "⚠ Cache miss predicted\n"
                 "  Brick size:     %zu bytes\n"
                 "  Cache budget:   %zu bytes\n"
                 "  Over budget:    %zu bytes (%.1f%% overflow)",
                 brickSizeBytes, cacheBudgetBytes, bytesOverBudget, utilizationPercent);
    }
    return std::string(buffer);
}

// ============================================================================
// Default Leaf Data: Density + Material
// ============================================================================

/**
 * Default brick data layout: density (float) + material ID (uint32_t).
 * Total per voxel: 8 bytes
 * 8³ brick = 512 voxels = 4KB (fits in L1 cache)
 */
struct DefaultLeafData {
    static constexpr size_t numArrays = 2;

    using Array0Type = float;     // Density [0,1]
    using Array1Type = uint32_t;  // Material ID

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
