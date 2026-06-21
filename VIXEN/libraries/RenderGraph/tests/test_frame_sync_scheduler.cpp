// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/FrameSyncSchedule.h"
#include "Core/FrameSyncScheduler.h"
#include "Core/ResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
using namespace Vixen::RenderGraph;

// ============================================================================
// Minimal mocks for adapter tests (mirrored from test_wave_scheduler.cpp)
// ============================================================================

namespace {

class MockResource2 : public Resource {
public:
    MockResource2() = default;
    std::string debugName;
};

class MockNodeType2 : public NodeType {
public:
    explicit MockNodeType2(const std::string& name) : NodeType(name) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<NodeInstance>(instanceName, const_cast<MockNodeType2*>(this));
    }
};

void AddOutput2(NodeInstance* node, Resource* resource, size_t slotIndex = 0) {
    auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
    if (bundles.empty()) bundles.push_back({});
    if (bundles[0].outputs.size() <= slotIndex)
        bundles[0].outputs.resize(slotIndex + 1, nullptr);
    bundles[0].outputs[slotIndex] = resource;
}

void AddInput2(NodeInstance* node, Resource* resource, size_t slotIndex = 0) {
    auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
    if (bundles.empty()) bundles.push_back({});
    if (bundles[0].inputs.size() <= slotIndex)
        bundles[0].inputs.resize(slotIndex + 1, nullptr);
    bundles[0].inputs[slotIndex] = resource;
}

} // namespace

TEST(FrameSyncScheduleTypes, DefaultScheduleIsEmptyAndInvalid) {
    FrameSyncSchedule s;
    EXPECT_FALSE(s.valid);
    EXPECT_TRUE(s.groups.empty());
    EXPECT_TRUE(s.edges.empty());
    EXPECT_EQ(s.timelineValuesPerFrame, 0u);
}

TEST(FrameSyncScheduleTypes, GroupBarrierDistinguishesImageVsBuffer) {
    GroupBarrier b{};
    b.isImage = true;
    b.src = ResolveAccess(AccessKind::ComputeStorageWrite);
    b.dst = ResolveAccess(AccessKind::FragmentSampledRead);
    EXPECT_EQ(b.src.layout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(b.dst.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

static const Resource* R(int i) { return reinterpret_cast<const Resource*>(0x1000 + i); }

TEST(FrameSyncCore, ComputeWriteThenFragmentRead_ImageHandoff) {
    std::vector<ResourceTimeline> timelines = {{
        R(0), /*isImage=*/true,
        {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::FragmentSampledRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(timelines, /*groupCount=*/2);
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.edges.size(), 1u);
    EXPECT_EQ(s.edges[0].fromGroup, 0u);
    EXPECT_EQ(s.edges[0].toGroup, 1u);
    ASSERT_EQ(s.groups.size(), 2u);
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    const GroupBarrier& b = s.groups[1].entryBarriers[0];
    EXPECT_TRUE(b.isImage);
    EXPECT_EQ(b.src.layout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(b.dst.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ASSERT_EQ(s.groups[1].waitEdges.size(), 1u);
    ASSERT_EQ(s.groups[0].signalEdges.size(), 1u);
}

TEST(FrameSyncCore, ReadAfterReadSameLayout_NoSync) {
    std::vector<ResourceTimeline> timelines = {{
        R(0), true,
        {{0, AccessKind::FragmentSampledRead}, {1, AccessKind::FragmentSampledRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(timelines, 2);
    EXPECT_TRUE(s.edges.empty());
    EXPECT_TRUE(s.groups[1].entryBarriers.empty());
}

TEST(FrameSyncCore, BufferRAW_EdgeNoLayout) {
    std::vector<ResourceTimeline> timelines = {{
        R(0), /*isImage=*/false,
        {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::ComputeStorageRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(timelines, 2);
    ASSERT_EQ(s.edges.size(), 1u);
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    EXPECT_FALSE(s.groups[1].entryBarriers[0].isImage);
}

TEST(FrameSyncCore, FanIn_TwoProducersOneConsumer) {
    std::vector<ResourceTimeline> tl = {
        {R(0), false, {{0, AccessKind::ComputeStorageWrite}, {2, AccessKind::ComputeStorageRead}}},
        {R(1), false, {{1, AccessKind::ComputeStorageWrite}, {2, AccessKind::ComputeStorageRead}}},
    };
    FrameSyncSchedule s = BuildScheduleFromTimelines(tl, 3);
    EXPECT_EQ(s.edges.size(), 2u);
    EXPECT_EQ(s.groups[2].waitEdges.size(), 2u);
}

TEST(FrameSyncCore, TimelineOffsetsAreGroupOrdinals) {
    std::vector<ResourceTimeline> tl = {
        {R(0), false, {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::ComputeStorageRead}}},
    };
    FrameSyncSchedule s = BuildScheduleFromTimelines(tl, 2);
    EXPECT_EQ(s.timelineValuesPerFrame, 2u);
    ASSERT_EQ(s.edges.size(), 1u);
    EXPECT_EQ(s.edges[0].timelineOffset, 0u);
}

// ============================================================================
// Adapter tests: FrameSyncScheduler::Build (real graph → schedule)
// ============================================================================

TEST(FrameSyncAdapter, WriterThenReader_ProducesEdge) {
    MockNodeType2 type("mock");
    auto writer = type.CreateInstance("writer");
    auto reader = type.CreateInstance("reader");
    MockResource2 res; res.debugName = "img";

    AddOutput2(writer.get(), &res);   // writer writes res
    AddInput2(reader.get(),  &res);   // reader reads res

    ResourceAccessTracker tracker;
    tracker.AddNode(writer.get());
    tracker.AddNode(reader.get());

    std::vector<NodeInstance*> execOrder = {writer.get(), reader.get()};
    FrameSyncScheduler scheduler;
    ASSERT_TRUE(scheduler.Build(execOrder, tracker));
    const FrameSyncSchedule& s = scheduler.GetSchedule();
    ASSERT_EQ(s.groups.size(), 2u);
    EXPECT_EQ(s.groups[0].node, writer.get());
    EXPECT_EQ(s.groups[1].node, reader.get());
    EXPECT_EQ(s.edges.size(), 1u);   // RAW write->read across groups
}
