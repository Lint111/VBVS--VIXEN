// renderbridge S1: the GPU system manifest the GeneratedSystemStages runner reads.
#include <gtest/gtest.h>

#include "Nodes/GeneratedSystemStage.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeTypeRegistry.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/DeviceNode.h"

using namespace Vixen::RenderGraph;

namespace {

const char* kManifest = R"({
  "schema": "yeroket-gpu-system-manifest/v1",
  "stackHash": "0123456789abcdef",
  "systems": [ {
    "id": "core:Tonemap", "class": "Tonemap", "glsl": "Tonemap.comp", "relaxed": false, "axes": 2,
    "localSize": [8, 8, 1],
    "extent": { "source": "presentation", "kind": "state:frame-targets", "member": "Radiance" },
    "push": { "size": 8, "fields": [
      { "name": "_ExtentX", "offset": 0, "size": 4, "source": "extent" },
      { "name": "_ExtentY", "offset": 4, "size": 4, "source": "extent" } ] },
    "arguments": [
      { "name": "meter", "kind": "Fixture.Meter", "member": "*", "form": "storage", "access": "in",
        "binding": 2, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER", "singleton": false,
        "accessKind": "ComputeStorageRead" },
      { "name": "radiance_current", "kind": "state:frame-targets", "member": "Radiance", "form": "texture",
        "access": "in", "binding": 0, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "read": true, "write": false, "slot": "single",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeStorageRead" },
      { "name": "output_current", "kind": "state:frame-targets", "member": "Output", "form": "texture",
        "access": "ref", "binding": 1, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "format": "VK_FORMAT_R8G8B8A8_UNORM", "read": false, "write": true, "slot": "current",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeStorageWrite" } ] } ],
  "barriers": [], "transfers": [], "allocations": []
})";

// The TAA's shape: a sampled view beside the texture view of one input, a ref texture's current
// and previous slots, and the composition's allocations.
const char* kHistoryManifest = R"({
  "schema": "yeroket-gpu-system-manifest/v1",
  "stackHash": "0123456789abcdef",
  "systems": [ {
    "id": "core:Resolve", "class": "Resolve", "glsl": "Resolve.comp", "relaxed": false, "axes": 2,
    "localSize": [8, 8, 1],
    "extent": { "source": "presentation", "kind": "state:frame-targets", "member": "Radiance" },
    "push": { "size": 8, "fields": [
      { "name": "_ExtentX", "offset": 0, "size": 4, "source": "extent" },
      { "name": "_ExtentY", "offset": 4, "size": 4, "source": "extent" } ] },
    "arguments": [
      { "name": "radiance_current", "kind": "state:frame-targets", "member": "Radiance", "form": "texture",
        "access": "in", "binding": 0, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "read": true, "write": false, "slot": "single",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeStorageRead" },
      { "name": "radiance_current_sampled", "kind": "state:frame-targets", "member": "Radiance", "form": "sampled",
        "access": "in", "binding": 1, "samplerBinding": 2, "descriptor": "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE",
        "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "read": true, "write": false, "slot": "single",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeSampledReadGeneral" },
      { "name": "resolved_current", "kind": "state:frame-targets", "member": "Resolved", "form": "texture",
        "access": "ref", "binding": 3, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "read": false, "write": true, "slot": "current",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeStorageWrite" },
      { "name": "resolved_previous", "kind": "state:frame-targets", "member": "Resolved", "form": "texture",
        "access": "ref", "binding": 4, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "read": true, "write": false, "slot": "previous",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeStorageRead" } ] } ],
  "barriers": [], "transfers": [],
  "allocations": [
    { "kind": "state:frame-targets", "member": "Radiance", "dimensions": 2,
      "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "texelStride": 0, "slots": 1,
      "usage": "VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT", "extentSource": "presentation", "follows": "" },
    { "kind": "state:frame-targets", "member": "Resolved", "dimensions": 2,
      "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "texelStride": 0, "slots": 2,
      "usage": "VK_IMAGE_USAGE_STORAGE_BIT", "extentSource": "presentation", "follows": "" },
    { "kind": "state:frame-targets", "member": "Unused", "dimensions": 2,
      "format": "VK_FORMAT_R32G32_SFLOAT", "texelStride": 0, "slots": 1,
      "usage": "VK_IMAGE_USAGE_STORAGE_BIT", "extentSource": "presentation", "follows": "" } ]
})";

const SchemaIdentity kRadiance{"state:frame-targets", "Radiance"};
const SchemaIdentity kResolved{"state:frame-targets", "Resolved"};

void ReplaceFirst(std::string& text, const std::string& from, const std::string& to) {
    text.replace(text.find(from), from.size(), to);
}

} // namespace

TEST(GeneratedSystemStage, ManifestArgumentsCarrySchemaIdentityNotNames) {
    const auto manifest = GpuSystemManifest::Parse(kManifest);
    ASSERT_EQ(manifest.systems.size(), 1u);
    const auto& system = manifest.systems[0];
    EXPECT_EQ(system.className, "Tonemap");
    EXPECT_EQ(system.glsl, "Tonemap.comp");
    EXPECT_EQ(system.localSize, (std::array<uint32_t, 3>{8, 8, 1}));
    EXPECT_EQ(system.extentSource, "presentation");
    ASSERT_EQ(system.push.size(), 2u);
    EXPECT_TRUE(system.push[0].extent);
    ASSERT_EQ(system.arguments.size(), 3u);
    EXPECT_EQ(system.arguments[0].identity, (SchemaIdentity{"Fixture.Meter", "*"}));
    EXPECT_TRUE(system.arguments[0].read);
    EXPECT_FALSE(system.arguments[0].write);
    EXPECT_EQ(system.arguments[1].identity, (SchemaIdentity{"state:frame-targets", "Radiance"}));
    EXPECT_EQ(system.arguments[2].binding, 1u);
    EXPECT_TRUE(system.arguments[2].write);
}

TEST(GeneratedSystemStage, AnArgumentWithoutSchemaIdentityIsRejected) {
    std::string manifest = kManifest;
    manifest.replace(manifest.find("\"Fixture.Meter\""), std::string("\"Fixture.Meter\"").size(), "\"\"");
    EXPECT_THROW(GpuSystemManifest::Parse(manifest), std::runtime_error);
}

TEST(GeneratedSystemStage, AnUnregisteredExtentNamesTheSystemAndSource) {
    SchemaResourceRegistry registry;
    try {
        (void)registry.Extent("presentation", 0, "Tonemap");
        FAIL() << "expected a missing-extent error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("Tonemap"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("presentation"), std::string::npos);
    }
}

TEST(GeneratedSystemStage, ManifestCarriesHistorySlotsSampledViewsAndAllocations) {
    const auto manifest = GpuSystemManifest::Parse(kHistoryManifest);
    const auto& arguments = manifest.systems.at(0).arguments;
    ASSERT_EQ(arguments.size(), 4u);
    EXPECT_EQ(arguments[0].slot, FrameSlot::Current);  // "single"
    EXPECT_EQ(arguments[1].form, "sampled");
    EXPECT_EQ(arguments[1].samplerBinding, 2u);
    EXPECT_EQ(arguments[1].descriptorType, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    EXPECT_EQ(arguments[2].slot, FrameSlot::Current);
    EXPECT_TRUE(arguments[2].write);
    EXPECT_EQ(arguments[3].slot, FrameSlot::Previous);
    EXPECT_TRUE(arguments[3].read);
    EXPECT_FALSE(arguments[3].write);
    ASSERT_EQ(manifest.allocations.size(), 3u);
    EXPECT_EQ(manifest.allocations[1].identity, kResolved);
    EXPECT_EQ(manifest.allocations[1].slots, 2u);
    EXPECT_EQ(manifest.allocations[1].format, VK_FORMAT_R16G16B16A16_SFLOAT);
    EXPECT_EQ(manifest.allocations[2].format, VK_FORMAT_R32G32_SFLOAT);
    EXPECT_EQ(manifest.allocations[1].extentSource, "presentation");
}

TEST(GeneratedSystemStage, AnUnknownSlotOrFormatIsRejected) {
    std::string slot = kHistoryManifest;
    ReplaceFirst(slot, "\"slot\": \"previous\"", "\"slot\": \"older\"");
    EXPECT_THROW(GpuSystemManifest::Parse(slot), std::runtime_error);
    std::string format = kHistoryManifest;
    ReplaceFirst(format, "\"format\": \"VK_FORMAT_R32G32_SFLOAT\"", "\"format\": \"VK_FORMAT_D32_SFLOAT\"");
    EXPECT_THROW(GpuSystemManifest::Parse(format), std::runtime_error);
}

TEST(GeneratedSystemStage, ATextureAndASampledViewOfOneImageAreOneReadAndHistoryIsAnother) {
    const auto manifest = GpuSystemManifest::Parse(kHistoryManifest);
    const auto reads = GpuSystemImageReads(manifest.systems.at(0));
    ASSERT_EQ(reads.size(), 2u);
    EXPECT_EQ(reads[0], std::make_pair(kRadiance, FrameSlot::Current));
    EXPECT_EQ(reads[1], std::make_pair(kResolved, FrameSlot::Previous));
}

TEST(GeneratedSystemStage, OnlyUsedUnregisteredIdentitiesAreAllocated) {
    const auto manifest = GpuSystemManifest::Parse(kHistoryManifest);
    SchemaResourceRegistry registry;
    // Unused is declared but no system binds it; Radiance and Resolved are both unregistered.
    auto make = GpuSystemAllocationsToMake(manifest, registry);
    ASSERT_EQ(make.size(), 2u);
    EXPECT_EQ(make[0].identity, kRadiance);
    EXPECT_EQ(make[1].identity, kResolved);
    // A hand pass that registers an identity keeps it (R680.4).
    registry.Provide(kRadiance, NodeHandle{}, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    make = GpuSystemAllocationsToMake(manifest, registry);
    ASSERT_EQ(make.size(), 1u);
    EXPECT_EQ(make[0].identity, kResolved);
    EXPECT_EQ(make[0].slots, 2u);
    // A previous-frame registration alone does not make the current image the hand pass's.
    SchemaResourceRegistry previousOnly;
    previousOnly.Provide(kResolved, NodeHandle{}, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute,
                         FrameSlot::Previous);
    EXPECT_EQ(GpuSystemAllocationsToMake(manifest, previousOnly).size(), 2u);
}

TEST(GeneratedSystemStage, AnAllocationOfMoreThanTwoSlotsIsRejected) {
    std::string text = kHistoryManifest;
    ReplaceFirst(text, "\"slots\": 2", "\"slots\": 3");
    EXPECT_THROW(GpuSystemAllocationsToMake(GpuSystemManifest::Parse(text), SchemaResourceRegistry{}),
                 std::runtime_error);
}

TEST(GeneratedSystemStage, AHandChainBehindTheGeneratedWriterOrdersItsReaders) {
    const NodeHandle hand{7}, barrier{9};
    SchemaResourceRegistry::ProducerEntry plain{hand, false}, behind{hand, true};
    // A barrier source orders a reader unless its hand producer already follows the generated writer.
    EXPECT_EQ(GpuSystemOrderingSource(plain, barrier), barrier);
    EXPECT_EQ(GpuSystemOrderingSource(behind, barrier), hand);
    EXPECT_EQ(GpuSystemOrderingSource(plain, std::nullopt), hand);
    EXPECT_EQ(GpuSystemOrderingSource(std::nullopt, barrier), barrier);
    EXPECT_FALSE(GpuSystemOrderingSource(std::nullopt, std::nullopt).has_value());
}

TEST(GeneratedSystemStage, APreviousFrameBindingNeedsAPreviousRegistration) {
    NodeTypeRegistry types;
    RegisterAllNodes(types);
    RenderGraph graph(&types);
    ConnectionBatch batch(&graph);
    const NodeHandle target = graph.AddNode<RenderTargetNodeType>("target");
    const NodeHandle gatherer = graph.AddNode<DescriptorResourceGathererNodeType>("gatherer");
    SchemaResourceRegistry registry;
    registry.Provide(kResolved, target, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    registry.ApplyDescriptor(kResolved, "Resolve", batch, gatherer, 3);
    try {
        registry.ApplyDescriptor(kResolved, "Resolve", batch, gatherer, 4, FrameSlot::Previous);
        FAIL() << "expected a missing previous-frame resource";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("previous-frame"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("Resolved"), std::string::npos);
    }
}

TEST(GeneratedSystemStage, AllocateMakesAHistoryPairAndARenderTargetFromTheManifest) {
    NodeTypeRegistry types;
    RegisterAllNodes(types);
    RenderGraph graph(&types);
    ConnectionBatch batch(&graph);
    SdiStageCommon common;
    common.device = graph.AddNode<DeviceNodeType>("device");
    common.commandPool = graph.AddNode<CommandPoolNodeType>("pool");
    const NodeHandle presentation = graph.AddNode<RenderTargetNodeType>("presentation");
    SchemaResourceRegistry registry;
    registry.ProvideExtent("presentation", 0, presentation, RenderTargetNodeConfig::WIDTH_OUT, 640u);
    registry.ProvideExtent("presentation", 1, presentation, RenderTargetNodeConfig::HEIGHT_OUT, 360u);
    registry.ProvideExtentTarget("presentation", presentation, RenderTargetNodeConfig::RENDER_TARGET);

    GeneratedSystemStages systems;
    systems.Instantiate(&graph, GpuSystemManifest::Parse(kHistoryManifest));
    ASSERT_EQ(systems.Stages().size(), 1u);
    EXPECT_EQ(systems.Stages()[0].imageReadCount, 2u);
    systems.Allocate(&graph, batch, common, registry);

    const auto pair = systems.Allocation(kResolved);
    ASSERT_TRUE(pair.has_value());
    auto* history = graph.GetInstance(*pair);
    EXPECT_EQ(history->GetNodeType()->GetTypeName(), "AccumulationHistory");
    EXPECT_EQ(history->GetParameterValue<uint32_t>(AccumulationHistoryNodeConfig::PARAM_FORMAT, 0u),
              static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT));
    const auto single = systems.Allocation(kRadiance);
    ASSERT_TRUE(single.has_value());
    auto* radiance = graph.GetInstance(*single);
    EXPECT_EQ(radiance->GetNodeType()->GetTypeName(), "RenderTarget");
    EXPECT_EQ(radiance->GetParameterValue<uint32_t>(RenderTargetNodeConfig::PARAM_IMAGE_COUNT, 0u), 1u);
    EXPECT_FALSE(systems.Allocation({"state:frame-targets", "Unused"}).has_value());

    // The pair registers both frames; the single target only the current one.
    EXPECT_TRUE(registry.Has(kResolved));
    EXPECT_NE(registry.Image(kResolved, FrameSlot::Previous), nullptr);
    EXPECT_NE(registry.ImageWrite(kResolved), nullptr);
    EXPECT_EQ(registry.Image(kRadiance, FrameSlot::Previous), nullptr);
    const size_t before = batch.GetConnectionCount();
    const NodeHandle gatherer = graph.AddNode<DescriptorResourceGathererNodeType>("gatherer");
    registry.ApplyDescriptor(kResolved, "Resolve", batch, gatherer, 4, FrameSlot::Previous);
    EXPECT_EQ(batch.GetConnectionCount(), before + 1);
}
