// renderbridge S1: the GPU system manifest the GeneratedSystemStages runner reads.
#include <gtest/gtest.h>

#include "Nodes/GeneratedSystemStage.h"

using namespace Vixen::RenderGraph;

namespace {

const char* kManifest = R"({
  "schema": "yeroket-gpu-system-manifest/v1",
  "stackHash": "0123456789abcdef",
  "systems": [ {
    "id": "core:Tonemap", "class": "Tonemap", "glsl": "Tonemap.comp", "relaxed": false, "axes": 2,
    "localSize": [8, 8, 1],
    "extent": { "source": "presentation", "kind": "state:frame-targets", "member": "Radiance" },
    "push": { "size": 12, "fields": [
      { "name": "_ExtentX", "offset": 0, "size": 4, "source": "extent" },
      { "name": "_ExtentY", "offset": 4, "size": 4, "source": "extent" },
      { "name": "_Frame", "offset": 8, "size": 4, "source": "frame" } ] },
    "arguments": [
      { "name": "meter", "kind": "Fixture.Meter", "member": "*", "form": "storage", "access": "in",
        "binding": 2, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER", "singleton": false,
        "accessKind": "ComputeStorageRead", "layout": "soaComponentRegions", "regions": [
          { "member": "density", "offset": 0, "sourceOffset": 0, "stride": 4, "size": 4, "alignment": 4 },
          { "member": "flags", "offset": 4, "sourceOffset": 4, "stride": 4, "size": 4, "alignment": 4 } ] },
      { "name": "radiance_current", "kind": "state:frame-targets", "member": "Radiance", "form": "texture",
        "access": "in", "binding": 0, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "format": "VK_FORMAT_R16G16B16A16_SFLOAT", "read": true, "write": false, "slot": "single",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeStorageRead" },
      { "name": "output_current", "kind": "state:frame-targets", "member": "Output", "form": "texture",
        "access": "ref", "binding": 1, "descriptor": "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE",
        "format": "VK_FORMAT_R8G8B8A8_UNORM", "read": false, "write": true, "slot": "current",
        "layout": "VK_IMAGE_LAYOUT_GENERAL", "accessKind": "ComputeStorageWrite" } ] } ],
  "barriers": [], "transfers": [], "transferPlan": [ {
    "before": "core:MeterSettings", "after": "core:Tonemap", "direction": "cpuToGpu",
    "resource": "Fixture.Meter.*", "system": "core:Tonemap", "argument": "meter",
    "kind": "Fixture.Meter", "member": "*", "binding": 2, "class": "cold", "regions": [
      { "member": "density", "offset": 0, "sourceOffset": 0, "stride": 4, "size": 4, "alignment": 4 },
      { "member": "flags", "offset": 4, "sourceOffset": 4, "stride": 4, "size": 4, "alignment": 4 } ] } ],
  "allocations": []
})";

} // namespace

TEST(GeneratedSystemStage, ManifestArgumentsCarrySchemaIdentityNotNames) {
    const auto manifest = GpuSystemManifest::Parse(kManifest);
    ASSERT_EQ(manifest.systems.size(), 1u);
    const auto& system = manifest.systems[0];
    EXPECT_EQ(system.className, "Tonemap");
    EXPECT_EQ(system.glsl, "Tonemap.comp");
    EXPECT_EQ(system.localSize, (std::array<uint32_t, 3>{8, 8, 1}));
    EXPECT_EQ(system.extentSource, "presentation");
    ASSERT_EQ(system.push.size(), 3u);
    EXPECT_TRUE(system.push[0].extent);
    EXPECT_TRUE(system.push[2].frame);
    ASSERT_EQ(system.arguments.size(), 3u);
    EXPECT_EQ(system.arguments[0].identity, (SchemaIdentity{"Fixture.Meter", "*"}));
    EXPECT_TRUE(system.arguments[0].read);
    EXPECT_FALSE(system.arguments[0].write);
    ASSERT_EQ(system.arguments[0].regions.size(), 2u);
    EXPECT_EQ(system.arguments[0].regions[1].sourceOffset, 4u);
    EXPECT_EQ(system.arguments[1].identity, (SchemaIdentity{"state:frame-targets", "Radiance"}));
    EXPECT_EQ(system.arguments[2].binding, 1u);
    EXPECT_TRUE(system.arguments[2].write);
    ASSERT_EQ(manifest.transferPlan.size(), 1u);
    EXPECT_EQ(manifest.transferPlan[0].transferClass, GpuTransferClass::Cold);
}

TEST(GeneratedSystemStage, ColdFillRefreshesEachGenerationSlotThenUploadsZeroAtSteadyState) {
    struct Meter {
        float density;
        uint32_t flags;
    } meter{2.5f, 7u};
    uint64_t generation = 12;
    const auto manifest = GpuSystemManifest::Parse(kManifest);
    TransferSourceRegistry sources;
    sources.ProvideSingleton<Meter>({"Fixture.Meter", "*"}, &meter, [&] { return generation; });
    TransferPlan plan(manifest);

    std::vector<std::byte> uploaded;
    auto upload = [&](const GpuTransferPlanRow&, uint32_t, std::span<const std::byte> bytes) {
        uploaded.assign(bytes.begin(), bytes.end());
    };
    for (uint64_t frame = 0; frame < TransferPlan::GenerationSlots; ++frame) {
        const auto ledger = plan.Fill(frame, sources, upload);
        EXPECT_EQ(ledger.steady_state_bytes_uploaded, sizeof(Meter));
        EXPECT_EQ(ledger.buffersUploaded, 1u);
    }
    const auto steady = plan.Fill(4, sources, upload);
    EXPECT_EQ(steady.steady_state_bytes_uploaded, 0u);
    EXPECT_EQ(steady.buffersUploaded, 0u);

    generation = 13;
    meter.flags = 9;
    const auto changed = plan.Fill(5, sources, upload);
    EXPECT_EQ(changed.steady_state_bytes_uploaded, sizeof(Meter));
    ASSERT_EQ(uploaded.size(), 16u);  // two uint offsets followed by the two component planes
    uint32_t densityOffset = 0;
    uint32_t flagsOffset = 0;
    uint32_t uploadedFlags = 0;
    std::memcpy(&densityOffset, uploaded.data(), sizeof(uint32_t));
    std::memcpy(&flagsOffset, uploaded.data() + sizeof(uint32_t), sizeof(uint32_t));
    std::memcpy(&uploadedFlags, uploaded.data() + flagsOffset, sizeof(uint32_t));
    EXPECT_EQ(densityOffset, 8u);
    EXPECT_EQ(flagsOffset, 12u);
    EXPECT_EQ(uploadedFlags, 9u);
}

TEST(GeneratedSystemStage, HotFillRunsOnlyWhenItsCpuWriterRan) {
    auto manifest = GpuSystemManifest::Parse(kManifest);
    manifest.transferPlan[0].transferClass = GpuTransferClass::Hot;
    struct Meter {
        float density;
        uint32_t flags;
    } meter{1.0f, 3u};
    bool writerRan = false;
    TransferSourceRegistry sources;
    sources.ProvideSingleton<Meter>({"Fixture.Meter", "*"}, &meter, [] { return 0u; },
                                    [&] { return writerRan; });
    TransferPlan plan(manifest);
    uint32_t calls = 0;
    auto upload = [&](const GpuTransferPlanRow&, uint32_t, std::span<const std::byte>) { ++calls; };
    EXPECT_EQ(plan.Fill(0, sources, upload).steady_state_bytes_uploaded, 0u);
    writerRan = true;
    EXPECT_EQ(plan.Fill(1, sources, upload).steady_state_bytes_uploaded, sizeof(Meter));
    EXPECT_EQ(calls, 1u);
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
