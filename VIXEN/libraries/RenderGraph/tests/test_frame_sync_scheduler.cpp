// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/FrameSyncSchedule.h"
#include "Core/FrameSyncScheduler.h"
#include "Core/ResourceAccessTracker.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include <cstdint>
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

static const Resource* R(std::uintptr_t offset) {
    return reinterpret_cast<const Resource*>(std::uintptr_t{0x1000u} + offset);
}

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

// ============================================================================
// AUTO-SYNC P3: FindGroupForNode + declared AccessKind reaches the barrier
// ============================================================================

TEST(FrameSyncP3, FindGroupForNode_ReturnsCorrectGroup) {
    // Construct a schedule with two groups and verify FindGroupForNode works.
    std::vector<ResourceTimeline> timelines = {{
        R(0), /*isImage=*/true,
        {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::FragmentSampledRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(timelines, 2);
    ASSERT_TRUE(s.valid);

    // FindGroupForNode requires g.node to be set; set synthetic pointers.
    NodeInstance* fakeNodeA = reinterpret_cast<NodeInstance*>(0xA000);
    NodeInstance* fakeNodeB = reinterpret_cast<NodeInstance*>(0xB000);
    s.groups[0].node = fakeNodeA;
    s.groups[1].node = fakeNodeB;

    const SubmitGroup* gA = FindGroupForNode(s, fakeNodeA);
    const SubmitGroup* gB = FindGroupForNode(s, fakeNodeB);
    const SubmitGroup* gNull = FindGroupForNode(s, reinterpret_cast<NodeInstance*>(0xDEAD));

    ASSERT_NE(gA, nullptr);
    ASSERT_NE(gB, nullptr);
    EXPECT_EQ(gNull, nullptr);
    EXPECT_EQ(gA->groupId, 0u);
    EXPECT_EQ(gB->groupId, 1u);
}

TEST(FrameSyncP3, DeclaredAccessKind_ReachesBarrierLayout) {
    // A timeline carrying ComputeStorageWrite → FragmentSampledRead (image)
    // should produce a barrier whose src.layout is VK_IMAGE_LAYOUT_GENERAL
    // (the real ComputeStorageWrite layout), NOT the provisional default.
    // This test would pass even before P3 because BuildScheduleFromTimelines
    // already accepts explicit AccessKind in the timeline — but when the kind
    // is populated from a slot descriptor (Step 5), the end-to-end path is
    // exercised.  Here we verify the existing BuildScheduleFromTimelines path
    // correctly uses the declared kind (AccessKind::None accesses are excluded).
    std::vector<ResourceTimeline> tl = {{
        R(1), /*isImage=*/true,
        {{0, AccessKind::ComputeStorageWrite}, {1, AccessKind::FragmentSampledRead}}
    }};
    FrameSyncSchedule s = BuildScheduleFromTimelines(tl, 2);
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    const GroupBarrier& b = s.groups[1].entryBarriers[0];
    // ComputeStorageWrite → layout GENERAL (not UNDEFINED from None)
    EXPECT_EQ(b.src.layout, VK_IMAGE_LAYOUT_GENERAL)
        << "declared AccessKind::ComputeStorageWrite must produce GENERAL layout, not the provisional default";
    EXPECT_EQ(b.dst.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// An UNTYPED handle/config passthrough (accessKind=None, not the swapchain) is NOT a
// GPU memory hazard and must NOT bake a timeline edge. This is the anti-deadlock
// invariant (P5b M2): such edges would otherwise point from a non-submitting data node
// that never signals its timeline value, hanging any consumer that waits them. Real
// hazards (declared AccessKind, or the swapchain image) DO bake edges — see the
// FrameSyncCore/FrameSyncP3 BuildScheduleFromTimelines tests (explicit kinds) and
// SwapchainResource_ImageBarriersAndTagging below.
TEST(FrameSyncAdapter, UntypedPassthrough_ProducesNoEdge) {
    MockNodeType2 type("mock");
    auto writer = type.CreateInstance("writer");
    auto reader = type.CreateInstance("reader");
    MockResource2 res; res.debugName = "handle";  // a plain handle passthrough, accessKind None

    AddOutput2(writer.get(), &res);   // writer writes res (accessKind None — no descriptor)
    AddInput2(reader.get(),  &res);   // reader reads res (accessKind None)

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
    EXPECT_EQ(s.edges.size(), 0u)   // untyped passthrough is not a hazard → no timeline edge
        << "an untyped (accessKind=None, non-swapchain) write->read must NOT bake a timeline "
           "edge — only declared-sync or swapchain hazards do (P5b M2 anti-deadlock invariant)";
}

// P5a M2: passing the real swapchain Resource* to Build turns on isImage + acquire/present tagging.
// P5b M2: the swapchain edge bakes from DECLARED AccessKinds (the gate now ignores untyped
// metadata accesses), so the mock writer/reader declare a schema carrying the kinds — exactly
// how the live ComputeDispatchNode (ComputeStorageWrite) declares its swapchain access.
TEST(FrameSyncAdapter, SwapchainResource_ImageBarriersAndTagging) {
    MockNodeType2 writerType("mock_writer");
    MockNodeType2 readerType("mock_reader");
    // Declared sync accesses: writer ComputeStorageWrite (output slot 0), reader ComputeStorageRead (input slot 0).
    Schema writerOut(1);
    writerOut[0].accessKind = AccessKind::ComputeStorageWrite;
    writerType.SetOutputSchema(writerOut);
    Schema readerIn(1);
    readerIn[0].accessKind = AccessKind::ComputeStorageRead;
    readerType.SetInputSchema(readerIn);

    auto writer = writerType.CreateInstance("writer");
    auto reader = readerType.CreateInstance("reader");
    MockResource2 sharedResource; sharedResource.debugName = "swapchain";

    AddOutput2(writer.get(), &sharedResource);  // writer outputs the swapchain resource (declared W)
    AddInput2(reader.get(),  &sharedResource);  // reader consumes it (declared R)

    ResourceAccessTracker tracker;
    tracker.AddNode(writer.get());
    tracker.AddNode(reader.get());

    std::vector<NodeInstance*> execOrder = {writer.get(), reader.get()};
    FrameSyncScheduler scheduler;
    // Pass sharedResource as the swapchain identity — flips isImage=true for that timeline.
    ASSERT_TRUE(scheduler.Build(execOrder, tracker, &sharedResource));
    const FrameSyncSchedule& s = scheduler.GetSchedule();

    ASSERT_EQ(s.groups.size(), 2u);
    ASSERT_EQ(s.edges.size(), 1u);  // RAW hazard produces one edge

    // (a) The consumer group's entryBarriers must contain an image barrier.
    ASSERT_GE(s.groups[1].entryBarriers.size(), 1u);
    EXPECT_TRUE(s.groups[1].entryBarriers[0].isImage)
        << "swapchainResource must be treated as an image (isImage==true in entryBarrier)";

    // (b) The first group touching the swapchain resource is tagged acquireWait,
    //     the last group is tagged presentSignal.
    EXPECT_TRUE(s.groups[0].swapchainAcquireWait)
        << "first group touching swapchain must be tagged swapchainAcquireWait";
    EXPECT_TRUE(s.groups[1].swapchainPresentSignal)
        << "last group touching swapchain must be tagged swapchainPresentSignal";
}

// P5b M4: WSI timeline-law assert.
// The WSI acquire (imageAvailable) and present (renderComplete) synchronisation
// points are binary semaphores — never timeline edges.  The scheduler asserts this
// invariant at Build time.  This test verifies two things:
//
//   (a) The binary flags ARE set correctly (swapchainAcquireWait on the first group,
//       swapchainPresentSignal on the last group) — confirming the tagging pass ran.
//   (b) No timeline SyncEdge on the swapchain resource has its toGroup == the
//       acquire group OR its fromGroup == the present group.  In the 2-group
//       compute→UI graph the single swapchain edge goes from group 0 (compute,
//       acquireWait) to group 1 (UI, presentSignal).  The from-group IS the acquire
//       group and the to-group IS the present group — which is fine (that is the
//       normal compute→UI ordering edge direction).  The WSI law only forbids the
//       reverse: a timeline edge that targets the acquire group (would mean a
//       non-WSI group must signal before the acquirer) or originates from the
//       present group (meaningless — present owns only the binary signal).
//
//   In a plain 2-group compute→UI graph the swapchain edge is
//       fromGroup=0 (acquire), toGroup=1 (present).
//   So toGroup(1)==present is allowed; fromGroup(0)==acquire is allowed.
//   Neither of the forbidden conditions (toGroup==acquireGroupId or
//   fromGroup==presentGroupId) is triggered, confirming the law holds.
TEST(FrameSyncWSILaw, SwapchainAdjacentGraph_WSIBinaryPointsAreTaggedAndNotTimelineEdges) {
    // Two-group graph: compute writes swapchain (ComputeStorageWrite),
    // UI reads it (ColorAttachmentWriteGeneral — same as the live M3 path).
    // Both declare explicit AccessKinds so a timeline edge is baked.
    MockNodeType2 computeType("compute");
    MockNodeType2 uiType("ui");

    // Compute: swapchain output with ComputeStorageWrite.
    Schema computeOut(1);
    computeOut[0].accessKind = AccessKind::ComputeStorageWrite;
    computeType.SetOutputSchema(computeOut);

    // UI: swapchain input with ColorAttachmentWriteGeneral (declared in M3).
    Schema uiIn(1);
    uiIn[0].accessKind = AccessKind::ColorAttachmentWriteGeneral;
    uiType.SetInputSchema(uiIn);

    auto computeNode = computeType.CreateInstance("compute");
    auto uiNode      = uiType.CreateInstance("ui");
    MockResource2 swapchain; swapchain.debugName = "swapchain";

    AddOutput2(computeNode.get(), &swapchain);
    AddInput2(uiNode.get(),       &swapchain);

    ResourceAccessTracker tracker;
    tracker.AddNode(computeNode.get());
    tracker.AddNode(uiNode.get());

    std::vector<NodeInstance*> execOrder = {computeNode.get(), uiNode.get()};
    FrameSyncScheduler scheduler;
    ASSERT_TRUE(scheduler.Build(execOrder, tracker, &swapchain));
    const FrameSyncSchedule& s = scheduler.GetSchedule();

    ASSERT_EQ(s.groups.size(), 2u);

    // (a) Binary flags are set.
    EXPECT_TRUE(s.groups[0].swapchainAcquireWait)
        << "compute group (first) must be tagged swapchainAcquireWait";
    EXPECT_TRUE(s.groups[1].swapchainPresentSignal)
        << "UI group (last) must be tagged swapchainPresentSignal";

    // The timeline edge on the swapchain resource runs compute(0)→UI(1).
    // acquire group = 0, present group = 1.
    const uint32_t acquireGroupId = 0u;
    const uint32_t presentGroupId = 1u;

    // (b) WSI law: no timeline edge on the swapchain resource may have
    //     toGroup == acquireGroupId  (acquire boundary is binary-only)
    //  or fromGroup == presentGroupId (present boundary is binary-only).
    for (const SyncEdge& e : s.edges) {
        if (e.resource != &swapchain) continue;
        EXPECT_NE(e.toGroup, acquireGroupId)
            << "WSI law: no timeline edge may target the acquire group (toGroup == acquireGroupId)";
        EXPECT_NE(e.fromGroup, presentGroupId)
            << "WSI law: no timeline edge may originate from the present group (fromGroup == presentGroupId)";
    }
}

// ============================================================================
// Sampled Lighting Inc3 M5: array-hazard constituent expansion
// ============================================================================
// BufferSyncGathererNode collapses N buffer-sync hazards into ONE array-typed
// Resource* (std::vector<VkBuffer>) so ComputeStageNodeConfig no longer needs a
// fixed named slot per buffer (BUFFER_WRITE/BUFFER_READ_A/BUFFER_READ_B). This
// is ONLY correct if ResourceAccessTracker::AddNode expands that ONE array-
// wrapper Resource* back into N independent per-constituent hazard records
// (Resource::hazardConstituents_) — otherwise the fan-in demo's two INDEPENDENT
// producers (aStage writes only bufA, bStage writes only bufB) would collapse
// into a single indivisible array hazard, and BuildScheduleFromTimelines would
// bake a spurious write-write aStage->bStage edge (from walking one shared
// Resource*'s access list in execution order) instead of the two genuine direct
// edges (bufA: aStage->cStage, bufB: bStage->cStage) the fan-in proof requires.
// These tests exercise ResourceAccessTracker::AddNode directly (the actual
// surgery — BuildScheduleFromTimelines itself is unchanged and already covered
// by FanIn_TwoProducersOneConsumer above) to prove the expansion is correct.

TEST(FrameSyncArrayHazard, ConstituentExpansion_ProducesIndependentPerBufferTimelines) {
    // Mirrors BuildFanInDemoGraph.cpp's real shape: aStage writes a 1-entry array
    // (bufA only) via aWriteGatherer's array Resource*; bStage writes its OWN
    // 1-entry array (bufB only) via bWriteGatherer's array Resource*; cStage reads
    // a 2-entry array (bufA, bufB) via cReadGatherer's array Resource*. THREE
    // DISTINCT array-wrapper Resource*s (one per gatherer instance) — exactly as
    // the real graph wires it — each carrying hazardConstituents_ pointing back
    // to the SAME two underlying buffer Resource*s (bufA, bufB).
    MockNodeType2 aStageType("a_stage"), bStageType("b_stage"), cStageType("c_stage");

    // mutability = ReadWrite is required for an INPUT slot to count as a writer
    // (ResourceAccessTracker::AddNode upgrades ResourceAccessType::Read -> ReadWrite
    // only when desc->mutability == SlotMutability::ReadWrite — the AccessKind alone,
    // e.g. ComputeStorageWrite, does not imply this; see ResourceDescriptor's own
    // "Auto-sync P1: ReadWrite inputs count as writers" doc comment).
    Schema aWriteIn(1);
    aWriteIn[0].accessKind = AccessKind::ComputeStorageWrite;
    aWriteIn[0].mutability = SlotMutability::ReadWrite;
    aStageType.SetInputSchema(aWriteIn);

    Schema bWriteIn(1);
    bWriteIn[0].accessKind = AccessKind::ComputeStorageWrite;
    bWriteIn[0].mutability = SlotMutability::ReadWrite;
    bStageType.SetInputSchema(bWriteIn);

    Schema cReadIn(1);
    cReadIn[0].accessKind = AccessKind::ComputeStorageRead;
    cStageType.SetInputSchema(cReadIn);

    auto aStage = aStageType.CreateInstance("a_stage_inst");
    auto bStage = bStageType.CreateInstance("b_stage_inst");
    auto cStage = cStageType.CreateInstance("c_stage_inst");

    // The two REAL underlying buffers (what BufferSyncGathererNode's own
    // hazardConstituents_ would point at — e.g. bufANode/bufBNode's own
    // StorageBufferNode::STORAGE_BUFFER Resource*s in the live graph).
    MockResource2 bufA; bufA.debugName = "bufA";
    MockResource2 bufB; bufB.debugName = "bufB";

    // The three array-wrapper Resource*s a real BufferSyncGathererNode instance
    // would publish as its BUFFER_ARRAY output — each with its OWN preserved
    // constituent list, exactly as BufferSyncGathererNode::CompileImpl does via
    // Resource::SetHazardConstituents.
    MockResource2 aWriteArray; aWriteArray.debugName = "aWriteArray";
    aWriteArray.SetHazardConstituents({&bufA});
    MockResource2 bWriteArray; bWriteArray.debugName = "bWriteArray";
    bWriteArray.SetHazardConstituents({&bufB});
    MockResource2 cReadArray; cReadArray.debugName = "cReadArray";
    cReadArray.SetHazardConstituents({&bufA, &bufB});

    AddInput2(aStage.get(), &aWriteArray);
    AddInput2(bStage.get(), &bWriteArray);
    AddInput2(cStage.get(), &cReadArray);

    ResourceAccessTracker tracker;
    tracker.AddNode(aStage.get());
    tracker.AddNode(bStage.get());
    tracker.AddNode(cStage.get());

    // The tracker must have expanded EACH array wrapper into its constituents —
    // querying by the TRUE underlying Resource* (bufA/bufB), not the array
    // wrappers, must show the correct writers/readers.
    const ResourceAccessInfo* bufAInfo = tracker.GetAccessInfo(&bufA);
    const ResourceAccessInfo* bufBInfo = tracker.GetAccessInfo(&bufB);
    ASSERT_NE(bufAInfo, nullptr) << "bufA must be tracked via aStage's/cStage's array constituent expansion";
    ASSERT_NE(bufBInfo, nullptr) << "bufB must be tracked via bStage's/cStage's array constituent expansion";

    EXPECT_TRUE(bufAInfo->HasWriter());
    EXPECT_TRUE(bufBInfo->HasWriter());
    // bufA's own accesses: aStage (write) + cStage (read) — NOT bStage.
    EXPECT_EQ(bufAInfo->accesses.size(), 2u)
        << "bufA must show EXACTLY aStage's write + cStage's read, not a spurious bStage access";
    // bufB's own accesses: bStage (write) + cStage (read) — NOT aStage.
    EXPECT_EQ(bufBInfo->accesses.size(), 2u)
        << "bufB must show EXACTLY bStage's write + cStage's read, not a spurious aStage access";

    // The array WRAPPER Resource*s themselves must NOT appear as tracked hazard
    // resources (the expansion replaces them, it doesn't track both).
    EXPECT_EQ(tracker.GetAccessInfo(&aWriteArray), nullptr)
        << "the array-wrapper Resource* itself must not be a separate tracked hazard "
           "once its constituents are expanded — only the true per-buffer Resource*s are";
}

TEST(FrameSyncArrayHazard, ConstituentExpansion_BakesTwoIndependentEdges_NotASpuriousChain) {
    // End-to-end: ResourceAccessTracker::AddNode (the surgery) feeding
    // FrameSyncScheduler::Build (unchanged) must bake the SAME 2-independent-edge
    // shape FanIn_TwoProducersOneConsumer proves at the raw-timeline level —
    // confirming the array-hazard path produces IDENTICAL scheduling to the old
    // fixed-named-slot path, not a spurious aStage->bStage->cStage chain.
    MockNodeType2 aStageType("a_stage"), bStageType("b_stage"), cStageType("c_stage");
    Schema writeIn(1);
    writeIn[0].accessKind = AccessKind::ComputeStorageWrite;
    aStageType.SetInputSchema(writeIn);
    bStageType.SetInputSchema(writeIn);
    Schema readIn(1);
    readIn[0].accessKind = AccessKind::ComputeStorageRead;
    cStageType.SetInputSchema(readIn);

    auto aStage = aStageType.CreateInstance("a_stage_inst");
    auto bStage = bStageType.CreateInstance("b_stage_inst");
    auto cStage = cStageType.CreateInstance("c_stage_inst");

    MockResource2 bufA; bufA.debugName = "bufA";
    MockResource2 bufB; bufB.debugName = "bufB";
    MockResource2 aWriteArray; aWriteArray.debugName = "aWriteArray";
    aWriteArray.SetHazardConstituents({&bufA});
    MockResource2 bWriteArray; bWriteArray.debugName = "bWriteArray";
    bWriteArray.SetHazardConstituents({&bufB});
    MockResource2 cReadArray; cReadArray.debugName = "cReadArray";
    cReadArray.SetHazardConstituents({&bufA, &bufB});

    AddInput2(aStage.get(), &aWriteArray);
    AddInput2(bStage.get(), &bWriteArray);
    AddInput2(cStage.get(), &cReadArray);

    ResourceAccessTracker tracker;
    tracker.AddNode(aStage.get());
    tracker.AddNode(bStage.get());
    tracker.AddNode(cStage.get());

    // Execution order: aStage(0), bStage(1), cStage(2) — mirrors the real graph's
    // topological sort (both producers before the consumer).
    std::vector<NodeInstance*> execOrder = {aStage.get(), bStage.get(), cStage.get()};
    FrameSyncScheduler scheduler;
    ASSERT_TRUE(scheduler.Build(execOrder, tracker));
    const FrameSyncSchedule& s = scheduler.GetSchedule();

    ASSERT_EQ(s.groups.size(), 3u);
    // The genuine 2-wait fan-in: exactly 2 edges, both terminating at cStage (group 2).
    ASSERT_EQ(s.edges.size(), 2u)
        << "must bake exactly 2 independent edges (bufA->cStage, bufB->cStage), not a "
           "3-node aStage->bStage->cStage chain (which would also produce edges.size()==2 "
           "but with the WRONG topology — the from/to assertions below catch that)";
    EXPECT_EQ(s.groups[2].waitEdges.size(), 2u)
        << "cStage (group 2) must have 2 waitEdges — the genuine fan-in this whole test proves";

    // Both edges must originate from a PRODUCER group (0 or 1) and terminate at
    // cStage (group 2) — NOT a spurious aStage(0)->bStage(1) edge.
    for (const SyncEdge& e : s.edges) {
        EXPECT_TRUE(e.fromGroup == 0u || e.fromGroup == 1u)
            << "each edge must originate from aStage or bStage, not cStage";
        EXPECT_EQ(e.toGroup, 2u)
            << "each edge must terminate at cStage (group 2) — a spurious "
               "aStage->bStage edge would show toGroup==1, which this catches";
    }
    // Exactly one edge from each producer (not two edges from the same producer,
    // which would mean bufA and bufB were incorrectly merged into one timeline).
    bool sawFromA = false, sawFromB = false;
    for (const SyncEdge& e : s.edges) {
        if (e.fromGroup == 0u) sawFromA = true;
        if (e.fromGroup == 1u) sawFromB = true;
    }
    EXPECT_TRUE(sawFromA) << "missing the bufA: aStage->cStage edge";
    EXPECT_TRUE(sawFromB) << "missing the bufB: bStage->cStage edge";
}
