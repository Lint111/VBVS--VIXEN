//#include <gtest/gtest.h>
//#include "BrickStorage.h"
//#include <glm/glm.hpp>
//
//using namespace SVO;
//
//// ============================================================================
//// Test Custom Brick Layouts
//// ============================================================================
//
//// Custom layout: RGB color only
//struct ColorOnlyBrick {
//    static constexpr size_t numArrays = 3;
//    using Array0Type = uint8_t;  // R
//    using Array1Type = uint8_t;  // G
//    using Array2Type = uint8_t;  // B
//    using Array3Type = void;
//    using Array4Type = void;
//    using Array5Type = void;
//    using Array6Type = void;
//    using Array7Type = void;
//};
//
//// Custom layout: Single float (SDF distance field)
//struct SDFBrick {
//    static constexpr size_t numArrays = 1;
//    using Array0Type = float;    // Signed distance
//    using Array1Type = void;
//    using Array2Type = void;
//    using Array3Type = void;
//    using Array4Type = void;
//    using Array5Type = void;
//    using Array6Type = void;
//    using Array7Type = void;
//};
//
//// Custom layout: Full physically-based data
//struct PBRBrick {
//    static constexpr size_t numArrays = 5;
//    using Array0Type = float;    // Density
//    using Array1Type = uint32_t; // Packed albedo RGB
//    using Array2Type = uint8_t;  // Metallic
//    using Array3Type = uint8_t;  // Roughness
//    using Array4Type = uint16_t; // Packed normal
//    using Array5Type = void;
//    using Array6Type = void;
//    using Array7Type = void;
//};
//
//// ============================================================================
//// Basic Allocation and Indexing Tests
//// ============================================================================
//
//TEST(BrickStorageTest, ConstructionParameters) {
//    DefaultBrickStorage storage(3); // 8³ bricks
//
//    EXPECT_EQ(storage.getDepth(), 3);
//    EXPECT_EQ(storage.getSideLength(), 8);
//    EXPECT_EQ(storage.getVoxelsPerBrick(), 512); // 8³
//    EXPECT_EQ(storage.getBrickCount(), 0);
//}
//
//TEST(BrickStorageTest, BrickSizeCalculation) {
//    // Test various depths
//    EXPECT_EQ(BrickStorage<DefaultLeafData>(1).getVoxelsPerBrick(), 8);      // 2³
//    EXPECT_EQ(BrickStorage<DefaultLeafData>(2).getVoxelsPerBrick(), 64);     // 4³
//    EXPECT_EQ(BrickStorage<DefaultLeafData>(3).getVoxelsPerBrick(), 512);    // 8³
//    EXPECT_EQ(BrickStorage<DefaultLeafData>(4).getVoxelsPerBrick(), 4096);   // 16³
//}
//
//TEST(BrickStorageTest, AllocateBrick) {
//    DefaultBrickStorage storage(3);
//
//    uint32_t brick0 = storage.allocateBrick();
//    uint32_t brick1 = storage.allocateBrick();
//    uint32_t brick2 = storage.allocateBrick();
//
//    EXPECT_EQ(brick0, 0);
//    EXPECT_EQ(brick1, 1);
//    EXPECT_EQ(brick2, 2);
//    EXPECT_EQ(storage.getBrickCount(), 3);
//}
//
//TEST(BrickStorageTest, Index3DConversion) {
//    // Test with LINEAR ordering
//    DefaultBrickStorage storage(3, 256, 0, BrickIndexOrder::LinearXYZ); // 8x8x8
//
//    // Corner voxels
//    EXPECT_EQ(storage.getIndex(0, 0, 0), 0);
//    EXPECT_EQ(storage.getIndex(7, 7, 7), 511); // 8³-1
//
//    // Edge cases
//    EXPECT_EQ(storage.getIndex(1, 0, 0), 1);
//    EXPECT_EQ(storage.getIndex(0, 1, 0), 8);
//    EXPECT_EQ(storage.getIndex(0, 0, 1), 64);
//
//    // Center voxel
//    EXPECT_EQ(storage.getIndex(4, 4, 4), 4 + 4*8 + 4*64);
//}
//
//TEST(BrickStorageTest, Index3DOutOfBounds) {
//    DefaultBrickStorage storage(3); // 8x8x8
//
//    EXPECT_THROW(storage.getIndex(-1, 0, 0), std::out_of_range);
//    EXPECT_THROW(storage.getIndex(0, -1, 0), std::out_of_range);
//    EXPECT_THROW(storage.getIndex(0, 0, -1), std::out_of_range);
//    EXPECT_THROW(storage.getIndex(8, 0, 0), std::out_of_range);
//    EXPECT_THROW(storage.getIndex(0, 8, 0), std::out_of_range);
//    EXPECT_THROW(storage.getIndex(0, 0, 8), std::out_of_range);
//}
//
//// ============================================================================
//// Data Access Tests - DefaultLeafData (Density + Material)
//// ============================================================================
//
//TEST(BrickStorageTest, DefaultLeafData_SetAndGet) {
//    DefaultBrickStorage storage(3);
//    uint32_t brickID = storage.allocateBrick();
//
//    // Set density and material for corner voxel
//    size_t idx = storage.getIndex(0, 0, 0);
//    storage.set<0>(brickID, idx, 0.75f);      // density
//    storage.set<1>(brickID, idx, 42u);        // material ID
//
//    // Retrieve
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, idx), 0.75f);
//    EXPECT_EQ(storage.get<1>(brickID, idx), 42u);
//}
//
//TEST(BrickStorageTest, DefaultLeafData_MultipleBricks) {
//    DefaultBrickStorage storage(3);
//    uint32_t brick0 = storage.allocateBrick();
//    uint32_t brick1 = storage.allocateBrick();
//
//    size_t centerIdx = storage.getIndex(4, 4, 4);
//
//    // Write to brick 0
//    storage.set<0>(brick0, centerIdx, 1.0f);
//    storage.set<1>(brick0, centerIdx, 100u);
//
//    // Write to brick 1
//    storage.set<0>(brick1, centerIdx, 0.5f);
//    storage.set<1>(brick1, centerIdx, 200u);
//
//    // Verify isolation
//    EXPECT_FLOAT_EQ(storage.get<0>(brick0, centerIdx), 1.0f);
//    EXPECT_EQ(storage.get<1>(brick0, centerIdx), 100u);
//    EXPECT_FLOAT_EQ(storage.get<0>(brick1, centerIdx), 0.5f);
//    EXPECT_EQ(storage.get<1>(brick1, centerIdx), 200u);
//}
//
//TEST(BrickStorageTest, DefaultLeafData_FillBrick) {
//    DefaultBrickStorage storage(2); // 4³=64 voxels
//    uint32_t brickID = storage.allocateBrick();
//
//    // Fill brick with gradient pattern
//    for (int z = 0; z < 4; ++z) {
//        for (int y = 0; y < 4; ++y) {
//            for (int x = 0; x < 4; ++x) {
//                size_t idx = storage.getIndex(x, y, z);
//                float density = (x + y + z) / 9.0f; // [0, 1]
//                uint32_t material = x + y * 4 + z * 16;
//
//                storage.set<0>(brickID, idx, density);
//                storage.set<1>(brickID, idx, material);
//            }
//        }
//    }
//
//    // Verify
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, storage.getIndex(0, 0, 0)), 0.0f);
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, storage.getIndex(3, 3, 3)), 1.0f);
//    EXPECT_EQ(storage.get<1>(brickID, storage.getIndex(1, 2, 3)), 1 + 2*4 + 3*16);
//}
//
//// ============================================================================
//// Custom Layout Tests
//// ============================================================================
//
//TEST(BrickStorageTest, ColorOnlyBrick_RGB) {
//    BrickStorage<ColorOnlyBrick> storage(3);
//    uint32_t brickID = storage.allocateBrick();
//
//    size_t idx = storage.getIndex(4, 4, 4);
//    storage.set<0>(brickID, idx, uint8_t(255)); // R
//    storage.set<1>(brickID, idx, uint8_t(128)); // G
//    storage.set<2>(brickID, idx, uint8_t(64));  // B
//
//    EXPECT_EQ(storage.get<0>(brickID, idx), 255);
//    EXPECT_EQ(storage.get<1>(brickID, idx), 128);
//    EXPECT_EQ(storage.get<2>(brickID, idx), 64);
//}
//
//TEST(BrickStorageTest, SDFBrick_SignedDistance) {
//    BrickStorage<SDFBrick> storage(3);
//    uint32_t brickID = storage.allocateBrick();
//
//    // Store SDF values (negative inside, positive outside)
//    storage.set<0>(brickID, storage.getIndex(0, 0, 0), -0.5f);
//    storage.set<0>(brickID, storage.getIndex(7, 7, 7), 1.0f);
//
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, storage.getIndex(0, 0, 0)), -0.5f);
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, storage.getIndex(7, 7, 7)), 1.0f);
//}
//
//TEST(BrickStorageTest, PBRBrick_AllArrays) {
//    BrickStorage<PBRBrick> storage(2); // 4³ for faster test
//    uint32_t brickID = storage.allocateBrick();
//
//    size_t idx = storage.getIndex(2, 2, 2);
//
//    // Set all PBR data
//    storage.set<0>(brickID, idx, 0.8f);                     // density
//    storage.set<1>(brickID, idx, 0xFF8040u);                // albedo (packed RGB)
//    storage.set<2>(brickID, idx, uint8_t(128));             // metallic
//    storage.set<3>(brickID, idx, uint8_t(64));              // roughness
//    storage.set<4>(brickID, idx, uint16_t(32768));          // normal (packed)
//
//    // Verify
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, idx), 0.8f);
//    EXPECT_EQ(storage.get<1>(brickID, idx), 0xFF8040u);
//    EXPECT_EQ(storage.get<2>(brickID, idx), uint8_t(128));
//    EXPECT_EQ(storage.get<3>(brickID, idx), uint8_t(64));
//    EXPECT_EQ(storage.get<4>(brickID, idx), uint16_t(32768));
//}
//
//// ============================================================================
//// Cache Budget Tests
//// ============================================================================
//
//TEST(BrickStorageTest, CacheBudget_NoBudget) {
//    DefaultBrickStorage storage(3); // No budget specified
//
//    auto report = storage.getCacheBudgetReport();
//
//    EXPECT_TRUE(report.fitsInCache);
//    EXPECT_EQ(report.cacheBudgetBytes, 0);
//    EXPECT_GT(report.brickSizeBytes, 0);
//}
//
//TEST(BrickStorageTest, CacheBudget_FitsInL1) {
//    // 8³ brick with 2 arrays (float + uint32_t) = 512 * 8 = 4096 bytes
//    // L1 cache = 32KB → should fit
//    DefaultBrickStorage storage(3, 256, 32768);
//
//    auto report = storage.getCacheBudgetReport();
//
//    EXPECT_TRUE(report.fitsInCache);
//    EXPECT_EQ(report.brickSizeBytes, 512 * (sizeof(float) + sizeof(uint32_t)));
//    EXPECT_EQ(report.cacheBudgetBytes, 32768);
//    EXPECT_GT(report.bytesRemaining, 0);
//    EXPECT_EQ(report.bytesOverBudget, 0);
//    EXPECT_LT(report.utilizationPercent, 100.0f);
//}
//
//TEST(BrickStorageTest, CacheBudget_ExceedsCache) {
//    // 8³ brick = 4096 bytes, but budget only 2KB
//    DefaultBrickStorage storage(3, 256, 2048);
//
//    auto report = storage.getCacheBudgetReport();
//
//    EXPECT_FALSE(report.fitsInCache);
//    EXPECT_EQ(report.cacheBudgetBytes, 2048);
//    EXPECT_GT(report.bytesOverBudget, 0);
//    EXPECT_EQ(report.bytesRemaining, 0);
//    EXPECT_GT(report.utilizationPercent, 100.0f);
//}
//
//TEST(BrickStorageTest, CacheBudget_LargerBrick) {
//    // 16³ brick = 4096 voxels = 32KB
//    BrickStorage<DefaultLeafData> storage(4, 256, 32768);
//
//    auto report = storage.getCacheBudgetReport();
//
//    EXPECT_EQ(report.brickSizeBytes, 4096 * (sizeof(float) + sizeof(uint32_t)));
//    // Should fit exactly or nearly
//}
//
//TEST(BrickStorageTest, CacheBudget_ReportToString) {
//    DefaultBrickStorage storage(3, 256, 32768);
//
//    auto report = storage.getCacheBudgetReport();
//    std::string str = report.toString();
//
//    EXPECT_FALSE(str.empty());
//    // Should contain key info
//    EXPECT_NE(str.find("bytes"), std::string::npos);
//}
//
//// ============================================================================
//// Capacity and Growth Tests
//// ============================================================================
//
//TEST(BrickStorageTest, AutomaticGrowth) {
//    DefaultBrickStorage storage(3, 2); // Initial capacity: 2 bricks
//
//    uint32_t brick0 = storage.allocateBrick();
//    uint32_t brick1 = storage.allocateBrick();
//    EXPECT_EQ(storage.getCapacity(), 2);
//
//    // Trigger growth
//    uint32_t brick2 = storage.allocateBrick();
//    EXPECT_EQ(storage.getCapacity(), 4);
//    EXPECT_EQ(storage.getBrickCount(), 3);
//
//    // Data survives growth
//    size_t idx = storage.getIndex(0, 0, 0);
//    storage.set<0>(brick0, idx, 0.1f);
//    storage.set<0>(brick1, idx, 0.2f);
//    storage.set<0>(brick2, idx, 0.3f);
//
//    EXPECT_FLOAT_EQ(storage.get<0>(brick0, idx), 0.1f);
//    EXPECT_FLOAT_EQ(storage.get<0>(brick1, idx), 0.2f);
//    EXPECT_FLOAT_EQ(storage.get<0>(brick2, idx), 0.3f);
//}
//
//TEST(BrickStorageTest, LargeAllocation) {
//    DefaultBrickStorage storage(3, 1024);
//
//    // Allocate many bricks
//    for (int i = 0; i < 500; ++i) {
//        storage.allocateBrick();
//    }
//
//    EXPECT_EQ(storage.getBrickCount(), 500);
//}
//
//// ============================================================================
//// GPU Buffer Access Tests
//// ============================================================================
//
//TEST(BrickStorageTest, RawArrayAccess) {
//    DefaultBrickStorage storage(2); // 4³=64 voxels
//    uint32_t brick0 = storage.allocateBrick();
//    uint32_t brick1 = storage.allocateBrick();
//
//    // Fill with test data
//    for (size_t i = 0; i < 64; ++i) {
//        storage.set<0>(brick0, i, static_cast<float>(i));
//        storage.set<1>(brick0, i, static_cast<uint32_t>(i * 2));
//        storage.set<0>(brick1, i, static_cast<float>(i + 100));
//        storage.set<1>(brick1, i, static_cast<uint32_t>(i * 2 + 1));
//    }
//
//    // Get raw pointers
//    const void* densityArray = storage.getArrayData<0>();
//    const void* materialArray = storage.getArrayData<1>();
//
//    EXPECT_NE(densityArray, nullptr);
//    EXPECT_NE(materialArray, nullptr);
//
//    // Verify size
//    EXPECT_EQ(storage.getArraySizeBytes<0>(), 2 * 64 * sizeof(float));
//    EXPECT_EQ(storage.getArraySizeBytes<1>(), 2 * 64 * sizeof(uint32_t));
//
//    // Verify data via raw access
//    const float* densities = static_cast<const float*>(densityArray);
//    const uint32_t* materials = static_cast<const uint32_t*>(materialArray);
//
//    EXPECT_FLOAT_EQ(densities[0], 0.0f);        // brick0[0]
//    EXPECT_FLOAT_EQ(densities[64], 100.0f);     // brick1[0]
//    EXPECT_EQ(materials[0], 0u);                // brick0[0]
//    EXPECT_EQ(materials[64], 1u);               // brick1[0]
//}
//
//// ============================================================================
//// Domain-Specific Layout Tests
//// ============================================================================
//
//#include "BrickLayouts.h"
//
//TEST(BrickStorageTest, SoundPropagation_Layout) {
//    BrickStorage<SoundPropagationBrick> storage(3, 256, 32768); // L1 cache
//
//    uint32_t brickID = storage.allocateBrick();
//    size_t idx = storage.getIndex(0, 0, 0);
//
//    // Set acoustic properties
//    storage.set<0>(brickID, idx, 2400.0f);  // Density (concrete)
//    storage.set<1>(brickID, idx, 0.35f);    // Absorption
//    storage.set<2>(brickID, idx, 0.60f);    // Reflection
//    storage.set<3>(brickID, idx, 0.05f);    // Transmission
//
//    // Verify
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, idx), 2400.0f);
//    EXPECT_FLOAT_EQ(storage.get<1>(brickID, idx), 0.35f);
//    EXPECT_FLOAT_EQ(storage.get<2>(brickID, idx), 0.60f);
//    EXPECT_FLOAT_EQ(storage.get<3>(brickID, idx), 0.05f);
//
//    // Check cache budget
//    auto report = storage.getCacheBudgetReport();
//    EXPECT_TRUE(report.fitsInCache); // 8KB should fit in 32KB L1
//}
//
//TEST(BrickStorageTest, AINavigation_MinimalFootprint) {
//    BrickStorage<AINavigationBrick> storage(3, 1024, 32768);
//
//    uint32_t brickID = storage.allocateBrick();
//    size_t idx = storage.getIndex(4, 4, 4);
//
//    // Set navigation data (very compact: 4 bytes/voxel)
//    storage.set<0>(brickID, idx, uint8_t(255)); // Fully walkable
//    storage.set<1>(brickID, idx, uint8_t(10));  // Low cost
//    storage.set<2>(brickID, idx, uint8_t(128)); // Medium cover
//    storage.set<3>(brickID, idx, uint8_t(0xFF));// Fully visible
//
//    EXPECT_EQ(storage.get<0>(brickID, idx), 255);
//    EXPECT_EQ(storage.get<1>(brickID, idx), 10);
//    EXPECT_EQ(storage.get<2>(brickID, idx), 128);
//    EXPECT_EQ(storage.get<3>(brickID, idx), 0xFF);
//
//    // Check cache efficiency (only 2KB per brick!)
//    auto report = storage.getCacheBudgetReport();
//    EXPECT_EQ(report.brickSizeBytes, 512 * 4); // 2048 bytes
//    EXPECT_LT(report.utilizationPercent, 10.0f); // <10% L1 usage
//}
//
//TEST(BrickStorageTest, OccupancyBrick_UltraCompact) {
//    BrickStorage<OccupancyBrick> storage(3, 2048, 32768);
//
//    uint32_t brickID = storage.allocateBrick();
//
//    // Fill with checkerboard pattern
//    for (int z = 0; z < 8; ++z) {
//        for (int y = 0; y < 8; ++y) {
//            for (int x = 0; x < 8; ++x) {
//                size_t idx = storage.getIndex(x, y, z);
//                uint8_t occupied = (x + y + z) % 2;
//                storage.set<0>(brickID, idx, occupied);
//            }
//        }
//    }
//
//    // Verify pattern
//    EXPECT_EQ(storage.get<0>(brickID, storage.getIndex(0, 0, 0)), 0);
//    EXPECT_EQ(storage.get<0>(brickID, storage.getIndex(1, 0, 0)), 1);
//    EXPECT_EQ(storage.get<0>(brickID, storage.getIndex(0, 1, 0)), 1);
//
//    // Check ultra-compact size (only 512 bytes!)
//    auto report = storage.getCacheBudgetReport();
//    EXPECT_EQ(report.brickSizeBytes, 512);
//    EXPECT_LT(report.utilizationPercent, 2.0f); // <2% L1 usage
//}
//
//TEST(BrickStorageTest, FluidSimulation_MultiArray) {
//    BrickStorage<FluidSimulationBrick> storage(3, 256, 32768);
//
//    uint32_t brickID = storage.allocateBrick();
//    size_t idx = storage.getIndex(2, 3, 4);
//
//    // Set fluid properties
//    storage.set<0>(brickID, idx, 1000.0f);         // Density (water)
//    storage.set<1>(brickID, idx, uint16_t(12345)); // Vel X
//    storage.set<2>(brickID, idx, uint16_t(23456)); // Vel Y
//    storage.set<3>(brickID, idx, uint16_t(34567)); // Vel Z
//    storage.set<4>(brickID, idx, 101325.0f);       // Pressure (1 atm)
//
//    EXPECT_FLOAT_EQ(storage.get<0>(brickID, idx), 1000.0f);
//    EXPECT_EQ(storage.get<1>(brickID, idx), 12345);
//    EXPECT_EQ(storage.get<2>(brickID, idx), 23456);
//    EXPECT_EQ(storage.get<3>(brickID, idx), 34567);
//    EXPECT_FLOAT_EQ(storage.get<4>(brickID, idx), 101325.0f);
//}
//
//// ============================================================================
//// Morton Code Tests
//// ============================================================================
//
//#include "MortonCode.h"
//
//TEST(BrickStorageTest, MortonCode_BasicEncoding) {
//    // Test Morton encoding for small coordinates
//    EXPECT_EQ(encodeMorton(0, 0, 0), 0);
//    EXPECT_EQ(encodeMorton(1, 0, 0), 1);   // x=1 → 001
//    EXPECT_EQ(encodeMorton(0, 1, 0), 2);   // y=1 → 010
//    EXPECT_EQ(encodeMorton(0, 0, 1), 4);   // z=1 → 100
//    EXPECT_EQ(encodeMorton(1, 1, 1), 7);   // xyz=111 → 111
//
//    // Verify (2, 3, 4) encoding
//    uint32_t morton234 = encodeMorton(2, 3, 4);
//    uint32_t x, y, z;
//    decodeMorton(morton234, x, y, z);
//    EXPECT_EQ(x, 2);
//    EXPECT_EQ(y, 3);
//    EXPECT_EQ(z, 4);
//}
//
//TEST(BrickStorageTest, MortonCode_Decoding) {
//    uint32_t x, y, z;
//
//    decodeMorton(0, x, y, z);
//    EXPECT_EQ(x, 0); EXPECT_EQ(y, 0); EXPECT_EQ(z, 0);
//
//    decodeMorton(7, x, y, z);
//    EXPECT_EQ(x, 1); EXPECT_EQ(y, 1); EXPECT_EQ(z, 1);
//
//    decodeMorton(94, x, y, z);
//    EXPECT_EQ(x, 6); EXPECT_EQ(y, 3); EXPECT_EQ(z, 1); // 94 = 0b1011110 → x=bits[0,3,6]=110=6, y=bits[1,4]=11=3, z=bits[2,5]=01=1
//}
//
//TEST(BrickStorageTest, MortonCode_RoundTrip) {
//    // Test encode → decode round trip
//    for (uint32_t x = 0; x < 8; ++x) {
//        for (uint32_t y = 0; y < 8; ++y) {
//            for (uint32_t z = 0; z < 8; ++z) {
//                uint32_t morton = encodeMorton(x, y, z);
//                uint32_t dx, dy, dz;
//                decodeMorton(morton, dx, dy, dz);
//                EXPECT_EQ(dx, x);
//                EXPECT_EQ(dy, y);
//                EXPECT_EQ(dz, z);
//            }
//        }
//    }
//}
//
//TEST(BrickStorageTest, MortonOrder_SpatialLocality) {
//    // Morton order keeps spatially nearby points close in memory
//    // Example: (0,0,0) and (0,0,1) should have adjacent Morton codes
//
//    uint32_t m000 = encodeMorton(0, 0, 0);
//    uint32_t m001 = encodeMorton(0, 0, 1);
//    uint32_t m010 = encodeMorton(0, 1, 0);
//    uint32_t m100 = encodeMorton(1, 0, 0);
//
//    // All neighbors within same octant should be close
//    EXPECT_LT(std::abs(static_cast<int>(m001 - m000)), 8);
//    EXPECT_LT(std::abs(static_cast<int>(m010 - m000)), 8);
//    EXPECT_LT(std::abs(static_cast<int>(m100 - m000)), 8);
//}
//
//TEST(BrickStorageTest, MortonOrder_UsageInBrick) {
//    // Compare Morton vs Linear ordering in brick storage
//    DefaultBrickStorage mortonStorage(3, 256, 32768, BrickIndexOrder::Morton);
//    DefaultBrickStorage linearStorage(3, 256, 32768, BrickIndexOrder::LinearXYZ);
//
//    // Same 3D coordinates should map to different flat indices
//    size_t mortonIdx = mortonStorage.getIndex(0, 0, 1);
//    size_t linearIdx = linearStorage.getIndex(0, 0, 1);
//
//    EXPECT_EQ(mortonIdx, 4);   // Morton(0,0,1) = 4
//    EXPECT_EQ(linearIdx, 64);  // Linear: 0 + 0*8 + 1*64 = 64
//
//    // Morton should show better locality
//    EXPECT_LT(mortonIdx, linearIdx); // Morton indices stay smaller/closer
//}
//
//TEST(BrickStorageTest, MortonOrder_DataIntegrity) {
//    // Ensure Morton ordering doesn't break data access
//    DefaultBrickStorage storage(3, 256, 32768, BrickIndexOrder::Morton);
//    uint32_t brickID = storage.allocateBrick();
//
//    // Write checkerboard pattern
//    for (int z = 0; z < 8; ++z) {
//        for (int y = 0; y < 8; ++y) {
//            for (int x = 0; x < 8; ++x) {
//                size_t idx = storage.getIndex(x, y, z);
//                float value = (x + y + z) % 2 == 0 ? 1.0f : 0.0f;
//                storage.set<0>(brickID, idx, value);
//            }
//        }
//    }
//
//    // Verify pattern (read back using same coordinates)
//    for (int z = 0; z < 8; ++z) {
//        for (int y = 0; y < 8; ++y) {
//            for (int x = 0; x < 8; ++x) {
//                size_t idx = storage.getIndex(x, y, z);
//                float expected = (x + y + z) % 2 == 0 ? 1.0f : 0.0f;
//                EXPECT_FLOAT_EQ(storage.get<0>(brickID, idx), expected);
//            }
//        }
//    }
//}
//
//// ============================================================================
//// Edge Case Tests
//// ============================================================================
//
//TEST(BrickStorageTest, InvalidDepth) {
//    EXPECT_THROW(BrickStorage<DefaultLeafData>(0), std::invalid_argument);
//    EXPECT_THROW(BrickStorage<DefaultLeafData>(11), std::invalid_argument);
//}
//
//TEST(BrickStorageTest, AccessInvalidBrick) {
//    DefaultBrickStorage storage(3);
//    storage.allocateBrick(); // Allocate brick 0 only
//
//    // Try to access non-existent brick 1
//    EXPECT_THROW(storage.get<0>(1, 0), std::out_of_range);
//    EXPECT_THROW(storage.set<0>(1, 0, 0.5f), std::out_of_range);
//}
//
//TEST(BrickStorageTest, AccessInvalidVoxel) {
//    DefaultBrickStorage storage(3); // 8³=512 voxels
//    uint32_t brickID = storage.allocateBrick();
//
//    EXPECT_THROW(storage.get<0>(brickID, 512), std::out_of_range);
//    EXPECT_THROW(storage.set<0>(brickID, 512, 0.5f), std::out_of_range);
//}
//
//// ============================================================================
//// Object-of-Arrays Memory Layout Verification
//// ============================================================================
//
//TEST(BrickStorageTest, OoA_MemoryLayout) {
//    DefaultBrickStorage storage(2, 2); // 4³ voxels, 2 bricks
//    storage.allocateBrick();
//    storage.allocateBrick();
//
//    // Fill density array with identifiable pattern
//    for (uint32_t brickID = 0; brickID < 2; ++brickID) {
//        for (size_t i = 0; i < 64; ++i) {
//            storage.set<0>(brickID, i, static_cast<float>(brickID * 1000 + i));
//        }
//    }
//
//    // Verify OoA: all densities are contiguous
//    const float* densities = static_cast<const float*>(storage.getArrayData<0>());
//
//    // Brick 0 densities: [0..63]
//    for (size_t i = 0; i < 64; ++i) {
//        EXPECT_FLOAT_EQ(densities[i], static_cast<float>(i));
//    }
//
//    // Brick 1 densities: [64..127]
//    for (size_t i = 0; i < 64; ++i) {
//        EXPECT_FLOAT_EQ(densities[64 + i], static_cast<float>(1000 + i));
//    }
//}
