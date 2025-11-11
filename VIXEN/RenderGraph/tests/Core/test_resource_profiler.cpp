/**
 * @file test_resource_profiler.cpp
 * @brief Comprehensive tests for ResourceProfiler class (Phase H)
 *
 * Coverage: ResourceProfiler.h (Target: 85%+)
 *
 * Tests:
 * - Frame lifecycle (BeginFrame/EndFrame)
 * - Per-node allocation tracking
 * - Per-node release tracking
 * - Statistics aggregation (stack, heap, VRAM)
 * - Aliasing efficiency calculations
 * - Rolling window management (120 frames)
 * - Text export format
 * - JSON export format
 * - Edge cases and error handling
 *
 * Phase H: Per-Node Resource Profiling
 */

#include <gtest/gtest.h>
#include "Core/ResourceProfiler.h"
#include "Core/StackResourceHandle.h"
#include <memory>
#include <string>
#include <sstream>

using namespace Vixen::RenderGraph;

// ============================================================================
// Test Fixture
// ============================================================================

class ResourceProfilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        profiler = std::make_unique<ResourceProfiler>();
    }

    void TearDown() override {
        profiler.reset();
    }

    std::unique_ptr<ResourceProfiler> profiler;

    // Helper: Create mock resource pointer
    Resource* CreateMockResource(int id) {
        return reinterpret_cast<Resource*>(static_cast<uintptr_t>(0x1000 + id));
    }
};

// ============================================================================
// 1. Construction & Initialization
// ============================================================================

TEST_F(ResourceProfilerTest, ConstructorInitializesEmptyProfiler) {
    EXPECT_NE(profiler, nullptr) << "Profiler should be constructed successfully";
}

// ============================================================================
// 2. Frame Lifecycle
// ============================================================================

TEST_F(ResourceProfilerTest, BeginFrameStartsNewFrame) {
    uint64_t frameNumber = 1;

    EXPECT_NO_THROW({
        profiler->BeginFrame(frameNumber);
    }) << "Should begin frame without errors";
}

TEST_F(ResourceProfilerTest, EndFrameCompletesFrame) {
    profiler->BeginFrame(1);

    EXPECT_NO_THROW({
        profiler->EndFrame();
    }) << "Should end frame without errors";
}

TEST_F(ResourceProfilerTest, MultipleFrameLifecycle) {
    // Test multiple frames
    for (uint64_t frame = 0; frame < 10; ++frame) {
        EXPECT_NO_THROW({
            profiler->BeginFrame(frame);
            profiler->EndFrame();
        }) << "Frame " << frame << " should complete without errors";
    }
}

TEST_F(ResourceProfilerTest, EndFrameWithoutBeginIsHandledGracefully) {
    EXPECT_NO_THROW({
        profiler->EndFrame();
    }) << "Ending frame without begin should be handled gracefully";
}

TEST_F(ResourceProfilerTest, MultipleBeginFrameCallsAreHandledGracefully) {
    EXPECT_NO_THROW({
        profiler->BeginFrame(1);
        profiler->BeginFrame(2);  // Second begin without end
    }) << "Multiple begin calls should be handled gracefully";
}

// ============================================================================
// 3. Allocation Recording - Stack
// ============================================================================

TEST_F(ResourceProfilerTest, RecordStackAllocation) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;
    std::string nodeName = "TestNode";

    EXPECT_NO_THROW({
        profiler->RecordAllocation(
            nodeId,
            nodeName,
            ResourceLocation::Stack,
            256,     // bytes
            false    // not aliased
        );
    }) << "Should record stack allocation";

    profiler->EndFrame();

    // Verify statistics
    auto stats = profiler->GetNodeStats(nodeId, 1);
    EXPECT_EQ(stats.stackAllocations, 1) << "Should count 1 stack allocation";
    EXPECT_EQ(stats.stackBytesUsed, 256) << "Should track 256 bytes";
}

TEST_F(ResourceProfilerTest, RecordMultipleStackAllocations) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;
    std::string nodeName = "TestNode";

    // Record multiple allocations
    for (int i = 0; i < 5; ++i) {
        profiler->RecordAllocation(nodeId, nodeName, ResourceLocation::Stack, 128, false);
    }

    profiler->EndFrame();

    auto stats = profiler->GetNodeStats(nodeId, 1);
    EXPECT_EQ(stats.stackAllocations, 5) << "Should count 5 allocations";
    EXPECT_EQ(stats.stackBytesUsed, 5 * 128) << "Should track total bytes";
}

// ============================================================================
// 4. Allocation Recording - Heap
// ============================================================================

TEST_F(ResourceProfilerTest, RecordHeapAllocation) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;
    std::string nodeName = "TestNode";

    profiler->RecordAllocation(
        nodeId,
        nodeName,
        ResourceLocation::Heap,
        4096,    // bytes
        false    // not aliased
    );

    profiler->EndFrame();

    auto stats = profiler->GetNodeStats(nodeId, 1);
    EXPECT_EQ(stats.heapAllocations, 1) << "Should count 1 heap allocation";
    EXPECT_EQ(stats.heapBytesUsed, 4096) << "Should track 4096 bytes";
}

// ============================================================================
// 5. Allocation Recording - VRAM
// ============================================================================

TEST_F(ResourceProfilerTest, RecordVRAMAllocation) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;
    std::string nodeName = "TestNode";

    profiler->RecordAllocation(
        nodeId,
        nodeName,
        ResourceLocation::VRAM,
        64 * 1024 * 1024,  // 64 MB
        false              // not aliased
    );

    profiler->EndFrame();

    auto stats = profiler->GetNodeStats(nodeId, 1);
    EXPECT_EQ(stats.vramAllocations, 1) << "Should count 1 VRAM allocation";
    EXPECT_EQ(stats.vramBytesUsed, 64 * 1024 * 1024) << "Should track 64 MB";
}

TEST_F(ResourceProfilerTest, RecordVRAMAllocationWithAliasing) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;
    std::string nodeName = "TestNode";

    profiler->RecordAllocation(
        nodeId,
        nodeName,
        ResourceLocation::VRAM,
        32 * 1024 * 1024,  // 32 MB
        true               // aliased!
    );

    profiler->EndFrame();

    auto stats = profiler->GetNodeStats(nodeId, 1);
    EXPECT_EQ(stats.vramAllocations, 1) << "Should count 1 VRAM allocation";
    EXPECT_EQ(stats.aliasedAllocations, 1) << "Should count 1 aliased allocation";
    EXPECT_EQ(stats.bytesSavedViaAliasing, 32 * 1024 * 1024) << "Should count bytes saved";
}

// ============================================================================
// 6. Release Recording
// ============================================================================

TEST_F(ResourceProfilerTest, RecordRelease) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;
    Resource* resource = CreateMockResource(1);

    EXPECT_NO_THROW({
        profiler->RecordRelease(nodeId, resource);
    }) << "Should record release without errors";

    profiler->EndFrame();
}

TEST_F(ResourceProfilerTest, RecordMultipleReleases) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;

    for (int i = 0; i < 10; ++i) {
        Resource* resource = CreateMockResource(i);
        profiler->RecordRelease(nodeId, resource);
    }

    profiler->EndFrame();
    SUCCEED();
}

// ============================================================================
// 7. Multi-Node Statistics
// ============================================================================

TEST_F(ResourceProfilerTest, TrackMultipleNodes) {
    profiler->BeginFrame(1);

    // Record allocations for different nodes
    profiler->RecordAllocation(10, "Node10", ResourceLocation::Stack, 256, false);
    profiler->RecordAllocation(20, "Node20", ResourceLocation::Heap, 4096, false);
    profiler->RecordAllocation(30, "Node30", ResourceLocation::VRAM, 16 * 1024 * 1024, false);

    profiler->EndFrame();

    // Verify each node's stats
    auto stats10 = profiler->GetNodeStats(10, 1);
    EXPECT_EQ(stats10.stackBytesUsed, 256) << "Node 10 should have 256 bytes stack";

    auto stats20 = profiler->GetNodeStats(20, 1);
    EXPECT_EQ(stats20.heapBytesUsed, 4096) << "Node 20 should have 4096 bytes heap";

    auto stats30 = profiler->GetNodeStats(30, 1);
    EXPECT_EQ(stats30.vramBytesUsed, 16 * 1024 * 1024) << "Node 30 should have 16 MB VRAM";
}

TEST_F(ResourceProfilerTest, GetAllNodeStatsReturnsAllNodes) {
    profiler->BeginFrame(1);

    // Record allocations for multiple nodes
    profiler->RecordAllocation(10, "Node10", ResourceLocation::Stack, 256, false);
    profiler->RecordAllocation(20, "Node20", ResourceLocation::Heap, 4096, false);
    profiler->RecordAllocation(30, "Node30", ResourceLocation::VRAM, 8 * 1024 * 1024, false);

    profiler->EndFrame();

    auto allStats = profiler->GetAllNodeStats(1);
    EXPECT_EQ(allStats.size(), 3) << "Should return stats for 3 nodes";
}

// ============================================================================
// 8. Frame Statistics
// ============================================================================

TEST_F(ResourceProfilerTest, GetFrameStatsAggregatesAllNodes) {
    profiler->BeginFrame(1);

    // Record allocations across multiple nodes
    profiler->RecordAllocation(10, "Node10", ResourceLocation::Stack, 256, false);
    profiler->RecordAllocation(20, "Node20", ResourceLocation::Stack, 512, false);
    profiler->RecordAllocation(30, "Node30", ResourceLocation::Heap, 4096, false);
    profiler->RecordAllocation(40, "Node40", ResourceLocation::VRAM, 32 * 1024 * 1024, true);  // Aliased

    profiler->EndFrame();

    auto frameStats = profiler->GetFrameStats(1);
    EXPECT_EQ(frameStats.totalStackBytes, 256 + 512) << "Should aggregate stack bytes";
    EXPECT_EQ(frameStats.totalHeapBytes, 4096) << "Should aggregate heap bytes";
    EXPECT_EQ(frameStats.totalVRAMBytes, 32 * 1024 * 1024) << "Should aggregate VRAM bytes";
    EXPECT_EQ(frameStats.totalBytesSavedViaAliasing, 32 * 1024 * 1024) << "Should track aliasing savings";
}

// ============================================================================
// 9. Aliasing Efficiency Calculations
// ============================================================================

TEST_F(ResourceProfilerTest, NodeStatsCalculateAliasingEfficiency) {
    profiler->BeginFrame(1);

    uint32_t nodeId = 42;

    // Allocate 100 MB, 50 MB saved via aliasing = 33.3% efficiency
    profiler->RecordAllocation(nodeId, "TestNode", ResourceLocation::VRAM, 50 * 1024 * 1024, false);
    profiler->RecordAllocation(nodeId, "TestNode", ResourceLocation::VRAM, 50 * 1024 * 1024, true);

    profiler->EndFrame();

    auto stats = profiler->GetNodeStats(nodeId, 1);
    float efficiency = stats.GetAliasingEfficiency();

    EXPECT_GE(efficiency, 0.0f) << "Efficiency should be non-negative";
    EXPECT_LE(efficiency, 100.0f) << "Efficiency should not exceed 100%";
    EXPECT_NEAR(efficiency, 33.3f, 1.0f) << "Efficiency should be ~33.3%";
}

TEST_F(ResourceProfilerTest, FrameStatsCalculateAliasingEfficiency) {
    profiler->BeginFrame(1);

    // Total 100 MB VRAM, 25 MB saved = 20% efficiency
    profiler->RecordAllocation(10, "Node10", ResourceLocation::VRAM, 75 * 1024 * 1024, false);
    profiler->RecordAllocation(20, "Node20", ResourceLocation::VRAM, 25 * 1024 * 1024, true);

    profiler->EndFrame();

    auto frameStats = profiler->GetFrameStats(1);
    float efficiency = frameStats.GetAliasingEfficiency();

    EXPECT_GE(efficiency, 0.0f);
    EXPECT_LE(efficiency, 100.0f);
    EXPECT_NEAR(efficiency, 20.0f, 1.0f) << "Efficiency should be ~20%";
}

// ============================================================================
// 10. Rolling Window Management
// ============================================================================

TEST_F(ResourceProfilerTest, RollingWindowKeeps120Frames) {
    // Record 150 frames (exceeds 120 limit)
    for (uint64_t frame = 0; frame < 150; ++frame) {
        profiler->BeginFrame(frame);
        profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 256, false);
        profiler->EndFrame();
    }

    // Oldest frames (0-29) should be discarded
    // Frames 30-149 should be kept
    auto oldStats = profiler->GetNodeStats(42, 29);
    auto recentStats = profiler->GetNodeStats(42, 149);

    // Expect old frame to have no data (or default values)
    EXPECT_EQ(oldStats.stackBytesUsed, 0) << "Old frame should be evicted";

    // Expect recent frame to have data
    EXPECT_EQ(recentStats.stackBytesUsed, 256) << "Recent frame should be retained";
}

TEST_F(ResourceProfilerTest, RollingWindowPreservesRecentFrames) {
    // Record 60 frames
    for (uint64_t frame = 0; frame < 60; ++frame) {
        profiler->BeginFrame(frame);
        profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 128, false);
        profiler->EndFrame();
    }

    // All frames should be retained (within 120 limit)
    for (uint64_t frame = 0; frame < 60; ++frame) {
        auto stats = profiler->GetNodeStats(42, frame);
        EXPECT_EQ(stats.stackBytesUsed, 128) << "Frame " << frame << " should be retained";
    }
}

// ============================================================================
// 11. Text Export
// ============================================================================

TEST_F(ResourceProfilerTest, ExportAsTextGeneratesValidOutput) {
    profiler->BeginFrame(1);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 256, false);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::VRAM, 64 * 1024 * 1024, true);
    profiler->EndFrame();

    std::string output = profiler->ExportAsText(1);

    EXPECT_FALSE(output.empty()) << "Text export should not be empty";
    EXPECT_NE(output.find("Frame #1"), std::string::npos) << "Should contain frame number";
    EXPECT_NE(output.find("TestNode"), std::string::npos) << "Should contain node name";
    EXPECT_NE(output.find("Stack:"), std::string::npos) << "Should contain stack stats";
    EXPECT_NE(output.find("VRAM:"), std::string::npos) << "Should contain VRAM stats";
}

TEST_F(ResourceProfilerTest, ExportAsTextHandlesEmptyFrame) {
    profiler->BeginFrame(1);
    profiler->EndFrame();

    std::string output = profiler->ExportAsText(1);

    EXPECT_FALSE(output.empty()) << "Should generate output even for empty frame";
    EXPECT_NE(output.find("Frame #1"), std::string::npos) << "Should contain frame number";
}

// ============================================================================
// 12. JSON Export
// ============================================================================

TEST_F(ResourceProfilerTest, ExportAsJSONGeneratesValidJSON) {
    profiler->BeginFrame(1);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 256, false);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::VRAM, 64 * 1024 * 1024, true);
    profiler->EndFrame();

    std::string json = profiler->ExportAsJSON(1);

    EXPECT_FALSE(json.empty()) << "JSON export should not be empty";
    EXPECT_NE(json.find("\"frameNumber\":"), std::string::npos) << "Should contain frameNumber field";
    EXPECT_NE(json.find("\"nodes\":"), std::string::npos) << "Should contain nodes array";
    EXPECT_NE(json.find("\"totals\":"), std::string::npos) << "Should contain totals object";
    EXPECT_NE(json.find("TestNode"), std::string::npos) << "Should contain node name";
}

TEST_F(ResourceProfilerTest, ExportAsJSONHandlesEmptyFrame) {
    profiler->BeginFrame(1);
    profiler->EndFrame();

    std::string json = profiler->ExportAsJSON(1);

    EXPECT_FALSE(json.empty()) << "Should generate JSON even for empty frame";
    EXPECT_NE(json.find("\"frameNumber\": 1"), std::string::npos) << "Should contain frame number";
}

TEST_F(ResourceProfilerTest, ExportAsJSONContainsAllFields) {
    profiler->BeginFrame(1);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 256, false);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::Heap, 4096, false);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::VRAM, 32 * 1024 * 1024, true);
    profiler->EndFrame();

    std::string json = profiler->ExportAsJSON(1);

    // Check for expected JSON fields
    EXPECT_NE(json.find("\"nodeId\""), std::string::npos);
    EXPECT_NE(json.find("\"nodeName\""), std::string::npos);
    EXPECT_NE(json.find("\"stackBytes\""), std::string::npos);
    EXPECT_NE(json.find("\"heapBytes\""), std::string::npos);
    EXPECT_NE(json.find("\"vramBytes\""), std::string::npos);
    EXPECT_NE(json.find("\"aliasedAllocations\""), std::string::npos);
    EXPECT_NE(json.find("\"bytesSavedViaAliasing\""), std::string::npos);
}

// ============================================================================
// 13. Edge Cases
// ============================================================================

TEST_F(ResourceProfilerTest, RecordAllocationWithZeroBytes) {
    profiler->BeginFrame(1);

    EXPECT_NO_THROW({
        profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 0, false);
    }) << "Should handle zero-byte allocation gracefully";

    profiler->EndFrame();

    auto stats = profiler->GetNodeStats(42, 1);
    EXPECT_EQ(stats.stackBytesUsed, 0) << "Should track 0 bytes";
}

TEST_F(ResourceProfilerTest, GetNodeStatsForNonExistentNode) {
    profiler->BeginFrame(1);
    profiler->EndFrame();

    auto stats = profiler->GetNodeStats(999, 1);

    // Should return default-initialized stats
    EXPECT_EQ(stats.nodeId, 0) << "Non-existent node should return default stats";
    EXPECT_EQ(stats.stackBytesUsed, 0);
    EXPECT_EQ(stats.heapBytesUsed, 0);
    EXPECT_EQ(stats.vramBytesUsed, 0);
}

TEST_F(ResourceProfilerTest, GetNodeStatsForNonExistentFrame) {
    profiler->BeginFrame(1);
    profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 256, false);
    profiler->EndFrame();

    // Query non-existent frame
    auto stats = profiler->GetNodeStats(42, 999);

    EXPECT_EQ(stats.stackBytesUsed, 0) << "Non-existent frame should return default stats";
}

TEST_F(ResourceProfilerTest, RecordReleaseWithNullResource) {
    profiler->BeginFrame(1);

    EXPECT_NO_THROW({
        profiler->RecordRelease(42, nullptr);
    }) << "Should handle null resource gracefully";

    profiler->EndFrame();
}

// ============================================================================
// 14. Clear Functionality
// ============================================================================

TEST_F(ResourceProfilerTest, ClearResetsAllData) {
    // Record some data
    for (uint64_t frame = 0; frame < 10; ++frame) {
        profiler->BeginFrame(frame);
        profiler->RecordAllocation(42, "TestNode", ResourceLocation::Stack, 256, false);
        profiler->EndFrame();
    }

    // Clear
    EXPECT_NO_THROW({
        profiler->Clear();
    }) << "Should clear without errors";

    // Verify data is cleared
    auto stats = profiler->GetNodeStats(42, 9);
    EXPECT_EQ(stats.stackBytesUsed, 0) << "Data should be cleared";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
