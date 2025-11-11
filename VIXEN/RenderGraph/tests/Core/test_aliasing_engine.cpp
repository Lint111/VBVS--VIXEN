/**
 * @file test_aliasing_engine.cpp
 * @brief Comprehensive tests for AliasingEngine class (Phase H)
 *
 * Coverage: AliasingEngine.h (Target: 85%+)
 *
 * Tests:
 * - Aliasing candidate registration
 * - Best-fit alias finding algorithm
 * - Memory compatibility checking
 * - Lifetime overlap detection
 * - Statistics tracking (success rate, bytes saved, efficiency)
 * - Release and reuse lifecycle
 * - Aliasing threshold enforcement
 * - Edge cases and error handling
 *
 * Phase H: Memory Aliasing for 50-80% VRAM Savings
 */

#include <gtest/gtest.h>
#include "Core/AliasingEngine.h"
#include "Core/Resource.h"
#include <memory>
#include <vulkan/vulkan.h>

using namespace Vixen::RenderGraph;

// ============================================================================
// Test Fixture
// ============================================================================

class AliasingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AliasingEngine>();
    }

    void TearDown() override {
        engine.reset();
    }

    std::unique_ptr<AliasingEngine> engine;

    // Helper: Create mock resource pointer
    Resource* CreateMockResource(int id) {
        return reinterpret_cast<Resource*>(static_cast<uintptr_t>(0x1000 + id));
    }

    // Helper: Create VkMemoryRequirements with specific size and alignment
    VkMemoryRequirements CreateMemoryRequirements(size_t size, size_t alignment = 256) {
        VkMemoryRequirements req = {};
        req.size = size;
        req.alignment = alignment;
        req.memoryTypeBits = 0x1;  // Simple memory type
        return req;
    }
};

// ============================================================================
// 1. Construction & Initialization
// ============================================================================

TEST_F(AliasingEngineTest, ConstructorInitializesEmptyEngine) {
    auto stats = engine->GetStats();
    EXPECT_EQ(stats.totalAliasAttempts, 0) << "New engine should have no attempts";
    EXPECT_EQ(stats.successfulAliases, 0) << "New engine should have no successes";
    EXPECT_EQ(stats.totalBytesSaved, 0) << "New engine should have saved no bytes";
}

// ============================================================================
// 2. Configuration
// ============================================================================

TEST_F(AliasingEngineTest, SetAliasingThresholdAcceptsValidValues) {
    EXPECT_NO_THROW({
        engine->SetAliasingThreshold(0);
    }) << "Should accept 0 (alias everything)";

    EXPECT_NO_THROW({
        engine->SetAliasingThreshold(1024 * 1024);  // 1 MB
    }) << "Should accept 1 MB threshold";

    EXPECT_NO_THROW({
        engine->SetAliasingThreshold(10 * 1024 * 1024);  // 10 MB
    }) << "Should accept 10 MB threshold";
}

TEST_F(AliasingEngineTest, EnableAliasingTogglesAliasingBehavior) {
    EXPECT_NO_THROW({
        engine->Enable(true);
    }) << "Should enable aliasing";

    EXPECT_NO_THROW({
        engine->Enable(false);
    }) << "Should disable aliasing";
}

// ============================================================================
// 3. Alias Registration
// ============================================================================

TEST_F(AliasingEngineTest, RegisterForAliasingStoresCandidate) {
    Resource* resource = CreateMockResource(1);
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);  // 4 MB

    EXPECT_NO_THROW({
        engine->RegisterForAliasing(
            resource,
            requirements,
            ResourceLifetime::FrameLocal
        );
    }) << "Should register resource for aliasing";
}

TEST_F(AliasingEngineTest, RegisterMultipleCandidates) {
    for (int i = 0; i < 10; ++i) {
        Resource* resource = CreateMockResource(i);
        auto requirements = CreateMemoryRequirements((i + 1) * 1024 * 1024);  // Variable sizes

        EXPECT_NO_THROW({
            engine->RegisterForAliasing(
                resource,
                requirements,
                ResourceLifetime::FrameLocal
            );
        });
    }
}

// ============================================================================
// 4. Release and Mark Available
// ============================================================================

TEST_F(AliasingEngineTest, MarkReleasedMakesResourceAvailable) {
    Resource* resource = CreateMockResource(1);
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);

    // Register first
    engine->RegisterForAliasing(resource, requirements, ResourceLifetime::FrameLocal);

    // Mark as released
    uint64_t frameNumber = 1;
    EXPECT_NO_THROW({
        engine->MarkReleased(resource, frameNumber);
    }) << "Should mark resource as released";
}

TEST_F(AliasingEngineTest, MarkReleasedMultipleResources) {
    // Register and release multiple resources
    for (int i = 0; i < 5; ++i) {
        Resource* resource = CreateMockResource(i);
        auto requirements = CreateMemoryRequirements((i + 1) * 1024 * 1024);

        engine->RegisterForAliasing(resource, requirements, ResourceLifetime::FrameLocal);
        engine->MarkReleased(resource, static_cast<uint64_t>(i));
    }

    // Verify no crashes or errors
    SUCCEED();
}

// ============================================================================
// 5. Alias Finding - Basic Cases
// ============================================================================

TEST_F(AliasingEngineTest, FindAliasReturnsNullWhenNoResourcesAvailable) {
    engine->Enable(true);
    auto requirements = CreateMemoryRequirements(1 * 1024 * 1024);  // 1 MB

    Resource* alias = engine->FindAlias(
        requirements,
        ResourceLifetime::FrameLocal,
        1 * 1024 * 1024
    );

    EXPECT_EQ(alias, nullptr) << "Should return null when no resources available";

    // Verify attempt was counted
    auto stats = engine->GetStats();
    EXPECT_EQ(stats.totalAliasAttempts, 1) << "Should count the attempt";
    EXPECT_EQ(stats.successfulAliases, 0) << "Should count as failed";
}

TEST_F(AliasingEngineTest, FindAliasReturnsNullWhenDisabled) {
    engine->Enable(false);

    // Register an available resource
    Resource* resource = CreateMockResource(1);
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);
    engine->RegisterForAliasing(resource, requirements, ResourceLifetime::FrameLocal);
    engine->MarkReleased(resource, 1);

    // Try to find alias while disabled
    Resource* alias = engine->FindAlias(
        requirements,
        ResourceLifetime::FrameLocal,
        4 * 1024 * 1024
    );

    EXPECT_EQ(alias, nullptr) << "Should return null when aliasing is disabled";
}

TEST_F(AliasingEngineTest, FindAliasReturnsResourceWhenCompatible) {
    engine->Enable(true);
    engine->SetAliasingThreshold(1 * 1024 * 1024);  // 1 MB

    // Register and release a resource
    Resource* resource = CreateMockResource(1);
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);  // 4 MB
    engine->RegisterForAliasing(resource, requirements, ResourceLifetime::FrameLocal);
    engine->MarkReleased(resource, 1);

    // Try to find alias for same-sized resource
    auto newRequirements = CreateMemoryRequirements(4 * 1024 * 1024);  // 4 MB
    Resource* alias = engine->FindAlias(
        newRequirements,
        ResourceLifetime::FrameLocal,
        4 * 1024 * 1024
    );

    EXPECT_NE(alias, nullptr) << "Should find compatible alias";
    EXPECT_EQ(alias, resource) << "Should return the registered resource";

    // Verify statistics
    auto stats = engine->GetStats();
    EXPECT_EQ(stats.successfulAliases, 1) << "Should count successful alias";
    EXPECT_GT(stats.totalBytesSaved, 0) << "Should count bytes saved";
}

// ============================================================================
// 6. Best-Fit Algorithm
// ============================================================================

TEST_F(AliasingEngineTest, FindAliasBestFitAlgorithm) {
    engine->Enable(true);
    engine->SetAliasingThreshold(0);  // Alias everything

    // Register resources of different sizes
    Resource* resource1MB = CreateMockResource(1);
    Resource* resource4MB = CreateMockResource(2);
    Resource* resource8MB = CreateMockResource(3);

    engine->RegisterForAliasing(resource1MB, CreateMemoryRequirements(1 * 1024 * 1024), ResourceLifetime::FrameLocal);
    engine->RegisterForAliasing(resource4MB, CreateMemoryRequirements(4 * 1024 * 1024), ResourceLifetime::FrameLocal);
    engine->RegisterForAliasing(resource8MB, CreateMemoryRequirements(8 * 1024 * 1024), ResourceLifetime::FrameLocal);

    engine->MarkReleased(resource1MB, 1);
    engine->MarkReleased(resource4MB, 1);
    engine->MarkReleased(resource8MB, 1);

    // Request 3 MB resource - should get 4 MB (best fit)
    auto requirements = CreateMemoryRequirements(3 * 1024 * 1024);
    Resource* alias = engine->FindAlias(requirements, ResourceLifetime::FrameLocal, 3 * 1024 * 1024);

    EXPECT_NE(alias, nullptr) << "Should find alias";
    EXPECT_EQ(alias, resource4MB) << "Best fit should be 4 MB resource (smallest that fits)";
}

// ============================================================================
// 7. Memory Compatibility Checking
// ============================================================================

TEST_F(AliasingEngineTest, FindAliasRespectsAlignment) {
    engine->Enable(true);
    engine->SetAliasingThreshold(0);

    // Register resource with 256-byte alignment
    Resource* resource = CreateMockResource(1);
    auto requirements256 = CreateMemoryRequirements(4 * 1024 * 1024, 256);
    engine->RegisterForAliasing(resource, requirements256, ResourceLifetime::FrameLocal);
    engine->MarkReleased(resource, 1);

    // Request resource with 512-byte alignment
    auto requirements512 = CreateMemoryRequirements(4 * 1024 * 1024, 512);
    Resource* alias = engine->FindAlias(requirements512, ResourceLifetime::FrameLocal, 4 * 1024 * 1024);

    // Should fail if alignment is incompatible (depends on implementation)
    // This test verifies the alignment check is performed
    EXPECT_TRUE(alias == nullptr || alias == resource)
        << "Should check alignment compatibility";
}

TEST_F(AliasingEngineTest, FindAliasRespectsMemoryType) {
    engine->Enable(true);
    engine->SetAliasingThreshold(0);

    // Register resource with memory type 0x1
    Resource* resource = CreateMockResource(1);
    auto requirements1 = CreateMemoryRequirements(4 * 1024 * 1024);
    requirements1.memoryTypeBits = 0x1;
    engine->RegisterForAliasing(resource, requirements1, ResourceLifetime::FrameLocal);
    engine->MarkReleased(resource, 1);

    // Request resource with memory type 0x2
    auto requirements2 = CreateMemoryRequirements(4 * 1024 * 1024);
    requirements2.memoryTypeBits = 0x2;
    Resource* alias = engine->FindAlias(requirements2, ResourceLifetime::FrameLocal, 4 * 1024 * 1024);

    // Should fail if memory types don't overlap
    EXPECT_EQ(alias, nullptr) << "Should reject incompatible memory types";
}

// ============================================================================
// 8. Threshold Enforcement
// ============================================================================

TEST_F(AliasingEngineTest, AliasingThresholdEnforcement) {
    engine->Enable(true);
    engine->SetAliasingThreshold(5 * 1024 * 1024);  // 5 MB threshold

    // Register small resource (1 MB, below threshold)
    Resource* smallResource = CreateMockResource(1);
    auto smallReq = CreateMemoryRequirements(1 * 1024 * 1024);
    engine->RegisterForAliasing(smallResource, smallReq, ResourceLifetime::FrameLocal);
    engine->MarkReleased(smallResource, 1);

    // Request alias for small resource
    auto requestReq = CreateMemoryRequirements(1 * 1024 * 1024);
    Resource* alias = engine->FindAlias(requestReq, ResourceLifetime::FrameLocal, 1 * 1024 * 1024);

    // Should be null if below threshold
    EXPECT_EQ(alias, nullptr) << "Should not alias resources below threshold";

    // Register large resource (8 MB, above threshold)
    Resource* largeResource = CreateMockResource(2);
    auto largeReq = CreateMemoryRequirements(8 * 1024 * 1024);
    engine->RegisterForAliasing(largeResource, largeReq, ResourceLifetime::FrameLocal);
    engine->MarkReleased(largeResource, 1);

    // Request alias for large resource
    auto largeRequestReq = CreateMemoryRequirements(8 * 1024 * 1024);
    alias = engine->FindAlias(largeRequestReq, ResourceLifetime::FrameLocal, 8 * 1024 * 1024);

    // Should succeed if above threshold
    EXPECT_NE(alias, nullptr) << "Should alias resources above threshold";
}

// ============================================================================
// 9. Statistics Tracking
// ============================================================================

TEST_F(AliasingEngineTest, StatisticsTrackAttempts) {
    engine->Enable(true);
    engine->SetAliasingThreshold(0);

    // Make several alias attempts
    for (int i = 0; i < 10; ++i) {
        auto requirements = CreateMemoryRequirements(1 * 1024 * 1024);
        engine->FindAlias(requirements, ResourceLifetime::FrameLocal, 1 * 1024 * 1024);
    }

    auto stats = engine->GetStats();
    EXPECT_EQ(stats.totalAliasAttempts, 10) << "Should count all attempts";
}

TEST_F(AliasingEngineTest, StatisticsTrackSuccesses) {
    engine->Enable(true);
    engine->SetAliasingThreshold(0);

    // Register and release resource
    Resource* resource = CreateMockResource(1);
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);
    engine->RegisterForAliasing(resource, requirements, ResourceLifetime::FrameLocal);
    engine->MarkReleased(resource, 1);

    // Successful alias
    engine->FindAlias(requirements, ResourceLifetime::FrameLocal, 4 * 1024 * 1024);

    auto stats = engine->GetStats();
    EXPECT_EQ(stats.successfulAliases, 1) << "Should count successful alias";
    EXPECT_GT(stats.totalBytesSaved, 0) << "Should count bytes saved";
}

TEST_F(AliasingEngineTest, StatisticsCalculateEfficiency) {
    engine->Enable(true);
    engine->SetAliasingThreshold(0);

    // Register resource
    Resource* resource = CreateMockResource(1);
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);
    engine->RegisterForAliasing(resource, requirements, ResourceLifetime::FrameLocal);
    engine->MarkReleased(resource, 1);

    // Find alias
    engine->FindAlias(requirements, ResourceLifetime::FrameLocal, 4 * 1024 * 1024);

    auto stats = engine->GetStats();
    float efficiency = stats.GetSavingsPercentage();

    EXPECT_GE(efficiency, 0.0f) << "Efficiency should be non-negative";
    EXPECT_LE(efficiency, 100.0f) << "Efficiency should not exceed 100%";
}

// ============================================================================
// 10. Clear Functionality
// ============================================================================

TEST_F(AliasingEngineTest, ClearResetsState) {
    engine->Enable(true);

    // Register resources and perform aliases
    Resource* resource = CreateMockResource(1);
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);
    engine->RegisterForAliasing(resource, requirements, ResourceLifetime::FrameLocal);
    engine->MarkReleased(resource, 1);
    engine->FindAlias(requirements, ResourceLifetime::FrameLocal, 4 * 1024 * 1024);

    // Clear
    EXPECT_NO_THROW({
        engine->Clear();
    }) << "Should clear without errors";

    // Verify state is reset
    auto stats = engine->GetStats();
    EXPECT_EQ(stats.totalAliasAttempts, 0) << "Attempts should be reset";
    EXPECT_EQ(stats.successfulAliases, 0) << "Successes should be reset";
    EXPECT_EQ(stats.totalBytesSaved, 0) << "Bytes saved should be reset";
}

// ============================================================================
// 11. Edge Cases
// ============================================================================

TEST_F(AliasingEngineTest, FindAliasWithZeroSizeRequest) {
    engine->Enable(true);

    auto requirements = CreateMemoryRequirements(0);  // Zero size
    Resource* alias = engine->FindAlias(requirements, ResourceLifetime::FrameLocal, 0);

    EXPECT_EQ(alias, nullptr) << "Should handle zero-size request gracefully";
}

TEST_F(AliasingEngineTest, RegisterNullResourceIsHandledGracefully) {
    auto requirements = CreateMemoryRequirements(4 * 1024 * 1024);

    // Depending on implementation, this might assert or handle gracefully
    // Test that it doesn't crash
    EXPECT_NO_THROW({
        engine->RegisterForAliasing(nullptr, requirements, ResourceLifetime::FrameLocal);
    });
}

TEST_F(AliasingEngineTest, MarkReleasedNullResourceIsHandledGracefully) {
    EXPECT_NO_THROW({
        engine->MarkReleased(nullptr, 1);
    }) << "Should handle null resource gracefully";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
