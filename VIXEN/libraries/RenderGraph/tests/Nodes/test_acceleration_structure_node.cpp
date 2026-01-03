/**
 * @file test_acceleration_structure_node.cpp
 * @brief Comprehensive tests for AccelerationStructureNode class (Phase 3.4)
 *
 * Coverage: AccelerationStructureNode.h, AccelerationStructureNodeConfig.h
 *
 * Unit Tests (No Vulkan Required):
 * - Configuration validation (slot counts, indices, types)
 * - BUILD_MODE and IMAGE_INDEX slot metadata
 * - Static mode as default behavior
 * - Compile-time assertions for slot indices
 *
 * Integration Tests (Full Vulkan SDK Required):
 * - Static mode BLAS/TLAS creation (via cacher)
 * - Dynamic mode initialization and per-frame output
 *
 * Sprint 5 Phase 3.4: Integration Tests for Dynamic TLAS
 */

#include <gtest/gtest.h>
#include "Nodes/AccelerationStructureNode.h"
#include "Data/Nodes/AccelerationStructureNodeConfig.h"
#include "AccelerationStructureCacher.h"  // For ASBuildMode
#include <memory>
#include <type_traits>

using namespace Vixen::RenderGraph;

// ============================================================================
// Test Fixture
// ============================================================================

class AccelerationStructureNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Unit tests validate configuration without actual Vulkan resources
    }

    void TearDown() override {
    }
};

// ============================================================================
// 1. Configuration Tests - Slot Counts
// ============================================================================

TEST_F(AccelerationStructureNodeTest, ConfigHasFiveInputs) {
    EXPECT_EQ(AccelerationStructureNodeConfig::INPUT_COUNT, 5)
        << "AccelerationStructureNode should have 5 inputs: "
        << "VULKAN_DEVICE_IN, COMMAND_POOL, AABB_DATA, IMAGE_INDEX, BUILD_MODE";
}

TEST_F(AccelerationStructureNodeTest, ConfigHasTwoOutputs) {
    EXPECT_EQ(AccelerationStructureNodeConfig::OUTPUT_COUNT, 2)
        << "AccelerationStructureNode should have 2 outputs: "
        << "ACCELERATION_STRUCTURE_DATA, TLAS_HANDLE";
}

TEST_F(AccelerationStructureNodeTest, ConfigArrayModeIsSingle) {
    EXPECT_EQ(AccelerationStructureNodeConfig::ARRAY_MODE, SlotArrayMode::Single)
        << "AccelerationStructureNode should use Single array mode";
}

// ============================================================================
// 2. Configuration Tests - Input Slot Indices
// ============================================================================

TEST_F(AccelerationStructureNodeTest, InputSlotIndicesAreCorrect) {
    EXPECT_EQ(AccelerationStructureNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0);
    EXPECT_EQ(AccelerationStructureNodeConfig::COMMAND_POOL_Slot::index, 1);
    EXPECT_EQ(AccelerationStructureNodeConfig::AABB_DATA_Slot::index, 2);
    EXPECT_EQ(AccelerationStructureNodeConfig::IMAGE_INDEX_Slot::index, 3);
    EXPECT_EQ(AccelerationStructureNodeConfig::BUILD_MODE_Slot::index, 4);
}

TEST_F(AccelerationStructureNodeTest, OutputSlotIndicesAreCorrect) {
    EXPECT_EQ(AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA_Slot::index, 0);
    EXPECT_EQ(AccelerationStructureNodeConfig::TLAS_HANDLE_Slot::index, 1);
}

// ============================================================================
// 3. Configuration Tests - Slot Nullability (Phase 3 additions)
// ============================================================================

TEST_F(AccelerationStructureNodeTest, RequiredInputsAreNotNullable) {
    EXPECT_FALSE(AccelerationStructureNodeConfig::VULKAN_DEVICE_IN_Slot::nullable)
        << "VULKAN_DEVICE_IN must be required";
    EXPECT_FALSE(AccelerationStructureNodeConfig::COMMAND_POOL_Slot::nullable)
        << "COMMAND_POOL must be required";
    EXPECT_FALSE(AccelerationStructureNodeConfig::AABB_DATA_Slot::nullable)
        << "AABB_DATA must be required";
}

TEST_F(AccelerationStructureNodeTest, DynamicModeInputsAreOptional) {
    EXPECT_TRUE(AccelerationStructureNodeConfig::IMAGE_INDEX_Slot::nullable)
        << "IMAGE_INDEX should be optional (only needed for dynamic mode)";
    EXPECT_TRUE(AccelerationStructureNodeConfig::BUILD_MODE_Slot::nullable)
        << "BUILD_MODE should be optional (defaults to Static)";
}

TEST_F(AccelerationStructureNodeTest, OutputsAreRequired) {
    EXPECT_FALSE(AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA_Slot::nullable)
        << "ACCELERATION_STRUCTURE_DATA output must be required";
    EXPECT_FALSE(AccelerationStructureNodeConfig::TLAS_HANDLE_Slot::nullable)
        << "TLAS_HANDLE output must be required";
}

// ============================================================================
// 4. Configuration Tests - Slot Types
// ============================================================================

TEST_F(AccelerationStructureNodeTest, VulkanDeviceInTypeIsCorrect) {
    bool isCorrectType = std::is_same_v<
        AccelerationStructureNodeConfig::VULKAN_DEVICE_IN_Slot::Type,
        Vixen::Vulkan::Resources::VulkanDevice*
    >;
    EXPECT_TRUE(isCorrectType)
        << "VULKAN_DEVICE_IN type should be VulkanDevice*";
}

TEST_F(AccelerationStructureNodeTest, CommandPoolTypeIsCorrect) {
    bool isCorrectType = std::is_same_v<
        AccelerationStructureNodeConfig::COMMAND_POOL_Slot::Type,
        VkCommandPool
    >;
    EXPECT_TRUE(isCorrectType)
        << "COMMAND_POOL type should be VkCommandPool";
}

TEST_F(AccelerationStructureNodeTest, AABBDataTypeIsCorrect) {
    bool isCorrectType = std::is_same_v<
        AccelerationStructureNodeConfig::AABB_DATA_Slot::Type,
        VoxelAABBData*
    >;
    EXPECT_TRUE(isCorrectType)
        << "AABB_DATA type should be VoxelAABBData*";
}

TEST_F(AccelerationStructureNodeTest, ImageIndexTypeIsCorrect) {
    bool isCorrectType = std::is_same_v<
        AccelerationStructureNodeConfig::IMAGE_INDEX_Slot::Type,
        uint32_t
    >;
    EXPECT_TRUE(isCorrectType)
        << "IMAGE_INDEX type should be uint32_t";
}

TEST_F(AccelerationStructureNodeTest, BuildModeTypeIsCorrect) {
    bool isCorrectType = std::is_same_v<
        AccelerationStructureNodeConfig::BUILD_MODE_Slot::Type,
        CashSystem::ASBuildMode
    >;
    EXPECT_TRUE(isCorrectType)
        << "BUILD_MODE type should be ASBuildMode";
}

TEST_F(AccelerationStructureNodeTest, AccelStructDataTypeIsCorrect) {
    bool isCorrectType = std::is_same_v<
        AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA_Slot::Type,
        AccelerationStructureData*
    >;
    EXPECT_TRUE(isCorrectType)
        << "ACCELERATION_STRUCTURE_DATA type should be AccelerationStructureData*";
}

TEST_F(AccelerationStructureNodeTest, TLASHandleTypeIsCorrect) {
    bool isCorrectType = std::is_same_v<
        AccelerationStructureNodeConfig::TLAS_HANDLE_Slot::Type,
        VkAccelerationStructureKHR
    >;
    EXPECT_TRUE(isCorrectType)
        << "TLAS_HANDLE type should be VkAccelerationStructureKHR";
}

// ============================================================================
// 5. ASBuildMode Tests
// ============================================================================

TEST_F(AccelerationStructureNodeTest, ASBuildModeEnumValues) {
    // Verify enum values are what we expect
    EXPECT_EQ(static_cast<uint8_t>(CashSystem::ASBuildMode::Static), 0);
    EXPECT_EQ(static_cast<uint8_t>(CashSystem::ASBuildMode::Dynamic), 1);
}

TEST_F(AccelerationStructureNodeTest, ASBuildModeDefaultIsStatic) {
    // Default-initialized ASBuildMode should be Static (value 0)
    CashSystem::ASBuildMode defaultMode{};
    EXPECT_EQ(defaultMode, CashSystem::ASBuildMode::Static)
        << "Default-initialized ASBuildMode should be Static";
}

// ============================================================================
// 6. Slot Role Tests (Phase 3 additions)
// ============================================================================

TEST_F(AccelerationStructureNodeTest, ImageIndexHasExecuteRole) {
    EXPECT_EQ(AccelerationStructureNodeConfig::IMAGE_INDEX_Slot::role, SlotRole::Execute)
        << "IMAGE_INDEX should have Execute role (per-frame value)";
}

TEST_F(AccelerationStructureNodeTest, BuildModeHasDependencyRole) {
    EXPECT_EQ(AccelerationStructureNodeConfig::BUILD_MODE_Slot::role, SlotRole::Dependency)
        << "BUILD_MODE should have Dependency role (set during Compile)";
}

// ============================================================================
// 7. Backward Compatibility Tests
// ============================================================================

TEST_F(AccelerationStructureNodeTest, OriginalInputsUnchanged) {
    // Verify original inputs are at their expected indices
    // This ensures backward compatibility with existing graphs
    EXPECT_EQ(AccelerationStructureNodeConfig::VULKAN_DEVICE_IN_Slot::index, 0)
        << "VULKAN_DEVICE_IN should remain at index 0 for backward compatibility";
    EXPECT_EQ(AccelerationStructureNodeConfig::COMMAND_POOL_Slot::index, 1)
        << "COMMAND_POOL should remain at index 1 for backward compatibility";
    EXPECT_EQ(AccelerationStructureNodeConfig::AABB_DATA_Slot::index, 2)
        << "AABB_DATA should remain at index 2 for backward compatibility";
}

TEST_F(AccelerationStructureNodeTest, OriginalOutputsUnchanged) {
    // Verify original outputs are at their expected indices
    EXPECT_EQ(AccelerationStructureNodeConfig::ACCELERATION_STRUCTURE_DATA_Slot::index, 0)
        << "ACCELERATION_STRUCTURE_DATA should remain at index 0";
    EXPECT_EQ(AccelerationStructureNodeConfig::TLAS_HANDLE_Slot::index, 1)
        << "TLAS_HANDLE should remain at index 1";
}

// ============================================================================
// 8. Parameter Tests
// ============================================================================

TEST_F(AccelerationStructureNodeTest, HasExpectedParameters) {
    // Verify parameter constants are defined
    EXPECT_STREQ(AccelerationStructureNodeConfig::PARAM_PREFER_FAST_TRACE, "prefer_fast_trace");
    EXPECT_STREQ(AccelerationStructureNodeConfig::PARAM_ALLOW_UPDATE, "allow_update");
    EXPECT_STREQ(AccelerationStructureNodeConfig::PARAM_ALLOW_COMPACTION, "allow_compaction");
}

// ============================================================================
// NOTE: Integration tests requiring Vulkan runtime are in a separate suite
// that runs only when full Vulkan SDK is available.
// ============================================================================
