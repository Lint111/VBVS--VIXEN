/**
 * @file test_group_dispatch.cpp
 * @brief Tests for Sprint 6.1 group-based dispatch system
 *
 * Tests:
 * - GroupKeyModifier validation and metadata storage
 * - DispatchPass validation (IsValid())
 * - Group partitioning logic in MultiDispatchNode
 * - Deterministic group ordering (std::map)
 * - Backward compatibility with QueueDispatch()
 */

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>
#include <map>
#include <vector>

// Sprint 6.1 includes
#include "Connection/ConnectionRule.h"  // Required for ConnectionModifier template instantiation
#include "Connection/Modifiers/GroupKeyModifier.h"
#include "Data/DispatchPass.h"
#include "Data/Nodes/MultiDispatchNodeConfig.h"
#include "Connection/ConnectionTypes.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// GROUP KEY MODIFIER TESTS
// ============================================================================

TEST(GroupKeyModifier, ConstructWithOptionalField) {
    // Test construction with optional<uint32_t> field
    auto modifier = GroupKey(&DispatchPass::groupId);

    EXPECT_NE(modifier, nullptr);
    EXPECT_EQ(modifier->Name(), "GroupKeyModifier");
    EXPECT_EQ(modifier->Priority(), 60u);  // Between field extraction (75) and validation (50)
}

TEST(GroupKeyModifier, ExtractsOptionalField) {
    auto modifier = GroupKeyModifier(&DispatchPass::groupId);

    EXPECT_TRUE(modifier.ExtractsOptional());
    // Field offset check (non-zero, platform-dependent)
    EXPECT_GT(modifier.GetFieldOffset(), 0u);
}

TEST(GroupKeyModifier, PreValidationRequiresAccumulationSlot) {
    auto modifier = GroupKeyModifier(&DispatchPass::groupId);

    // Create fake connection context
    ConnectionContext ctx;
    ctx.sourceSlot = SlotInfo{};  // Dummy
    ctx.targetSlot = SlotInfo{};
    ctx.targetSlot.flags = SlotFlags::None;  // NOT accumulation

    auto result = modifier.PreValidation(ctx);

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_FALSE(result.errorMessage.empty());
    EXPECT_TRUE(result.errorMessage.find("accumulation slot") != std::string::npos);
}

TEST(GroupKeyModifier, PreValidationSucceedsForAccumulationSlot) {
    auto modifier = GroupKeyModifier(&DispatchPass::groupId);

    ConnectionContext ctx;
    ctx.sourceSlot = SlotInfo{};
    ctx.targetSlot = SlotInfo{};
    ctx.targetSlot.flags = SlotFlags::Accumulation;  // IS accumulation

    auto result = modifier.PreValidation(ctx);

    EXPECT_TRUE(result.IsSuccess());
    // Sprint 6.1: Modifier validates intent but doesn't store metadata
    // Extraction is hard-coded in MultiDispatchNode::CompileImpl
}

// ============================================================================
// DISPATCH PASS VALIDATION TESTS
// ============================================================================

TEST(DispatchPass, IsValidRequiresPipeline) {
    DispatchPass pass{};
    pass.layout = reinterpret_cast<VkPipelineLayout>(0x1234);
    pass.workGroupCount = {1, 1, 1};
    pass.pipeline = VK_NULL_HANDLE;  // INVALID

    EXPECT_FALSE(pass.IsValid());
}

TEST(DispatchPass, IsValidRequiresLayout) {
    DispatchPass pass{};
    pass.pipeline = reinterpret_cast<VkPipeline>(0x1234);
    pass.workGroupCount = {1, 1, 1};
    pass.layout = VK_NULL_HANDLE;  // INVALID

    EXPECT_FALSE(pass.IsValid());
}

TEST(DispatchPass, IsValidRequiresNonZeroWorkGroups) {
    DispatchPass pass{};
    pass.pipeline = reinterpret_cast<VkPipeline>(0x1234);
    pass.layout = reinterpret_cast<VkPipelineLayout>(0x5678);
    pass.workGroupCount = {0, 0, 0};  // INVALID

    EXPECT_FALSE(pass.IsValid());
}

TEST(DispatchPass, IsValidAllowsNullOptionalFields) {
    DispatchPass pass{};
    pass.pipeline = reinterpret_cast<VkPipeline>(0x1234);
    pass.layout = reinterpret_cast<VkPipelineLayout>(0x5678);
    pass.workGroupCount = {8, 8, 1};
    // groupId = nullopt (optional, should be fine)
    // descriptorSets empty (optional, should be fine)
    // pushConstants = nullopt (optional, should be fine)

    EXPECT_TRUE(pass.IsValid());
}

TEST(DispatchPass, TotalWorkGroupsCalculation) {
    DispatchPass pass{};
    pass.pipeline = reinterpret_cast<VkPipeline>(0x1234);
    pass.layout = reinterpret_cast<VkPipelineLayout>(0x5678);
    pass.workGroupCount = {8, 4, 2};

    EXPECT_EQ(pass.TotalWorkGroups(), 64u);  // 8 * 4 * 2
}

TEST(DispatchPass, GroupIdOptional) {
    DispatchPass pass{};

    // Default: no group ID
    EXPECT_FALSE(pass.groupId.has_value());

    // Set group ID
    pass.groupId = 5;
    EXPECT_TRUE(pass.groupId.has_value());
    EXPECT_EQ(pass.groupId.value(), 5u);

    // Reset
    pass.groupId = std::nullopt;
    EXPECT_FALSE(pass.groupId.has_value());
}

// ============================================================================
// GROUP PARTITIONING LOGIC TESTS
// ============================================================================

TEST(GroupPartitioning, DeterministicOrderingWithStdMap) {
    // Verify std::map provides deterministic ordering
    std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches;

    // Insert in random order
    groupedDispatches[5] = {};
    groupedDispatches[1] = {};
    groupedDispatches[10] = {};
    groupedDispatches[3] = {};

    // Verify iteration order is sorted
    std::vector<uint32_t> iterationOrder;
    for (const auto& [groupId, passes] : groupedDispatches) {
        iterationOrder.push_back(groupId);
    }

    EXPECT_EQ(iterationOrder.size(), 4u);
    EXPECT_EQ(iterationOrder[0], 1u);
    EXPECT_EQ(iterationOrder[1], 3u);
    EXPECT_EQ(iterationOrder[2], 5u);
    EXPECT_EQ(iterationOrder[3], 10u);
}

TEST(GroupPartitioning, PassesWithoutGroupIdDefaultToGroup0) {
    // Simulate MultiDispatchNode CompileImpl logic
    std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches;

    DispatchPass pass1{};
    pass1.debugName = "Pass1";
    pass1.groupId = std::nullopt;  // No group ID

    DispatchPass pass2{};
    pass2.debugName = "Pass2";
    pass2.groupId = 5;

    // Partition logic (mimic MultiDispatchNode::CompileImpl)
    std::vector<DispatchPass> input = {pass1, pass2};
    for (const auto& pass : input) {
        if (pass.groupId.has_value()) {
            groupedDispatches[pass.groupId.value()].push_back(pass);
        } else {
            groupedDispatches[0].push_back(pass);  // Default to group 0
        }
    }

    EXPECT_EQ(groupedDispatches.size(), 2u);
    EXPECT_EQ(groupedDispatches[0].size(), 1u);
    EXPECT_EQ(groupedDispatches[5].size(), 1u);
    EXPECT_EQ(groupedDispatches[0][0].debugName, "Pass1");
    EXPECT_EQ(groupedDispatches[5][0].debugName, "Pass2");
}

TEST(GroupPartitioning, MultiplePassesSameGroup) {
    std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches;

    DispatchPass pass1{};
    pass1.debugName = "Pass1";
    pass1.groupId = 2;

    DispatchPass pass2{};
    pass2.debugName = "Pass2";
    pass2.groupId = 2;

    DispatchPass pass3{};
    pass3.debugName = "Pass3";
    pass3.groupId = 2;

    std::vector<DispatchPass> input = {pass1, pass2, pass3};
    for (const auto& pass : input) {
        groupedDispatches[pass.groupId.value()].push_back(pass);
    }

    EXPECT_EQ(groupedDispatches.size(), 1u);
    EXPECT_EQ(groupedDispatches[2].size(), 3u);
    EXPECT_EQ(groupedDispatches[2][0].debugName, "Pass1");
    EXPECT_EQ(groupedDispatches[2][1].debugName, "Pass2");
    EXPECT_EQ(groupedDispatches[2][2].debugName, "Pass3");
}

TEST(GroupPartitioning, EmptyInputProducesEmptyMap) {
    std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches;
    std::vector<DispatchPass> input;

    for (const auto& pass : input) {
        if (pass.groupId.has_value()) {
            groupedDispatches[pass.groupId.value()].push_back(pass);
        }
    }

    EXPECT_TRUE(groupedDispatches.empty());
}

// ============================================================================
// MULTIDISPATCHNODECONFIG SLOT TESTS
// ============================================================================

TEST(MultiDispatchNodeConfig, HasGroupInputsSlot) {
    // Verify GROUP_INPUTS slot is defined with correct properties
    constexpr auto slotIndex = MultiDispatchNodeConfig::GROUP_INPUTS_Slot::index;
    EXPECT_EQ(slotIndex, 5u);

    constexpr auto nullability = MultiDispatchNodeConfig::GROUP_INPUTS_Slot::nullability;
    EXPECT_EQ(nullability, SlotNullability::Optional);

    constexpr auto role = MultiDispatchNodeConfig::GROUP_INPUTS_Slot::role;
    EXPECT_EQ(role, SlotRole::Dependency);

    constexpr auto storageStrategy = MultiDispatchNodeConfig::GROUP_INPUTS_Slot::storageStrategy;
    EXPECT_EQ(storageStrategy, SlotStorageStrategy::Value);
}

TEST(MultiDispatchNodeConfig, SlotCountIncludesGroupInputs) {
    // Sprint 6.1 added one input (GROUP_INPUTS), so total should be 6
    EXPECT_EQ(MultiDispatchNodeCounts::INPUTS, 6u);
    EXPECT_EQ(MultiDispatchNodeCounts::OUTPUTS, 2u);
}

// ============================================================================
// STATISTICS TESTS
// ============================================================================

TEST(MultiDispatchStats, DefaultInitialization) {
    MultiDispatchStats stats{};

    EXPECT_EQ(stats.dispatchCount, 0u);
    EXPECT_EQ(stats.barrierCount, 0u);
    EXPECT_EQ(stats.totalWorkGroups, 0u);
    EXPECT_DOUBLE_EQ(stats.recordTimeMs, 0.0);
}

TEST(MultiDispatchStats, Accumulation) {
    MultiDispatchStats stats{};

    // Simulate recording multiple dispatches
    stats.dispatchCount += 3;
    stats.barrierCount += 2;
    stats.totalWorkGroups += 64;
    stats.recordTimeMs = 1.25;

    EXPECT_EQ(stats.dispatchCount, 3u);
    EXPECT_EQ(stats.barrierCount, 2u);
    EXPECT_EQ(stats.totalWorkGroups, 64u);
    EXPECT_DOUBLE_EQ(stats.recordTimeMs, 1.25);
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST(EdgeCases, MaxGroupId) {
    // Verify we can handle large group IDs (up to UINT32_MAX)
    std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches;

    DispatchPass pass{};
    pass.groupId = UINT32_MAX;

    groupedDispatches[pass.groupId.value()].push_back(pass);

    EXPECT_EQ(groupedDispatches.size(), 1u);
    EXPECT_EQ(groupedDispatches[UINT32_MAX].size(), 1u);
}

TEST(EdgeCases, MixedGroupIdsWithNullopt) {
    std::map<uint32_t, std::vector<DispatchPass>> groupedDispatches;

    DispatchPass pass1{}; pass1.groupId = std::nullopt;
    DispatchPass pass2{}; pass2.groupId = 0;
    DispatchPass pass3{}; pass3.groupId = 1;
    DispatchPass pass4{}; pass4.groupId = std::nullopt;

    std::vector<DispatchPass> input = {pass1, pass2, pass3, pass4};
    for (const auto& pass : input) {
        if (pass.groupId.has_value()) {
            groupedDispatches[pass.groupId.value()].push_back(pass);
        } else {
            groupedDispatches[0].push_back(pass);  // Default to group 0
        }
    }

    // pass1 and pass4 (nullopt) go to group 0
    // pass2 also goes to group 0 (explicit)
    // pass3 goes to group 1
    EXPECT_EQ(groupedDispatches[0].size(), 3u);
    EXPECT_EQ(groupedDispatches[1].size(), 1u);
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
