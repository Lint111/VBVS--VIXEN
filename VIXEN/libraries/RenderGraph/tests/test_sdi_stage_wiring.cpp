// Semantic Shader Wiring S2 — provider-registry wire plan
// (undertow docs/plans/2026-08-03-semantic-shader-wiring.md)
//
// The shader is the declaration: BuildSdiWirePlan<Ns> walks the merged SDI's
// MEMBERS table (feature-filtered) and resolves every member against the set
// of provider names, producing the exact gatherer slot targets the hand
// Connect blocks write today. Unmatched member = configure-time hard error
// naming the shader, the member, and the available candidates.
//
// Pure data-level tests: no graph, no device. The mock namespace mirrors the
// generated merged-SDI shape (MemberInfo rows + Metadata) — the template is
// duck-typed over exactly that shape.

#include <gtest/gtest.h>
#include "Connection/SdiStageWiring.h"
#include "Connection/ShaderFeature.h"
#include "CapabilityGraph.h"

#include <stdexcept>
#include <string>
#include <unordered_set>

using namespace Vixen::RenderGraph;

// The feature under test, typed — identity is the GLSL define; requirements
// reference CapabilityGraph node names.
inline constexpr ShaderFeature kMockB1Feature{"VIXEN_B1_OCCLUSION_CULL"};

namespace {
inline constexpr const char* kMockReqNames[] = {"mock:required-cap"};
}
inline constexpr ShaderFeature kMockGatedFeature{
    "MOCK_DEVICE_GATED", kMockReqNames, 1};

namespace MockSdi {

inline constexpr const char* const kGatedFeatures[] = {"VIXEN_B1_OCCLUSION_CULL"};

struct MemberInfo {
    const char* name;
    bool isPushMember;
    uint32_t set;
    uint32_t binding;
    uint32_t offset;
    uint32_t featureCount;
    const char* const* features;
};

// Shape mirrors a generated merged header: descriptor members carry BINDING,
// push members carry offset (their gatherer slot is the push ORDINAL — the
// count of push rows before them).
inline constexpr MemberInfo MEMBERS[] = {
    {"BodyInstanceBuffer",     false, 0, 0,  0,  0, nullptr},
    {"OctreeConfigsSSBO",      false, 0, 1,  0,  0, nullptr},
    {"tileMaxImage",           false, 0, 2,  0,  0, nullptr},
    {"depthDistanceImage",     false, 0, 36, 0,  1, kGatedFeatures},
    {"prevViewProj",           true,  0, 0,  0,  0, nullptr},
    {"prevCamPos",             true,  0, 0,  64, 0, nullptr},
    {"dims",                   true,  0, 0,  80, 0, nullptr},
};

struct Metadata {
    static constexpr const char* PROGRAM_NAME = "MockCull";
};

} // namespace MockSdi

namespace {

std::unordered_set<std::string> Providers(std::initializer_list<const char*> names) {
    std::unordered_set<std::string> out;
    for (const char* n : names) out.insert(n);
    return out;
}

const SdiWirePlanEntry* Find(const SdiWirePlan& plan, const std::string& name) {
    for (const auto& e : plan.entries)
        if (name == e.name) return &e;
    return nullptr;
}

} // namespace

TEST(SdiStageWiring, PlanResolvesDescriptorAndPushSlots) {
    auto plan = BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
        /*activeFeatures=*/{},
        Providers({"BodyInstanceBuffer", "OctreeConfigsSSBO", "tileMaxImage",
                   "prevViewProj", "prevCamPos", "dims"}));

    // Descriptor members target their shader binding number.
    const auto* inst = Find(plan, "BodyInstanceBuffer");
    ASSERT_NE(inst, nullptr);
    EXPECT_FALSE(inst->isPush);
    EXPECT_EQ(inst->targetSlot, 0u);

    const auto* tile = Find(plan, "tileMaxImage");
    ASSERT_NE(tile, nullptr);
    EXPECT_EQ(tile->targetSlot, 2u);

    // Push members target their field ORDINAL (offset order), not their offset.
    const auto* vp = Find(plan, "prevViewProj");
    ASSERT_NE(vp, nullptr);
    EXPECT_TRUE(vp->isPush);
    EXPECT_EQ(vp->targetSlot, 0u);

    const auto* dims = Find(plan, "dims");
    ASSERT_NE(dims, nullptr);
    EXPECT_TRUE(dims->isPush);
    EXPECT_EQ(dims->targetSlot, 2u);
}

TEST(SdiStageWiring, PlanFiltersByActiveFeatures) {
    auto base = BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
        {}, Providers({"BodyInstanceBuffer", "OctreeConfigsSSBO", "tileMaxImage",
                       "prevViewProj", "prevCamPos", "dims"}));
    EXPECT_EQ(Find(base, "depthDistanceImage"), nullptr);

    SdiFeatureSet b1Features;
    b1Features.Enable(kMockB1Feature);
    auto b1 = BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
        b1Features,
        Providers({"BodyInstanceBuffer", "OctreeConfigsSSBO", "tileMaxImage",
                   "depthDistanceImage", "prevViewProj", "prevCamPos", "dims"}));
    const auto* depth = Find(b1, "depthDistanceImage");
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(depth->targetSlot, 36u);
}

TEST(SdiStageWiring, EnableIfAvailableConsultsCapabilityGraph) {
    // A typed feature carries requirement references into the capability
    // graph; enabling is gated on ALL of them being available.
    class TestCapability : public Vixen::CapabilityNode {
    public:
        TestCapability(const std::string& name, bool available)
            : CapabilityNode(name), available_(available) {}
    protected:
        bool CheckAvailability() const override { return available_; }
    private:
        bool available_;
    };

    Vixen::CapabilityGraph capsWithout;
    SdiFeatureSet setA;
    EXPECT_FALSE(setA.EnableIfAvailable(kMockGatedFeature, capsWithout));
    EXPECT_FALSE(setA.Contains("MOCK_DEVICE_GATED"));

    Vixen::CapabilityGraph capsWith;
    capsWith.RegisterCapability(
        std::make_shared<TestCapability>("mock:required-cap", true));
    SdiFeatureSet setB;
    EXPECT_TRUE(setB.EnableIfAvailable(kMockGatedFeature, capsWith));
    EXPECT_TRUE(setB.Contains("MOCK_DEVICE_GATED"));

    // Requirement-free features enable unconditionally.
    SdiFeatureSet setC;
    EXPECT_TRUE(setC.EnableIfAvailable(kMockB1Feature, capsWithout));
    EXPECT_TRUE(setC.Contains("VIXEN_B1_OCCLUSION_CULL"));
}

TEST(SdiStageWiring, UnmatchedMemberFailsNamingCandidates) {
    try {
        BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
            {}, Providers({"BodyInstanceBuffer", "prevViewProj", "prevCamPos",
                           "dims", "tileMaxImage"}));  // OctreeConfigsSSBO missing
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("MockCull"), std::string::npos) << msg;
        EXPECT_NE(msg.find("OctreeConfigsSSBO"), std::string::npos) << msg;
        // Candidate list: the provider names are offered for the near-miss hunt.
        EXPECT_NE(msg.find("BodyInstanceBuffer"), std::string::npos) << msg;
    }
}

TEST(SdiStageWiring, GatedMemberAbsentNeedsNoProvider) {
    // Feature inactive -> the gated member is not part of the interface, so a
    // registry without it must NOT error.
    EXPECT_NO_THROW((BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
        {}, Providers({"BodyInstanceBuffer", "OctreeConfigsSSBO", "tileMaxImage",
                       "prevViewProj", "prevCamPos", "dims"}))));
}

TEST(SdiStageWiring, FilterPlanSplitsDescriptorsAndPush) {
    // Multi-stage programs share ONE descriptor gatherer but have per-stage
    // push gatherers (bucketing's three modes): descriptors wire once,
    // push wires per stage — the filter is that seam.
    auto plan = BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
        {}, Providers({"BodyInstanceBuffer", "OctreeConfigsSSBO", "tileMaxImage",
                       "prevViewProj", "prevCamPos", "dims"}));

    auto desc = FilterPlan(plan, SdiWireSet::DescriptorsOnly);
    EXPECT_EQ(desc.entries.size(), 3u);
    for (const auto& e : desc.entries) EXPECT_FALSE(e.isPush);

    auto push = FilterPlan(plan, SdiWireSet::PushOnly);
    EXPECT_EQ(push.entries.size(), 3u);
    for (const auto& e : push.entries) EXPECT_TRUE(e.isPush);

    auto all = FilterPlan(plan, SdiWireSet::All);
    EXPECT_EQ(all.entries.size(), plan.entries.size());
}

TEST(SdiStageWiring, ScopedPlanSkipsOtherHalfProviders) {
    // A DescriptorsOnly plan must not demand PUSH providers (bucketing wires
    // its shared descriptor gatherer before the per-stage push overlays that
    // carry `mode`) — scope gates resolution, not just the output.
    auto desc = BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
        {}, Providers({"BodyInstanceBuffer", "OctreeConfigsSSBO", "tileMaxImage"}),
        SdiWireSet::DescriptorsOnly);
    EXPECT_EQ(desc.entries.size(), 3u);

    auto push = BuildSdiWirePlan<MockSdi::Metadata, MockSdi::MEMBERS>(
        {}, Providers({"prevViewProj", "prevCamPos", "dims"}),
        SdiWireSet::PushOnly);
    EXPECT_EQ(push.entries.size(), 3u);
    EXPECT_EQ(push.entries[0].targetSlot, 0u);
    EXPECT_EQ(push.entries[2].targetSlot, 2u);
}

TEST(SdiStageWiring, RegistryCopyIsIndependent) {
    // Per-stage overlay pattern: copy the shared registry, add the stage's own
    // provider (bucketing's per-stage `mode` constant) — the shared registry
    // must not see it.
    SdiProviderRegistry shared;
    shared.ProvideCustom("viewProj",
                         [](ConnectionBatch&, NodeHandle, uint32_t) {});

    SdiProviderRegistry stage = shared;
    stage.ProvideCustom("mode", [](ConnectionBatch&, NodeHandle, uint32_t) {});

    EXPECT_TRUE(stage.Has("viewProj"));
    EXPECT_TRUE(stage.Has("mode"));
    EXPECT_FALSE(shared.Has("mode"));
}
