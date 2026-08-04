// Semantic Shader Wiring S0 — feature-tagged SDI variant merge
// (docs/plans/2026-08-03-semantic-shader-wiring.md, undertow repo)
//
// SDI today models ONE compiled variant per reflected SPIR-V; #ifdef-gated
// bindings (VIXEN_B1_OCCLUSION_CULL binding 36, VIXEN_GPU_TRACE_HOOKS binding
// 14) are invisible as an axis, so gating leaks into the graph builder as
// hand-duplicated flag blocks. These tests pin the merge: N (featureSet,
// reflection) variants -> ONE interface where every member carries the feature
// predicate derived mechanically by diffing the variants.
//
// Pure data-level tests: no GPU, no glslang compile, no Vulkan device.

#include <gtest/gtest.h>
#include "SdiVariantMerge.h"
#include "SpirvInterfaceGenerator.h"

#include <algorithm>

using namespace ShaderManagement;

namespace {

SpirvDescriptorBinding MakeBinding(uint32_t set, uint32_t binding,
                                   const std::string& name,
                                   VkDescriptorType type) {
    SpirvDescriptorBinding b;
    b.set = set;
    b.binding = binding;
    b.name = name;
    b.descriptorType = type;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    return b;
}

SpirvStructMember MakePushMember(const std::string& name, uint32_t offset,
                                 uint32_t sizeInBytes) {
    SpirvStructMember m;
    m.name = name;
    m.offset = offset;
    m.type.baseType = SpirvTypeInfo::BaseType::Float;
    m.type.width = 32;
    m.type.sizeInBytes = sizeInBytes;
    return m;
}

// A reflection with the given set-0 bindings and optional push members.
SpirvReflectionData MakeData(const std::vector<SpirvDescriptorBinding>& bindings,
                             const std::vector<SpirvStructMember>& pushMembers = {}) {
    SpirvReflectionData data;
    data.programName = "TestProgram";
    data.descriptorSets[0] = bindings;
    if (!pushMembers.empty()) {
        SpirvPushConstantRange pc;
        pc.name = "pc";
        pc.offset = 0;
        pc.structDef.name = "PushConstants";
        pc.structDef.members = pushMembers;
        uint32_t end = 0;
        for (const auto& m : pushMembers)
            end = std::max(end, m.offset + m.type.sizeInBytes);
        pc.size = end;
        pc.structDef.sizeInBytes = end;
        data.pushConstants.push_back(pc);
    }
    return data;
}

SdiVariant MakeVariant(std::vector<std::string> features,
                       SpirvReflectionData data) {
    SdiVariant v;
    v.features = std::move(features);
    v.data = std::move(data);
    return v;
}

const SdiMergedBinding* FindMerged(const SdiMergedInterface& merged,
                                   uint32_t set, uint32_t binding) {
    for (const auto& b : merged.bindings)
        if (b.binding.set == set && b.binding.binding == binding) return &b;
    return nullptr;
}

const SdiMergedPushMember* FindPush(const SdiMergedInterface& merged,
                                    const std::string& name) {
    for (const auto& m : merged.pushMembers)
        if (m.member.name == name) return &m;
    return nullptr;
}

} // namespace

// --- Presence/predicate derivation -----------------------------------------

TEST(SdiVariantMerge, SingleVariantIsAllUnconditional) {
    auto base = MakeData({
        MakeBinding(0, 0, "outputImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        MakeBinding(0, 1, "esvoNodes", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });

    auto result = MergeSdiVariants("BodyInstanceRayMarch",
                                   {MakeVariant({}, base)});

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_TRUE(result.merged.featureAxis.empty());
    ASSERT_EQ(result.merged.bindings.size(), 2u);
    for (const auto& b : result.merged.bindings)
        EXPECT_TRUE(b.requiredFeatures.empty()) << b.binding.name;
}

TEST(SdiVariantMerge, GatedBindingGetsFeaturePredicate) {
    auto baseline = MakeData({
        MakeBinding(0, 0, "outputImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        MakeBinding(0, 1, "esvoNodes", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    auto b1 = MakeData({
        MakeBinding(0, 0, "outputImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        MakeBinding(0, 1, "esvoNodes", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        MakeBinding(0, 36, "depthDistanceImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
    });

    auto result = MergeSdiVariants(
        "BodyInstanceRayMarch",
        {MakeVariant({}, baseline),
         MakeVariant({"VIXEN_B1_OCCLUSION_CULL"}, b1)});

    ASSERT_TRUE(result.success) << result.errorMessage;
    ASSERT_EQ(result.merged.featureAxis,
              std::vector<std::string>{"VIXEN_B1_OCCLUSION_CULL"});

    const auto* depth = FindMerged(result.merged, 0, 36);
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(depth->requiredFeatures,
              std::vector<std::string>{"VIXEN_B1_OCCLUSION_CULL"});

    const auto* nodes = FindMerged(result.merged, 0, 1);
    ASSERT_NE(nodes, nullptr);
    EXPECT_TRUE(nodes->requiredFeatures.empty());

    // Deterministic output order: sorted by (set, binding).
    ASSERT_EQ(result.merged.bindings.size(), 3u);
    EXPECT_EQ(result.merged.bindings.back().binding.binding, 36u);
}

TEST(SdiVariantMerge, BaselineCommonFeaturesAreNotRequirements) {
    // TRACE is injected in BOTH compiled variants -> it is the baseline, not a
    // per-member requirement; only the B1 delta may appear as a predicate.
    auto traced = MakeData({
        MakeBinding(0, 0, "outputImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
    });
    auto tracedB1 = MakeData({
        MakeBinding(0, 0, "outputImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        MakeBinding(0, 36, "depthDistanceImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
    });

    auto result = MergeSdiVariants(
        "BodyInstanceRayMarch",
        {MakeVariant({"VIXEN_GPU_TRACE_HOOKS"}, traced),
         MakeVariant({"VIXEN_GPU_TRACE_HOOKS", "VIXEN_B1_OCCLUSION_CULL"}, tracedB1)});

    ASSERT_TRUE(result.success) << result.errorMessage;

    const auto* out = FindMerged(result.merged, 0, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_TRUE(out->requiredFeatures.empty());

    const auto* depth = FindMerged(result.merged, 0, 36);
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(depth->requiredFeatures,
              std::vector<std::string>{"VIXEN_B1_OCCLUSION_CULL"});
}

TEST(SdiVariantMerge, ConflictingBindingDeclarationsFail) {
    auto a = MakeData({MakeBinding(0, 3, "materials", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    auto b = MakeData({MakeBinding(0, 3, "lightTree", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});

    auto result = MergeSdiVariants("BadProgram",
                                   {MakeVariant({}, a),
                                    MakeVariant({"SOME_FLAG"}, b)});

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("BadProgram"), std::string::npos);
    EXPECT_NE(result.errorMessage.find("materials"), std::string::npos);
    EXPECT_NE(result.errorMessage.find("lightTree"), std::string::npos);
}

TEST(SdiVariantMerge, NonConjunctivePresenceFails) {
    // Present under {A} alone and {B} alone but ABSENT under {A,B}: no feature
    // conjunction reproduces that -> refuse loudly rather than emit a wrong
    // predicate.
    auto with = MakeData({MakeBinding(0, 7, "oddball", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    auto without = MakeData({});

    auto result = MergeSdiVariants(
        "OddProgram",
        {MakeVariant({}, without),
         MakeVariant({"A"}, with),
         MakeVariant({"B"}, with),
         MakeVariant({"A", "B"}, without)});

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("oddball"), std::string::npos);
}

// --- Push-constant members ---------------------------------------------------

TEST(SdiVariantMerge, PushConstantMemberGating) {
    auto baseline = MakeData({}, {
        MakePushMember("viewProj", 0, 64),
        MakePushMember("camPos", 64, 16),
    });
    auto b1 = MakeData({}, {
        MakePushMember("viewProj", 0, 64),
        MakePushMember("camPos", 64, 16),
        MakePushMember("hizDims", 80, 16),
    });

    auto result = MergeSdiVariants(
        "InstanceOcclusionCull",
        {MakeVariant({}, baseline),
         MakeVariant({"VIXEN_B1_OCCLUSION_CULL"}, b1)});

    ASSERT_TRUE(result.success) << result.errorMessage;

    const auto* viewProj = FindPush(result.merged, "viewProj");
    ASSERT_NE(viewProj, nullptr);
    EXPECT_TRUE(viewProj->requiredFeatures.empty());

    const auto* hizDims = FindPush(result.merged, "hizDims");
    ASSERT_NE(hizDims, nullptr);
    EXPECT_EQ(hizDims->requiredFeatures,
              std::vector<std::string>{"VIXEN_B1_OCCLUSION_CULL"});
}

TEST(SdiVariantMerge, PushMemberOffsetConflictFails) {
    auto a = MakeData({}, {MakePushMember("camPos", 0, 16)});
    auto b = MakeData({}, {MakePushMember("camPos", 16, 16)});

    auto result = MergeSdiVariants("BadPush",
                                   {MakeVariant({}, a),
                                    MakeVariant({"F"}, b)});

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("camPos"), std::string::npos);
}

// --- Emission ----------------------------------------------------------------

TEST(SdiVariantMerge, MergedEmissionCarriesFeatureTags) {
    auto baseline = MakeData({
        MakeBinding(0, 1, "esvoNodes", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });
    auto b1 = MakeData({
        MakeBinding(0, 1, "esvoNodes", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        MakeBinding(0, 36, "depthDistanceImage", VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
    });

    auto result = MergeSdiVariants(
        "BodyInstanceRayMarch",
        {MakeVariant({}, baseline),
         MakeVariant({"VIXEN_B1_OCCLUSION_CULL"}, b1)});
    ASSERT_TRUE(result.success) << result.errorMessage;

    SpirvInterfaceGenerator generator;
    std::string code = generator.GenerateMergedToString(result.merged);

    ASSERT_FALSE(code.empty());
    // Program-keyed (not uuid-keyed) namespace under the SDI root.
    EXPECT_NE(code.find("ShaderInterface"), std::string::npos);
    EXPECT_NE(code.find("BodyInstanceRayMarch"), std::string::npos);
    // The gated member carries its predicate; the unconditional one is tagged
    // feature-free. Substring-level pinning only — layout may evolve.
    EXPECT_NE(code.find("VIXEN_B1_OCCLUSION_CULL"), std::string::npos);
    EXPECT_NE(code.find("FEATURE_COUNT = 1"), std::string::npos);
    EXPECT_NE(code.find("FEATURE_COUNT = 0"), std::string::npos);
    // The flat member table for the runtime auto-connect walk (S2 consumer).
    EXPECT_NE(code.find("MEMBERS"), std::string::npos);
    EXPECT_NE(code.find("depthDistanceImage"), std::string::npos);
    EXPECT_NE(code.find("esvoNodes"), std::string::npos);
}

TEST(SdiVariantMerge, MergedEmissionNamesAndIndices) {
    // S1 face: name-keyed aliases for descriptor bindings and INDEX ordinals
    // for push members, so Connect sites write names instead of numbers.
    auto data = MakeData(
        {
            MakeBinding(0, 0, "BodyInstanceBuffer", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            MakeBinding(0, 3, "InstanceSkipMaskBuffer", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        },
        {
            MakePushMember("prevViewProj", 0, 64),
            MakePushMember("prevCamPos", 64, 16),
            MakePushMember("dims", 80, 16),
        });

    auto result = MergeSdiVariants("InstanceOcclusionCull",
                                   {MakeVariant({}, data)});
    ASSERT_TRUE(result.success) << result.errorMessage;

    SpirvInterfaceGenerator generator;
    std::string code = generator.GenerateMergedToString(result.merged);

    // Descriptor aliases: name -> Binding struct, nested in Bind so a
    // fallback-named binding never collides with its layout struct.
    EXPECT_NE(code.find("namespace Bind {"), std::string::npos);
    EXPECT_NE(code.find("using BodyInstanceBuffer = Set0::Binding0;"),
              std::string::npos);
    EXPECT_NE(code.find("using InstanceSkipMaskBuffer = Set0::Binding3;"),
              std::string::npos);
    // Push members carry their field ordinal (offset order) for the
    // PushConstantGathererNode slot index.
    EXPECT_NE(code.find("INDEX = 0"), std::string::npos);
    EXPECT_NE(code.find("INDEX = 1"), std::string::npos);
    EXPECT_NE(code.find("INDEX = 2"), std::string::npos);
}

TEST(SdiVariantMerge, DuplicateMemberNamesSkipAliases) {
    // Two bindings sharing a (fallback) name: Binding structs always emit,
    // but the name-keyed alias would be a redefinition — it must be skipped
    // for BOTH, never emitted twice.
    auto data = MakeData({
        MakeBinding(0, 0, "SharedBlock", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        MakeBinding(0, 1, "SharedBlock", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });

    auto result = MergeSdiVariants("Dup", {MakeVariant({}, data)});
    ASSERT_TRUE(result.success) << result.errorMessage;

    SpirvInterfaceGenerator generator;
    std::string code = generator.GenerateMergedToString(result.merged);

    EXPECT_EQ(code.find("using SharedBlock"), std::string::npos);
    EXPECT_NE(code.find("struct Binding0"), std::string::npos);
    EXPECT_NE(code.find("struct Binding1"), std::string::npos);
}

// --- Access qualifiers (S3 sub-slice 1: derived-hazards prerequisite) -------
// The merged SDI must carry each binding's SPIR-V access mode so sync sets can
// later be DERIVED instead of hand-fed (epoch doc, S3 entry). Access is part
// of the declaration: variants disagreeing on it is a hard error, same as a
// name/type mismatch.

TEST(SdiVariantMerge, MergedBindingCarriesAccess) {
    auto ro = MakeBinding(0, 0, "SrcBuf", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ro.access = SpirvResourceAccess::ReadOnly;
    auto wo = MakeBinding(0, 1, "DstBuf", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    wo.access = SpirvResourceAccess::WriteOnly;
    auto rw = MakeBinding(0, 2, "AccBuf", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    auto result = MergeSdiVariants(
        "AccessCarry", {MakeVariant({}, MakeData({ro, wo, rw}))});
    ASSERT_TRUE(result.success) << result.errorMessage;

    EXPECT_EQ(FindMerged(result.merged, 0, 0)->binding.access,
              SpirvResourceAccess::ReadOnly);
    EXPECT_EQ(FindMerged(result.merged, 0, 1)->binding.access,
              SpirvResourceAccess::WriteOnly);
    EXPECT_EQ(FindMerged(result.merged, 0, 2)->binding.access,
              SpirvResourceAccess::ReadWrite);  // struct default
}

TEST(SdiVariantMerge, ConflictingAccessFailsMerge) {
    auto a = MakeBinding(0, 3, "SharedBuf", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    a.access = SpirvResourceAccess::ReadOnly;
    auto b = MakeBinding(0, 3, "SharedBuf", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    b.access = SpirvResourceAccess::WriteOnly;

    auto result = MergeSdiVariants(
        "AccessConflict",
        {MakeVariant({}, MakeData({a})),
         MakeVariant({"SOME_FEATURE"}, MakeData({b}))});
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("SharedBuf"), std::string::npos);
}
