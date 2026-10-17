#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "CapabilityGraph.h"
#include "ConfiguredOptionalPath.h"
#include "VulkanCapabilityConfig.h"

TEST(VulkanResources_CapabilityGraph, FragmentStoresAndAtomicsUsesDeviceFeatureSet) {
    Vixen::CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    ASSERT_NE(graph.GetCapability("DeviceFeature:fragmentStoresAndAtomics"), nullptr);
    EXPECT_FALSE(graph.IsCapabilityAvailable(
        "DeviceFeature:fragmentStoresAndAtomics"));

    graph.SetAvailableDeviceFeatures({"fragmentStoresAndAtomics"});
    graph.InvalidateAll();
    EXPECT_TRUE(graph.IsCapabilityAvailable(
        "DeviceFeature:fragmentStoresAndAtomics"));
}

TEST(VulkanResources_CapabilityGraph, BindingTimeAndTypedBuildFactsAreStoredOnGraphNodes) {
    using namespace Vixen;
    auto runtimeNode = std::make_shared<CompositeCapability>("RuntimeNode");
    EXPECT_EQ(runtimeNode->GetBindingTime(), CapabilityBindingTime::Runtime);
    runtimeNode->SetBindingTime(CapabilityBindingTime::BuildTime);
    EXPECT_EQ(runtimeNode->GetBindingTime(), CapabilityBindingTime::BuildTime);

    CapabilityGraph graph;
    graph.RegisterBuildFact("BuildFact:Platform",
        PlatformBuildFact{"Linux", "x86_64", "GNU", "15.2"});
    const auto factNode = std::dynamic_pointer_cast<BuildFactCapability>(
        graph.GetCapability("BuildFact:Platform"));
    ASSERT_NE(factNode, nullptr);
    ASSERT_TRUE(std::holds_alternative<PlatformBuildFact>(factNode->GetValue()));
    EXPECT_EQ(factNode->GetBindingTime(), CapabilityBindingTime::BuildTime);
}

TEST(VulkanResources_CapabilityGraph, UpscalerRecordsExposeBuildFactsAndFailClosedDefaults) {
    using namespace Vixen;
    static_assert(std::is_same_v<
        decltype(std::declval<const CapabilityGraph&>().GetUpscalerCapabilityRecords()),
        const UpscalerCapabilityRecords&>);

    CapabilityGraph graph;
    graph.BuildStandardCapabilities();
    const auto& records = graph.GetUpscalerCapabilityRecords();

    EXPECT_EQ(records.build.targetOS, BuildCapabilities::kPlatform);
    EXPECT_EQ(records.build.targetABI, BuildCapabilities::kArchitecture);
    EXPECT_EQ(records.build.compilerId, BuildCapabilities::kCompilerId);
    EXPECT_EQ(records.build.compilerVersion, BuildCapabilities::kCompilerVersion);
    EXPECT_EQ(records.build.spirvCompilerId, "glslang");
    EXPECT_EQ(records.build.spirvCompilerVersion, VIXEN_CONFIGURED_GLSLANG_VERSION);
    EXPECT_EQ(records.build.spirvTargetVersion, VIXEN_CONFIGURED_SPIRV_TARGET_VERSION);
    EXPECT_TRUE(records.build.nativeFallbackRetained);
    std::vector<std::string> expectedCompiledAdapters{
        "Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1"};
#if defined(VIXEN_CONFIGURED_FSR3_UPSCALER)
    expectedCompiledAdapters.push_back("Upscaler.Fsr3.1.4");
#endif
    EXPECT_EQ(records.build.compiledAdapters, expectedCompiledAdapters);

    const auto vulkanPackage = std::find_if(
        records.build.sdkPackageRevisions.begin(), records.build.sdkPackageRevisions.end(),
        [](const auto& package) { return package.packageId == "Vulkan SDK"; });
    ASSERT_NE(vulkanPackage, records.build.sdkPackageRevisions.end());
    EXPECT_EQ(vulkanPackage->revision, VIXEN_CONFIGURED_VULKAN_SDK_VERSION);
    const auto fsr1Package = std::find_if(
        records.build.sdkPackageRevisions.begin(), records.build.sdkPackageRevisions.end(),
        [](const auto& package) { return package.packageId == "AMD FidelityFX FSR 1"; });
    ASSERT_NE(fsr1Package, records.build.sdkPackageRevisions.end());
    EXPECT_EQ(fsr1Package->revision, "a21ffb8f6c13233ba336352bdff293894c706575");
#if defined(VIXEN_CONFIGURED_FSR3_UPSCALER)
    const auto fsr3Package = std::find_if(
        records.build.sdkPackageRevisions.begin(), records.build.sdkPackageRevisions.end(),
        [](const auto& package) { return package.packageId == "AMD FidelityFX FSR 3.1.4"; });
    ASSERT_NE(fsr3Package, records.build.sdkPackageRevisions.end());
    EXPECT_EQ(fsr3Package->revision, "c6efa6bf7f2027b3ec94f28578bb5965eabb9e55");
#endif
    EXPECT_EQ(records.qualification.status, UpscalerQualificationStatus::UnqualifiedNoRunner);
    EXPECT_EQ(records.qualification.parity, UpscalerParityResult::NotRun);
    EXPECT_EQ(records.policy.viewMode, UpscalerViewMode::Canonical);
}

TEST(VulkanResources_CapabilityGraph, UpscalerRuntimeQualificationAndPolicyRecordsRoundTrip) {
    using namespace Vixen;
    CapabilityGraph graph;

    UpscalerRuntimeFacts runtime{};
    runtime.physicalDevice = "AMD Radeon 8060S Graphics";
    runtime.icdOrTranslationLayer = "Dozen";
    runtime.deviceDriver.deviceName = runtime.physicalDevice;
    runtime.deviceDriver.driverName = "Dozen";
    runtime.deviceDriver.deviceUuid[0] = 0xA5;
    runtime.deviceDriver.driverUuid[0] = 0x5A;
    runtime.supportedFeatures = {"timelineSemaphore", "synchronization2"};
    runtime.formats.push_back({VK_FORMAT_R16G16B16A16_SFLOAT,
                               0,
                               VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,
                               0});
    graph.SetUpscalerRuntimeFacts(runtime);
    graph.SetUpscalerEnabledFeatures({"synchronization2", "timelineSemaphore", "synchronization2"});

    UpscalerQualificationRecord qualification{};
    qualification.status = UpscalerQualificationStatus::Qualified;
    qualification.artifactKey = "vixen-release-a1b2c3d4";
    qualification.deviceKey = "device-8060s-uuid";
    qualification.driverKey = "driver-dozen-uuid";
    qualification.platformKey = "wsl2-x86_64";
    qualification.parity = UpscalerParityResult::Passed;
    qualification.parityCorpusKey = "lookdev-32-png-sha256";
    qualification.timings.push_back({"lookdev-1080p", {1920, 1080}, 128, 2.4, 3.7});
    graph.SetUpscalerQualificationRecord(qualification);

    const UpscalerPolicyRecord policy{
        UpscalerViewMode::Relaxed, {1920, 1080}, 2, 1ull << 30,
        UpscalerWorkRole::Interactive};
    graph.SetUpscalerPolicyRecord(policy);

    const auto& records = graph.GetUpscalerCapabilityRecords();
    EXPECT_EQ(records.runtime.physicalDevice, "AMD Radeon 8060S Graphics");
    EXPECT_EQ(records.runtime.icdOrTranslationLayer, "Dozen");
    EXPECT_EQ(records.runtime.deviceDriver.driverName, "Dozen");
    EXPECT_EQ(records.runtime.deviceDriver.deviceUuid[0], 0xA5);
    EXPECT_EQ(records.runtime.deviceDriver.driverUuid[0], 0x5A);
    EXPECT_EQ(records.runtime.supportedFeatures,
              (std::vector<std::string>{"timelineSemaphore", "synchronization2"}));
    EXPECT_EQ(records.runtime.enabledFeatures,
              (std::vector<std::string>{"synchronization2", "timelineSemaphore"}));
    ASSERT_EQ(records.runtime.formats.size(), 1u);
    EXPECT_EQ(records.runtime.formats.front().format, VK_FORMAT_R16G16B16A16_SFLOAT);
    EXPECT_EQ(records.qualification.status, UpscalerQualificationStatus::Qualified);
    EXPECT_EQ(records.qualification.parity, UpscalerParityResult::Passed);
    EXPECT_EQ(records.qualification.artifactKey, "vixen-release-a1b2c3d4");
    EXPECT_EQ(records.qualification.deviceKey, "device-8060s-uuid");
    EXPECT_EQ(records.qualification.driverKey, "driver-dozen-uuid");
    EXPECT_EQ(records.qualification.platformKey, "wsl2-x86_64");
    EXPECT_EQ(records.qualification.parityCorpusKey, "lookdev-32-png-sha256");
    ASSERT_EQ(records.qualification.timings.size(), 1u);
    EXPECT_EQ(records.qualification.timings.front().outputExtent.width, 1920u);
    EXPECT_DOUBLE_EQ(records.qualification.timings.front().p95Milliseconds, 3.7);
    EXPECT_EQ(records.policy.viewMode, UpscalerViewMode::Relaxed);
    EXPECT_EQ(records.policy.outputExtent.height, 1080u);
    EXPECT_EQ(records.policy.historyCount, 2u);
    EXPECT_EQ(records.policy.memoryAllowanceBytes, 1ull << 30);
    EXPECT_EQ(records.policy.role, UpscalerWorkRole::Interactive);
}

TEST(VulkanResources_CapabilityGraph, UpscalerMachineProfilesDeclareRequiredProviderSets) {
    using namespace Vixen;
    static_assert(std::is_same_v<decltype(GetUpscalerMachineProfiles()),
                                 const std::vector<UpscalerMachineProfile>&>);

    const auto& profiles = GetUpscalerMachineProfiles();
    ASSERT_EQ(profiles.size(), 9u);
    struct ExpectedProfile {
        std::string_view id;
        std::vector<std::string> providers;
    };
    const std::array expectedProfiles{
        ExpectedProfile{"amd-8060s-windows",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1",
                         "Upscaler.Fsr3.1.4"}},
        ExpectedProfile{"amd-8060s-linux",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1",
                         "Upscaler.Fsr3.1.4"}},
        ExpectedProfile{"nvidia-rtx-3060-windows",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1",
                         "Upscaler.Fsr3.1.4", "Upscaler.DlssSr"}},
        ExpectedProfile{"nvidia-rtx-3060-linux",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1",
                         "Upscaler.Fsr3.1.4", "Upscaler.DlssSr"}},
        ExpectedProfile{"amd-laptop",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1",
                         "Upscaler.Fsr3.1.4"}},
        ExpectedProfile{"wsl2-dozen",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1"}},
        ExpectedProfile{"build-worker",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1"}},
        ExpectedProfile{"headless-native-runner",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1"}},
        ExpectedProfile{"intel-arc-native",
                        {"Upscaler.Native", "Upscaler.Linear", "Upscaler.Fsr1",
                         "Upscaler.XeSsSr"}},
    };
    const auto hasProvider = [](const UpscalerMachineProfile& profile,
                                std::string_view provider) {
        return std::find(profile.providerCompileSet.begin(), profile.providerCompileSet.end(),
                         provider) != profile.providerCompileSet.end();
    };
    for (const auto& expected : expectedProfiles) {
        const auto* profile = FindUpscalerMachineProfile(expected.id);
        ASSERT_NE(profile, nullptr) << expected.id;
        EXPECT_EQ(profile->providerCompileSet, expected.providers) << expected.id;
        EXPECT_TRUE(hasProvider(*profile, "Upscaler.Native")) << expected.id;
        EXPECT_EQ(profile->policy.viewMode, UpscalerViewMode::Canonical) << expected.id;
    }
    EXPECT_EQ(FindUpscalerMachineProfile("missing-profile"), nullptr);

    const auto* windows8060s = FindUpscalerMachineProfile("amd-8060s-windows");
    const auto* linux8060s = FindUpscalerMachineProfile("amd-8060s-linux");
    ASSERT_NE(windows8060s, nullptr);
    ASSERT_NE(linux8060s, nullptr);
    EXPECT_EQ(windows8060s->targetOS, "Windows");
    EXPECT_EQ(linux8060s->targetOS, "Linux");
    EXPECT_TRUE(hasProvider(*windows8060s, "Upscaler.Fsr1"));
    EXPECT_TRUE(hasProvider(*linux8060s, "Upscaler.Fsr3.1.4"));

    const auto* windows3060 = FindUpscalerMachineProfile("nvidia-rtx-3060-windows");
    const auto* linux3060 = FindUpscalerMachineProfile("nvidia-rtx-3060-linux");
    ASSERT_NE(windows3060, nullptr);
    ASSERT_NE(linux3060, nullptr);
    EXPECT_TRUE(hasProvider(*windows3060, "Upscaler.DlssSr"));
    EXPECT_TRUE(hasProvider(*linux3060, "Upscaler.DlssSr"));

    const auto* dozen = FindUpscalerMachineProfile("wsl2-dozen");
    const auto* buildWorker = FindUpscalerMachineProfile("build-worker");
    const auto* headless = FindUpscalerMachineProfile("headless-native-runner");
    const auto* intelArc = FindUpscalerMachineProfile("intel-arc-native");
    ASSERT_NE(dozen, nullptr);
    ASSERT_NE(buildWorker, nullptr);
    ASSERT_NE(headless, nullptr);
    ASSERT_NE(intelArc, nullptr);
    EXPECT_EQ(dozen->runtimeLabel, "Dozen");
    EXPECT_EQ(buildWorker->policy.role, UpscalerWorkRole::Build);
    EXPECT_EQ(headless->policy.role, UpscalerWorkRole::HeadlessCapture);
    EXPECT_TRUE(hasProvider(*intelArc, "Upscaler.XeSsSr"));
}

TEST(VulkanResources_CapabilityGraph, ResolvesUpscalerMachineProfileFromRuntimeAdapter) {
    using namespace Vixen;
    const auto expectProfile = [](const UpscalerRuntimeFacts& facts,
                                  std::string_view runtimeOS,
                                  std::string_view expectedId) {
        const auto* profile = ResolveUpscalerMachineProfile(facts, runtimeOS);
        ASSERT_NE(profile, nullptr);
        EXPECT_EQ(profile->profileId, expectedId);
    };
    UpscalerRuntimeFacts runtime;
    runtime.physicalDevice = "AMD Radeon(TM) 8060S Graphics";
    runtime.deviceDriver.deviceName = runtime.physicalDevice;
    runtime.deviceDriver.vendorId = 0x1002;
    runtime.icdOrTranslationLayer = "AMD Vulkan driver";
    runtime.deviceDriver.driverName = runtime.icdOrTranslationLayer;
    expectProfile(runtime, "Windows", "amd-8060s-windows");
    expectProfile(runtime, "Linux", "amd-8060s-linux");

    runtime.icdOrTranslationLayer = "Dozen";
    runtime.deviceDriver.driverName = "Dozen";
    expectProfile(runtime, "Linux", "wsl2-dozen");
    expectProfile(runtime, "Windows", "amd-8060s-windows");

    runtime.physicalDevice = "NVIDIA GeForce RTX 3060";
    runtime.deviceDriver.deviceName = runtime.physicalDevice;
    runtime.deviceDriver.vendorId = 0x10DE;
    runtime.icdOrTranslationLayer = "NVIDIA Vulkan driver";
    runtime.deviceDriver.driverName = runtime.icdOrTranslationLayer;
    expectProfile(runtime, "Windows", "nvidia-rtx-3060-windows");
    expectProfile(runtime, "Linux", "nvidia-rtx-3060-linux");

    runtime.physicalDevice = "AMD Radeon 780M";
    runtime.deviceDriver.deviceName = runtime.physicalDevice;
    runtime.deviceDriver.vendorId = 0x1002;
    runtime.icdOrTranslationLayer = "AMD Vulkan driver";
    runtime.deviceDriver.driverName = runtime.icdOrTranslationLayer;
    expectProfile(runtime, "Windows", "amd-laptop");

    runtime.physicalDevice = "Unknown GPU";
    runtime.deviceDriver.deviceName = runtime.physicalDevice;
    runtime.deviceDriver.vendorId = 0x1234;
    EXPECT_EQ(ResolveUpscalerMachineProfile(runtime, "Windows"), nullptr);

    runtime.physicalDevice = "Intel Arc A770";
    runtime.deviceDriver.deviceName = runtime.physicalDevice;
    runtime.deviceDriver.vendorId = 0x8086;
    expectProfile(runtime, "Linux", "intel-arc-native");

    runtime.physicalDevice = "llvmpipe (LLVM 20.1.0)";
    runtime.deviceDriver.deviceName = runtime.physicalDevice;
    runtime.deviceDriver.vendorId = 0;
    expectProfile(runtime, "Linux", "headless-native-runner");
}

TEST(VulkanResources_CapabilityGraph, ConfiguredOptionalPathUsesBuildModeBinding) {
    using namespace Vixen;
    CapabilityGraph graph;
    graph.RegisterCapability(std::make_shared<CompositeCapability>("RayQueryLighting"));

    EXPECT_EQ(BuildCapabilityTraits<BuildCapabilityId::RayQueryLighting>::buildTime,
              BuildCapabilities::kPersonalizedMode);
    const auto path = ResolveConfiguredOptionalPath<BuildCapabilityId::RayQueryLighting>(graph);
    if constexpr (BuildCapabilities::kPersonalizedMode &&
                  BuildCapabilityTraits<BuildCapabilityId::RayQueryLighting>::buildTime) {
        EXPECT_EQ(path == CapabilityPath::CapabilityEnabled,
                  BuildCapabilities::kCapability_RayQueryLighting);
    } else {
        EXPECT_EQ(path, CapabilityPath::CapabilityEnabled)
            << "full mode must preserve runtime CapabilityGraph resolution";
    }
}

TEST(VulkanResources_CapabilityGraph, ConfiguredPathInvokesOnlyTheSelectedBody) {
    using namespace Vixen;
    CapabilityGraph graph;
    graph.RegisterCapability(std::make_shared<CompositeCapability>("RayQueryLighting"));

    bool enabledInvoked = false;
    bool independentInvoked = false;
    WithConfiguredOptionalPath<BuildCapabilityId::RayQueryLighting>(
        graph, true,
        [&] { enabledInvoked = true; },
        [&] { independentInvoked = true; });

    if constexpr (BuildCapabilities::kPersonalizedMode &&
                  BuildCapabilityTraits<BuildCapabilityId::RayQueryLighting>::buildTime) {
        EXPECT_EQ(enabledInvoked, BuildCapabilities::kCapability_RayQueryLighting);
        EXPECT_EQ(independentInvoked, !BuildCapabilities::kCapability_RayQueryLighting);
    } else {
        EXPECT_TRUE(enabledInvoked);
        EXPECT_FALSE(independentInvoked);
    }
}

TEST(VulkanResources_CapabilityGraph, ConfiguredDeviceIdentityRejectsChangedDeviceOrDriver) {
    using namespace Vixen;
    if constexpr (!BuildCapabilities::kPersonalizedMode || !BuildCapabilities::kHasDeviceIdentity) {
        GTEST_SKIP() << "full mode has no device identity bound at configure";
    } else {
        DeviceDriverIdentityBuildFact identity{};
        identity.deviceName = BuildCapabilities::kDeviceName;
        identity.driverName = BuildCapabilities::kDriverName;
        identity.driverInfo = BuildCapabilities::kDriverInfo;
        identity.vendorId = BuildCapabilities::kVendorId;
        identity.deviceId = BuildCapabilities::kDeviceId;
        identity.driverVersion = BuildCapabilities::kDriverVersion;
        identity.driverId = BuildCapabilities::kDriverId;
        identity.deviceUuid = BuildCapabilities::kDeviceUuid;
        identity.driverUuid = BuildCapabilities::kDriverUuid;
        EXPECT_TRUE(CapabilityGraph::MatchesConfiguredDeviceIdentity(identity));

        identity.driverVersion ^= 1u;
        std::string refusal;
        EXPECT_FALSE(CapabilityGraph::MatchesConfiguredDeviceIdentity(identity, &refusal));
        EXPECT_NE(refusal.find("expected"), std::string::npos);
        EXPECT_NE(refusal.find(std::string(BuildCapabilities::kDeviceName)), std::string::npos);
    }
}

TEST(VulkanResources_CapabilityGraph, OptionalPathUsesIndependentTwinWhenDisabledOrUnavailable) {
    Vixen::CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              Vixen::CapabilityPath::CapabilityIndependent);

    graph.SetAvailableDeviceExtensions({
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    });
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              Vixen::CapabilityPath::CapabilityEnabled);
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting", false),
              Vixen::CapabilityPath::CapabilityIndependent);

    // Unknown capabilities fail closed to the same capability-independent
    // twin as a known-but-unavailable optional capability.
    EXPECT_EQ(graph.ResolveOptionalPath("CapabilityThatDoesNotExist"),
              Vixen::CapabilityPath::CapabilityIndependent);
}

TEST(VulkanResources_CapabilityGraph, RayQueryAndSubgroupCompositesUseAvailabilitySets) {
    Vixen::CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    ASSERT_NE(graph.GetCapability("RayQueryLighting"), nullptr);
    ASSERT_NE(graph.GetCapability("SubgroupCoopTraversal"), nullptr);
    EXPECT_FALSE(graph.IsCapabilityAvailable("RayQueryLighting"));
    EXPECT_FALSE(graph.IsCapabilityAvailable("SubgroupCoopTraversal"));

    graph.SetAvailableDeviceExtensions({
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    });
    graph.SetAvailableDeviceFeatures({
        "subgroupComputeBallot",
        "subgroupComputeArithmetic",
        "subgroupComputeShuffle",
    });
    graph.InvalidateAll();

    EXPECT_TRUE(graph.IsCapabilityAvailable("RayQueryLighting"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("SubgroupCoopTraversal"));

    graph.SetAvailableDeviceFeatures({"subgroupComputeBallot", "subgroupComputeShuffle"});
    graph.InvalidateAll();
    EXPECT_FALSE(graph.IsCapabilityAvailable("SubgroupCoopTraversal"));
}

TEST(VulkanResources_CapabilityGraph, VersionRequirementsSelectIndependentPathsByToolchainAndApi) {
    using Vixen::CapabilityGraph;
    using Vixen::CapabilityPath;
    using Vixen::CapabilityVersionSource;
    using Vixen::CapabilityVersionValue;

    CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    ASSERT_NE(graph.GetCapability("Version:VulkanApi"), nullptr);
    ASSERT_NE(graph.GetCapability("Version:VulkanSdk"), nullptr);
    ASSERT_NE(graph.GetCapability("Version:Glslang"), nullptr);
    ASSERT_NE(graph.GetCapability("Version:SpirvTarget"), nullptr);
    ASSERT_NE(graph.GetCapability("VulkanApi:RequiredFloor"), nullptr);
    ASSERT_NE(graph.GetCapability("SpirvTarget:RayQueryFloor"), nullptr);
    EXPECT_TRUE(graph.IsCapabilityAvailable("Version:VulkanSdk"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("Version:Glslang"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("Version:SpirvTarget"));

    const std::vector<std::string> rayQueryExtensions{
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    };
    auto rayQueryExtensionsWithSync2 = rayQueryExtensions;
    rayQueryExtensionsWithSync2.emplace_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    graph.SetAvailableDeviceExtensions(rayQueryExtensionsWithSync2);
    graph.SetAvailableDeviceFeatures({"synchronization2"});

    const auto oldApi = CapabilityVersionValue::Parse("1.2.0");
    const auto belowApiFloor = CapabilityVersionValue::Parse("1.1.0");
    const auto oldSpirv = CapabilityVersionValue::Parse("1.3.0");
    ASSERT_TRUE(oldApi.has_value());
    ASSERT_TRUE(belowApiFloor.has_value());
    ASSERT_TRUE(oldSpirv.has_value());
    graph.SetVersion(CapabilityVersionSource::VulkanApi, *belowApiFloor);
    graph.SetVersion(CapabilityVersionSource::SpirvTarget, *oldSpirv);

    EXPECT_FALSE(graph.IsCapabilityAvailable("VulkanApi:RequiredFloor"));
    EXPECT_FALSE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    EXPECT_FALSE(graph.IsCapabilityAvailable("SpirvTarget:RayQueryFloor"));
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              CapabilityPath::CapabilityIndependent);

    // Vulkan 1.2 uses the KHR promotion path: the extension plus its feature bit satisfies
    // synchronization2, while the old SPIR-V target still selects the independent ray-query twin.
    graph.SetVersion(CapabilityVersionSource::VulkanApi, *oldApi);
    EXPECT_TRUE(graph.IsCapabilityAvailable("VulkanApi:RequiredFloor"));
    graph.SetAvailableDeviceExtensions(rayQueryExtensions);
    EXPECT_FALSE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    graph.SetAvailableDeviceExtensions(rayQueryExtensionsWithSync2);
    EXPECT_TRUE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              CapabilityPath::CapabilityIndependent);

    // Vulkan 1.3 exposes synchronization2 as core; no extension advertisement is needed.
    graph.SetAvailableDeviceExtensions(rayQueryExtensions);
    const auto newApi = CapabilityVersionValue::Parse("1.3.0");
    const auto newSpirv = CapabilityVersionValue::Parse("1.6.0");
    ASSERT_TRUE(newApi.has_value());
    ASSERT_TRUE(newSpirv.has_value());
    graph.SetVersion(CapabilityVersionSource::VulkanApi, *newApi);
    graph.SetVersion(CapabilityVersionSource::SpirvTarget, *newSpirv);

    EXPECT_TRUE(graph.IsCapabilityAvailable("VulkanApi:RequiredFloor"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("SpirvTarget:RayQueryFloor"));
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              CapabilityPath::CapabilityEnabled);
}

TEST(VulkanResources_CapabilityGraph, BackgroundGpuPrefersIntegratedAndLesserDevice) {
    using Vixen::CapabilityGraph;
    using Vixen::PhysicalDeviceClass;
    using Vixen::PhysicalDeviceInfo;

    EXPECT_EQ(CapabilityGraph::ClassifyPhysicalDevice(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU),
              PhysicalDeviceClass::Integrated);
    EXPECT_EQ(CapabilityGraph::ClassifyPhysicalDevice(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU),
              PhysicalDeviceClass::Discrete);

    const std::vector<PhysicalDeviceInfo> devices{
        {1u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
         PhysicalDeviceClass::Discrete, 1ull << 30, "large discrete"},
        {2u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
         PhysicalDeviceClass::Integrated, 4ull << 30, "integrated A"},
        {3u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
         PhysicalDeviceClass::Integrated, 2ull << 30, "integrated B"},
    };
    const auto selection = CapabilityGraph::SelectBackgroundGpu(devices);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->index, 3u);
    EXPECT_EQ(selection->classification, PhysicalDeviceClass::Integrated);
}

TEST(VulkanResources_CapabilityGraph, BackgroundGpuFailsClosedWithoutCandidate) {
    const std::vector<Vixen::PhysicalDeviceInfo> devices{{
        0u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_OTHER,
        Vixen::PhysicalDeviceClass::Unknown, 0u, "unknown"}};
    EXPECT_FALSE(Vixen::CapabilityGraph::SelectBackgroundGpu(devices).has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
