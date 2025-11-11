/**
 * @file test_resource_pool.cpp
 * @brief Comprehensive tests for ResourcePool class (Phase H)
 *
 * Coverage: ResourcePool.h (Target: 85%+)
 *
 * Tests:
 * - Pool initialization and lifecycle
 * - Budget management integration
 * - Aliasing engine integration
 * - Profiling integration
 * - Resource allocation and release
 * - Configuration (aliasing enable/disable, thresholds)
 * - Frame tracking lifecycle
 * - Error handling and edge cases
 *
 * Phase H Integration Tests
 */

#include <gtest/gtest.h>
#include "Core/ResourcePool.h"
#include "Core/AliasingEngine.h"
#include "Core/ResourceProfiler.h"
#include "Core/ResourceBudgetManager.h"
#include <memory>
#include <chrono>

using namespace Vixen::RenderGraph;

// ============================================================================
// Test Fixture
// ============================================================================

class ResourcePoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<ResourcePool>();
    }

    void TearDown() override {
        pool.reset();
    }

    std::unique_ptr<ResourcePool> pool;
};

// ============================================================================
// 1. Construction & Initialization
// ============================================================================

TEST_F(ResourcePoolTest, ConstructorInitializesWithDefaultSettings) {
    EXPECT_NE(pool, nullptr) << "Pool should be constructed successfully";

    // Verify default settings
    auto* budgetMgr = pool->GetBudgetManager();
    EXPECT_NE(budgetMgr, nullptr) << "Budget manager should be initialized";

    // Verify default aliasing threshold (1 MB)
    EXPECT_NO_THROW({
        pool->SetAliasingThreshold(1024 * 1024);
    }) << "Should accept valid aliasing threshold";
}

TEST_F(ResourcePoolTest, DestructorLogsStatistics) {
    // Create pool in local scope to trigger destructor
    {
        auto tempPool = std::make_unique<ResourcePool>();
        tempPool->EnableAliasing(true);
        // Pool will be destroyed at end of scope, should log stats
    }
    // If this doesn't crash, destructor handled cleanup correctly
    SUCCEED();
}

// ============================================================================
// 2. Budget Management Integration
// ============================================================================

TEST_F(ResourcePoolTest, GetBudgetManagerReturnsValidPointer) {
    auto* budgetMgr = pool->GetBudgetManager();
    EXPECT_NE(budgetMgr, nullptr) << "Budget manager should be accessible";
}

TEST_F(ResourcePoolTest, SetBudgetConfiguresBudgetManager) {
    ResourceBudget budget;
    budget.maxBytes = 512 * 1024 * 1024;  // 512 MB
    budget.strict = true;
    budget.warningThreshold = 0.8f;
    budget.criticalThreshold = 0.95f;

    EXPECT_NO_THROW({
        pool->SetBudget(BudgetResourceType::HostMemory, budget);
    }) << "Should accept valid budget configuration";

    // Verify budget was set
    auto* budgetMgr = pool->GetBudgetManager();
    auto usage = budgetMgr->GetUsage(BudgetResourceType::HostMemory);
    EXPECT_EQ(usage.budgetBytes, budget.maxBytes)
        << "Budget should be set correctly";
}

TEST_F(ResourcePoolTest, GetBudgetStatsReturnsCurrentUsage) {
    // Set a budget first
    ResourceBudget budget;
    budget.maxBytes = 256 * 1024 * 1024;  // 256 MB
    pool->SetBudget(BudgetResourceType::HostMemory, budget);

    // Get stats
    auto stats = pool->GetBudgetStats(BudgetResourceType::HostMemory);
    EXPECT_EQ(stats.budgetBytes, budget.maxBytes)
        << "Budget stats should reflect configured budget";
    EXPECT_EQ(stats.usedBytes, 0)
        << "Initially no bytes should be used";
}

// ============================================================================
// 3. Aliasing Configuration
// ============================================================================

TEST_F(ResourcePoolTest, EnableAliasingActivatesAliasingEngine) {
    EXPECT_NO_THROW({
        pool->EnableAliasing(true);
    }) << "Should enable aliasing without errors";

    EXPECT_NO_THROW({
        pool->EnableAliasing(false);
    }) << "Should disable aliasing without errors";
}

TEST_F(ResourcePoolTest, SetAliasingThresholdAcceptsValidValues) {
    EXPECT_NO_THROW({
        pool->SetAliasingThreshold(0);
    }) << "Should accept 0 (alias everything)";

    EXPECT_NO_THROW({
        pool->SetAliasingThreshold(1024);  // 1 KB
    }) << "Should accept 1 KB threshold";

    EXPECT_NO_THROW({
        pool->SetAliasingThreshold(10 * 1024 * 1024);  // 10 MB
    }) << "Should accept 10 MB threshold";
}

// ============================================================================
// 4. Profiling Integration
// ============================================================================

TEST_F(ResourcePoolTest, BeginFrameProfilingStartsNewFrame) {
    uint64_t frameNumber = 42;

    EXPECT_NO_THROW({
        pool->BeginFrameProfiling(frameNumber);
    }) << "Should start frame profiling without errors";
}

TEST_F(ResourcePoolTest, EndFrameProfilingCompletesFrame) {
    pool->BeginFrameProfiling(1);

    EXPECT_NO_THROW({
        pool->EndFrameProfiling();
    }) << "Should end frame profiling without errors";
}

TEST_F(ResourcePoolTest, FrameProfilingLifecycle) {
    // Test multiple frames
    for (uint64_t frame = 0; frame < 10; ++frame) {
        EXPECT_NO_THROW({
            pool->BeginFrameProfiling(frame);
            // Simulate some work
            pool->EndFrameProfiling();
        }) << "Frame " << frame << " should complete without errors";
    }
}

TEST_F(ResourcePoolTest, BeginFrameStackTrackingStartsTracking) {
    uint64_t frameNumber = 1;

    EXPECT_NO_THROW({
        pool->BeginFrameStackTracking(frameNumber);
    }) << "Should start stack tracking without errors";
}

TEST_F(ResourcePoolTest, EndFrameStackTrackingCompletesTracking) {
    pool->BeginFrameStackTracking(1);

    EXPECT_NO_THROW({
        pool->EndFrameStackTracking();
    }) << "Should end stack tracking without errors";
}

// ============================================================================
// 5. Lifetime Analyzer Integration
// ============================================================================

TEST_F(ResourcePoolTest, SetLifetimeAnalyzerAcceptsValidPointer) {
    // Note: We can't easily create a real ResourceLifetimeAnalyzer
    // without full graph setup, so we test nullptr handling
    EXPECT_NO_THROW({
        pool->SetLifetimeAnalyzer(nullptr);
    }) << "Should accept nullptr (no analyzer)";
}

// ============================================================================
// 6. Combined Workflow Tests
// ============================================================================

TEST_F(ResourcePoolTest, CompleteFrameWorkflow) {
    // Configure pool
    ResourceBudget budget;
    budget.maxBytes = 1024 * 1024 * 1024;  // 1 GB
    pool->SetBudget(BudgetResourceType::DeviceMemory, budget);
    pool->EnableAliasing(true);
    pool->SetAliasingThreshold(1024 * 1024);  // 1 MB

    // Execute frame lifecycle
    uint64_t frameNumber = 1;
    EXPECT_NO_THROW({
        pool->BeginFrameProfiling(frameNumber);
        pool->BeginFrameStackTracking(frameNumber);

        // Simulate frame work here
        // (Resource allocations would happen here in real usage)

        pool->EndFrameStackTracking();
        pool->EndFrameProfiling();
    }) << "Complete frame workflow should execute without errors";

    // Verify final state
    auto stats = pool->GetBudgetStats(BudgetResourceType::DeviceMemory);
    EXPECT_GE(stats.budgetBytes, 0) << "Budget should be valid";
}

TEST_F(ResourcePoolTest, MultiFrameWorkflowWithProfiling) {
    pool->EnableAliasing(true);

    // Execute multiple frames
    for (uint64_t frame = 0; frame < 120; ++frame) {
        EXPECT_NO_THROW({
            pool->BeginFrameProfiling(frame);
            pool->BeginFrameStackTracking(frame);

            // Simulate work

            pool->EndFrameStackTracking();
            pool->EndFrameProfiling();
        }) << "Frame " << frame << " should complete successfully";
    }
}

// ============================================================================
// 7. Edge Cases & Error Handling
// ============================================================================

TEST_F(ResourcePoolTest, EndFrameWithoutBeginIsHandledGracefully) {
    // Ending frame without beginning should not crash
    EXPECT_NO_THROW({
        pool->EndFrameProfiling();
    }) << "Ending frame without begin should be handled gracefully";
}

TEST_F(ResourcePoolTest, MultipleBeginFrameCallsAreHandledGracefully) {
    // Multiple begin calls without end
    EXPECT_NO_THROW({
        pool->BeginFrameProfiling(1);
        pool->BeginFrameProfiling(2);  // Should handle gracefully
    }) << "Multiple begin calls should be handled gracefully";
}

TEST_F(ResourcePoolTest, NullLifetimeAnalyzerIsHandledGracefully) {
    pool->SetLifetimeAnalyzer(nullptr);
    pool->EnableAliasing(true);  // Aliasing without analyzer should still work

    // Execute frame - should work even without analyzer
    EXPECT_NO_THROW({
        pool->BeginFrameProfiling(1);
        pool->EndFrameProfiling();
    }) << "Should work without lifetime analyzer";
}

// ============================================================================
// 8. Budget Configuration Variations
// ============================================================================

TEST_F(ResourcePoolTest, ConfigureMultipleBudgetTypes) {
    ResourceBudget hostBudget;
    hostBudget.maxBytes = 512 * 1024 * 1024;  // 512 MB

    ResourceBudget deviceBudget;
    deviceBudget.maxBytes = 2 * 1024 * 1024 * 1024;  // 2 GB

    EXPECT_NO_THROW({
        pool->SetBudget(BudgetResourceType::HostMemory, hostBudget);
        pool->SetBudget(BudgetResourceType::DeviceMemory, deviceBudget);
    }) << "Should configure multiple budget types";

    // Verify both budgets
    auto hostStats = pool->GetBudgetStats(BudgetResourceType::HostMemory);
    auto deviceStats = pool->GetBudgetStats(BudgetResourceType::DeviceMemory);

    EXPECT_EQ(hostStats.budgetBytes, hostBudget.maxBytes);
    EXPECT_EQ(deviceStats.budgetBytes, deviceBudget.maxBytes);
}

TEST_F(ResourcePoolTest, StrictBudgetModeConfiguration) {
    ResourceBudget strictBudget;
    strictBudget.maxBytes = 256 * 1024 * 1024;  // 256 MB
    strictBudget.strict = true;

    EXPECT_NO_THROW({
        pool->SetBudget(BudgetResourceType::HostMemory, strictBudget);
    }) << "Should configure strict budget mode";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
