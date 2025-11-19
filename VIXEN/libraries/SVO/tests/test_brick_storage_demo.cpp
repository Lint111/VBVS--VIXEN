/**
 * Demo: Cache-aware brick storage
 *
 * Shows how to use BrickStorage with cache budget analysis
 * to optimize voxel data layouts for hardware cache sizes.
 */

#include "BrickStorage.h"
#include <iostream>

using namespace SVO;

// Example: Extended PBR layout with many arrays
struct ExtendedPBRData {
    static constexpr size_t numArrays = 10;

    using Array0Type = float;      // Density
    using Array1Type = uint32_t;   // Albedo RGB (packed)
    using Array2Type = uint8_t;    // Metallic
    using Array3Type = uint8_t;    // Roughness
    using Array4Type = uint16_t;   // Normal (packed)
    using Array5Type = uint8_t;    // Ambient occlusion
    using Array6Type = uint8_t;    // Emissive R
    using Array7Type = uint8_t;    // Emissive G
    using Array8Type = uint8_t;    // Emissive B
    using Array9Type = uint16_t;   // Material flags

    // Unused (supports up to 16)
    using Array10Type = void;
    using Array11Type = void;
    using Array12Type = void;
    using Array13Type = void;
    using Array14Type = void;
    using Array15Type = void;
};

int main() {
    std::cout << "=== Brick Storage Cache Budget Demo ===\n\n";

    // Typical cache sizes
    constexpr size_t L1_CACHE = 32 * 1024;   // 32KB
    constexpr size_t L2_CACHE = 256 * 1024;  // 256KB

    // Test 1: Default layout (density + material)
    std::cout << "1. Default Layout (2 arrays: float + uint32):\n";
    DefaultBrickStorage defaultStorage(3, 1024, L1_CACHE); // 8³ brick
    auto report1 = defaultStorage.getCacheBudgetReport();
    std::cout << report1.toString() << "\n\n";

    // Test 2: Extended PBR layout (10 arrays)
    std::cout << "2. Extended PBR Layout (10 arrays):\n";
    BrickStorage<ExtendedPBRData> pbrStorage(3, 1024, L1_CACHE); // 8³ brick
    auto report2 = pbrStorage.getCacheBudgetReport();
    std::cout << report2.toString() << "\n\n";

    // Test 3: Larger brick size
    std::cout << "3. Larger Brick (16³ voxels, default layout):\n";
    BrickStorage<DefaultLeafData> largeStorage(4, 1024, L1_CACHE); // 16³ brick
    auto report3 = largeStorage.getCacheBudgetReport();
    std::cout << report3.toString() << "\n\n";

    // Test 4: L2 cache comparison
    std::cout << "4. Same Large Brick, L2 Cache Budget:\n";
    BrickStorage<DefaultLeafData> l2Storage(4, 1024, L2_CACHE); // 16³ brick
    auto report4 = l2Storage.getCacheBudgetReport();
    std::cout << report4.toString() << "\n\n";

    // Demonstrate usage
    std::cout << "5. Example Usage:\n";
    uint32_t brickID = defaultStorage.allocateBrick();
    size_t centerIdx = defaultStorage.getIndex(4, 4, 4);
    defaultStorage.set<0>(brickID, centerIdx, 0.8f);      // density
    defaultStorage.set<1>(brickID, centerIdx, 42u);       // material

    std::cout << "  Allocated brick " << brickID << "\n";
    std::cout << "  Set center voxel: density=" << defaultStorage.get<0>(brickID, centerIdx)
              << ", material=" << defaultStorage.get<1>(brickID, centerIdx) << "\n\n";

    std::cout << "=== Demo Complete ===\n";
    return 0;
}
