/**
 * @file test_bandwidth_ab_measurement.cpp
 * @brief Sparse-Mip ESVO LOD Inc1 M5 gate (Task 12): the A/B bandwidth measurement that is
 * the entire increment's value proposition — "a distant object renders for almost free,
 * brick pool never resident."
 *
 * Drives N real BodyOctreeSceneNode instances (same fixture shape as M2's
 * test_partial_brick_upload.cpp: real device, real DirectAllocator/DeviceBudgetManager/
 * BatchedUploader wired exactly as DeviceNode::CreateDeviceBudgetManager does).
 *
 * IMPORTANT (discovered while building this test, not assumed): CreateOctreeBuffers's
 * INITIAL brick population (when RequestBrickResidency(true) is set BEFORE the first
 * Compile) writes bricksBuffer_ via a direct host-visible memcpy
 * (BodyOctreeSceneNode.cpp's file-local CreateHostBuffer helper), NOT via
 * BatchedUploader::Upload — so BatchedUploaderStats cannot observe that path at all
 * (confirmed: an initial-resident condition measured zero bytes/zero uploads through the
 * uploader, because brickPoolUploaded_ is already true by the time ExecuteImpl's
 * UploadBrickPool would otherwise fire, so it correctly no-ops). BatchedUploader is
 * exercised ONLY by the on-demand streaming path — RequestBrickResidency(true) called
 * AFTER a tree already exists mip-only — which is exactly M4c's real live trigger
 * behavior (a body starts far/mip-only, then crosses the resolvability/frustum gate and
 * gets a residency request at that point, never before).
 *
 * So both conditions below start every tree mip-only (RequestBrickResidency(false) before
 * Compile — the universal "has been far away" precondition), then:
 *
 *   (a) BASELINE (pre-Inc1-equivalent — every body's bricks get requested/uploaded, as if
 *       every tree were always resident): immediately call RequestBrickResidency(true) on
 *       every tree and drive Execute() until BatchedUploader has serviced every request —
 *       this is the real, uploader-observable cost of "upload this body's bricks."
 *   (b) INC1 (mip-only-until-close, all N stay far): RequestBrickResidency(true) is never
 *       called — matching M4c's trigger for a body that never crosses the resolvability/
 *       frustum/occlusion gate this run.
 *
 * Measures, via BatchedUploaderStats (same accessor M2's test uses):
 *   - totalBytesUploaded: real bytes moved through the uploader across all N trees.
 *   - wall-clock time for the residency-service phase across all N trees.
 *
 * This does not re-prove the residency TRIGGER logic (test_residency_trigger.cpp, M4c,
 * already covers that pure-CPU decision in isolation) — it proves the mechanism the trigger
 * gates has a real, measured bandwidth payoff when the trigger says "don't upload."
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // Resource
#include "Core/NodeContext.h"
#include "VulkanDevice.h"
#include "Memory/BatchedUploader.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/DirectAllocator.h"
#include "ResidencyTrigger.h"  // Vixen::SVO::InstanceWantsBrickResidency (Inc2 M1)

#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

// N far-away bodies — large enough to make the A/B delta unambiguous, small enough to stay
// well within the existing app's instance-count precedent (BodyOctreeSceneNode.cpp's 3*64
// cap noted in the plan's M4b section).
constexpr uint32_t kNumTrees = 16;

// Real discrete/integrated GPUs are now PREFERRED; software (lavapipe/llvmpipe) and
// Dozen are only a fallback when no real GPU is visible.
bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
    return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
           props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}

bool IsAcceptableDevice(const VkPhysicalDeviceProperties& props) {
    std::string name(props.deviceName);
    for (char& c : name) c = static_cast<char>(::tolower(c));
    const bool isSoftware =
        (name.find("llvmpipe") != std::string::npos ||
         name.find("lavapipe") != std::string::npos) &&
        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    const bool isDozen = name.find("direct3d12") != std::string::npos;
    return isSoftware || isDozen;
}

// ---------------------------------------------------------------------------
// Fixture: identical device/allocator/uploader wiring to test_partial_brick_upload.cpp's
// PartialBrickUploadTest, so this test exercises the exact same real upload path M2/M4c
// already gate — the only new thing here is running it across N trees and timing it.
// ---------------------------------------------------------------------------
class BandwidthAbMeasurementTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             deviceConfirmed_ = false;

    std::unique_ptr<VulkanDevice> deviceShell_;
    std::shared_ptr<ResourceManagement::DeviceBudgetManager> budgetManager_;
    ResourceManagement::BatchedUploader* uploaderObserver_ = nullptr;

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_bandwidth_ab_measurement";
        appInfo.apiVersion       = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;

        ASSERT_EQ(vkCreateInstance(&instInfo, nullptr, &instance_), VK_SUCCESS)
            << "vkCreateInstance failed — is a Vulkan device available?";

        ASSERT_NO_FATAL_FAILURE(PickPhysicalDevice());
        ASSERT_TRUE(deviceConfirmed_)
            << "Refusing to run: no usable Vulkan device found (real GPU, software "
               "rasterizer, or Dozen); nearest was '" << selectedDeviceName_ << "'.";

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());

        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device             = logicalDevice_;
        deviceShell_->queue              = queue_;
        deviceShell_->graphicsQueueIndex = queueFamily_;
        vkGetPhysicalDeviceProperties(physicalDevice_, &deviceShell_->gpuProperties);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &deviceShell_->gpuMemoryProperties);

        auto allocator = std::make_shared<ResourceManagement::DirectAllocator>(
            physicalDevice_, logicalDevice_);

        ResourceManagement::DeviceBudgetManager::Config budgetConfig{};
        budgetConfig.stagingQuota  = 256 * 1024 * 1024;  // headroom for N trees' bricks
        budgetConfig.strictBudget = false;

        budgetManager_ = std::make_shared<ResourceManagement::DeviceBudgetManager>(
            allocator, physicalDevice_, budgetConfig);
        deviceShell_->SetBudgetManager(budgetManager_);

        ResourceManagement::BatchedUploader::Config uploaderConfig;
        uploaderConfig.maxPendingUploads = 256;
        auto uploader = std::make_unique<ResourceManagement::BatchedUploader>(
            logicalDevice_, queue_, queueFamily_, budgetManager_.get(), uploaderConfig);
        uploaderObserver_ = uploader.get();
        deviceShell_->SetUploader(std::move(uploader));

        ASSERT_TRUE(deviceShell_->HasUploadSupport())
            << "Test harness failed to wire a real BatchedUploader.";
    }

    void TearDown() override {
        if (deviceShell_) {
            deviceShell_->WaitAllUploads();
            deviceShell_->device = VK_NULL_HANDLE;
            deviceShell_.reset();
        }
        budgetManager_.reset();
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyDevice(logicalDevice_, nullptr);
            logicalDevice_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    void PickPhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, nullptr), VK_SUCCESS);
        ASSERT_GT(count, 0u) << "No Vulkan physical devices visible.";
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), VK_SUCCESS);

        // Prefer a real discrete/integrated GPU; fall back to software/Dozen only
        // when no real GPU is visible.
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_     = dev;
                selectedDeviceName_ = props.deviceName;
                deviceConfirmed_    = true;
                return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsAcceptableDevice(props)) {
                physicalDevice_     = dev;
                selectedDeviceName_ = props.deviceName;
                deviceConfirmed_    = true;
                return;
            }
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[0], &props);
        selectedDeviceName_ = props.deviceName;
        deviceConfirmed_    = false;
    }

    void CreateLogicalDevice() {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        ASSERT_GT(qfCount, 0u);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfs.data());

        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queueFamily_ = i;
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found) << "No graphics queue family on the selected device";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;

        // BatchedUploader defaults Config::useTimelineSemaphores=true and creates
        // VK_SEMAPHORE_TYPE_TIMELINE semaphores unconditionally, so the device must be
        // created WITH timelineSemaphore enabled or every uploader flush trips
        // VUID-VkSemaphoreTypeCreateInfo-timelineSemaphore-03252 (on a real GPU this
        // degrades a post-residency upload into a multi-minute stall rather than a
        // normal run — root-caused 2026-07-12, mirrors test_partial_brick_upload.cpp).
        VkPhysicalDeviceVulkan12Features vulkan12Features{};
        vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 supported2{};
        supported2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported2.pNext = &vulkan12Features;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &supported2);
        ASSERT_TRUE(vulkan12Features.timelineSemaphore)
            << "Device does not support timelineSemaphore -- BatchedUploader requires it "
               "(or Config::useTimelineSemaphores=false, not used by this fixture).";
        vulkan12Features.pNext = nullptr;  // reset chain link before reuse as the enable struct

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vulkan12Features;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.pNext                = &features2;  // feature chain carries the enabled flags
        dInfo.queueCreateInfoCount = 1;
        dInfo.pQueueCreateInfos    = &qInfo;

        ASSERT_EQ(vkCreateDevice(physicalDevice_, &dInfo, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
    }

    // Builds and returns kNumTrees fresh BodyOctreeSceneNode instances, each wired to this
    // fixture's real device/command pool, ALWAYS starting mip-only
    // (RequestBrickResidency(false) before Compile — the universal "has been far away since
    // the app started" precondition). Compile's CreateOctreeBuffers therefore never populates
    // bricksBuffer_ via the direct-memcpy path for ANY tree here — see the file header
    // comment: that path bypasses BatchedUploader entirely and would make the baseline
    // condition invisible to BatchedUploaderStats. Both A/B conditions below diverge only in
    // whether RequestBrickResidency(true) is called AFTER this precondition, which is the
    // only path that flows through BatchedUploader (mirrors M4c's real trigger, which only
    // ever calls RequestBrickResidency(true) once a body crosses the resolvability gate).
    //
    // public: SetHandleVal/TreeSet/BuildTrees/TeardownTrees are used by the free function
    // RunCondition() below, outside this fixture class.
public:
    template<typename T>
    static void SetHandleVal(Resource& res, T value) {
        res.SetHandle<T>(std::move(value));
    }

    struct TreeSet {
        std::vector<std::unique_ptr<NodeInstance>> nodeBases;
        std::vector<BodyOctreeSceneNode*>          nodes;
        std::vector<Resource>                      devRes, poolRes, frRes;
        std::vector<uint32_t>                      frameIndices;
    };

    TreeSet BuildTrees() {
        TreeSet set;
        set.nodeBases.resize(kNumTrees);
        set.nodes.resize(kNumTrees);
        set.devRes.resize(kNumTrees);
        set.poolRes.resize(kNumTrees);
        set.frRes.resize(kNumTrees);
        set.frameIndices.assign(kNumTrees, 0u);

        for (uint32_t i = 0; i < kNumTrees; ++i) {
            BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
            set.nodeBases[i] = nodeType.CreateInstance(
                "bandwidth_ab_tree_" + std::to_string(i));
            set.nodes[i] = dynamic_cast<BodyOctreeSceneNode*>(set.nodeBases[i].get());
            EXPECT_NE(set.nodes[i], nullptr);

            using C = BodyOctreeSceneNodeConfig;
            SetHandleVal<VulkanDevice*>(set.devRes[i], deviceShell_.get());
            SetHandleVal<VkCommandPool>(set.poolRes[i], commandPool_);
            SetHandleVal<uint32_t>(set.frRes[i], set.frameIndices[i]);
            set.nodes[i]->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &set.devRes[i]);
            set.nodes[i]->SetInput(C::COMMAND_POOL_Slot::index,        0, &set.poolRes[i]);
            set.nodes[i]->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &set.frRes[i]);

            set.nodes[i]->RequestBrickResidency(false);  // mip-only precondition, always
            set.nodes[i]->Setup();
            set.nodes[i]->Compile();
            set.nodes[i]->Execute();  // settles the mip-only state; no bricks uploaded here

            set.frameIndices[i] = 1;
            SetHandleVal<uint32_t>(set.frRes[i], set.frameIndices[i]);
        }
        return set;
    }

    void TeardownTrees(TreeSet& set) {
        for (auto& node : set.nodes) {
            if (node) node->Cleanup(CleanupReason::FinalTeardown);
        }
        set.nodeBases.clear();
    }
};

struct AbResult {
    uint64_t totalBytesUploaded = 0;
    uint64_t totalUploads       = 0;
    double   wallMs             = 0.0;
};

// Services a TreeSet that already exists mip-only (per BuildTrees's precondition):
//   requestResidency=true  -> BASELINE: request+service every tree's bricks (the
//                              pre-Inc1-equivalent "always upload" cost).
//   requestResidency=false -> INC1: leave every tree mip-only (no request at all — the
//                              real trigger's behavior for a body that stays far).
// Times + measures via BatchedUploaderStats only the residency-service phase (matches
// M4c's real per-frame RequestBrickResidency + Execute + PollBrickUploadCompletion loop —
// each node needs one Execute to queue the upload (brickResidencyDirty_ serviced by
// UploadBrickPool) and a further Execute after WaitAllUploads for
// PollBrickUploadCompletion to observe completion and settle brickResident, mirroring
// test_partial_brick_upload.cpp's own multi-tick drive).
AbResult RunCondition(BandwidthAbMeasurementTest::TreeSet& set,
                      bool requestResidency,
                      ResourceManagement::BatchedUploader* uploader,
                      Vixen::Vulkan::Resources::VulkanDevice* deviceShell) {
    const auto statsBefore = uploader->GetStats();
    const auto t0 = std::chrono::steady_clock::now();

    if (requestResidency) {
        for (auto* node : set.nodes) {
            node->RequestBrickResidency(true);
        }
    }

    auto tick = [&]() {
        for (size_t i = 0; i < set.nodes.size(); ++i) {
            set.frameIndices[i] += 1;
            BandwidthAbMeasurementTest::SetHandleVal<uint32_t>(set.frRes[i], set.frameIndices[i]);
            set.nodes[i]->Execute();
        }
    };

    tick();  // queues UploadBrickPool's device->Upload() for every tree (if requested)
    deviceShell->WaitAllUploads();  // drive the async brick upload(s) to GPU-visible completion
    tick();  // PollBrickUploadCompletion observes completion, queues the config re-upload
    deviceShell->WaitAllUploads();  // drive the config re-upload to completion too

    const auto t1 = std::chrono::steady_clock::now();
    const auto statsAfter = uploader->GetStats();

    AbResult r;
    r.totalBytesUploaded = statsAfter.totalBytesUploaded - statsBefore.totalBytesUploaded;
    r.totalUploads       = statsAfter.totalUploads - statsBefore.totalUploads;
    r.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// THE MEASUREMENT
// ---------------------------------------------------------------------------
TEST_F(BandwidthAbMeasurementTest, MipOnlyFarBodiesUploadDrasticallyFewerBytesThanBaseline) {
    // --- Condition (a): BASELINE — every one of the N (already mip-only) trees gets its
    // bricks requested+serviced, i.e. the pre-Inc1-equivalent "every body's bricks get
    // uploaded" cost.
    TreeSet baselineSet = BuildTrees();
    AbResult baseline = RunCondition(baselineSet, /*requestResidency=*/true,
                                      uploaderObserver_, deviceShell_.get());
    TeardownTrees(baselineSet);

    // --- Condition (b): INC1 — every one of the N trees stays mip-only (no residency
    // request at all), matching what M4c's real trigger does for a body that stays far/
    // out-of-frustum/under-resolvable this whole run.
    TreeSet incSet = BuildTrees();
    AbResult inc1 = RunCondition(incSet, /*requestResidency=*/false,
                                  uploaderObserver_, deviceShell_.get());
    TeardownTrees(incSet);

    std::printf(
        "\n"
        "=== Sparse-Mip ESVO LOD Inc1 M5 — A/B bandwidth measurement (N=%u trees) ===\n"
        "  BASELINE (all-resident, pre-Inc1-equivalent):\n"
        "    totalBytesUploaded = %llu bytes\n"
        "    totalUploads       = %llu\n"
        "    wall time          = %.3f ms\n"
        "  INC1 (mip-only-until-close, all-far):\n"
        "    totalBytesUploaded = %llu bytes\n"
        "    totalUploads       = %llu\n"
        "    wall time          = %.3f ms\n"
        "  DELTA:\n"
        "    bytes saved        = %llu bytes (INC1 uploads %s of BASELINE's bytes)\n"
        "    wall time saved    = %.3f ms (%.1fx faster)\n"
        "==============================================================================\n",
        kNumTrees,
        static_cast<unsigned long long>(baseline.totalBytesUploaded),
        static_cast<unsigned long long>(baseline.totalUploads),
        baseline.wallMs,
        static_cast<unsigned long long>(inc1.totalBytesUploaded),
        static_cast<unsigned long long>(inc1.totalUploads),
        inc1.wallMs,
        static_cast<unsigned long long>(baseline.totalBytesUploaded - inc1.totalBytesUploaded),
        inc1.totalBytesUploaded == 0 ? "0% (exactly zero)" : "a nonzero fraction",
        baseline.wallMs - inc1.wallMs,
        inc1.wallMs > 0.0 ? baseline.wallMs / inc1.wallMs : 0.0);

    // --- The actual gate: mip-only-until-close must upload ZERO brick bytes (bricks never
    // requested for any of the N trees), while baseline must upload a real, non-trivial
    // amount (proving the two conditions are genuinely different, not both accidentally zero).
    EXPECT_EQ(inc1.totalBytesUploaded, 0u)
        << "Inc1 condition (bricks never requested for any of the N far-away trees) must "
           "upload exactly zero bytes through BatchedUploader — this IS the increment's "
           "entire value proposition ('brick pool never resident').";
    EXPECT_GT(baseline.totalBytesUploaded, 0u)
        << "Baseline condition (every tree resident) must upload a real, non-zero amount — "
           "otherwise this A/B isn't measuring anything.";
    EXPECT_GT(baseline.totalUploads, inc1.totalUploads)
        << "Baseline must issue strictly more BatchedUploader calls than the mip-only condition.";
}

// ---------------------------------------------------------------------------
// Sparse-Mip ESVO LOD Inc2 M1 — realistic MIXED-scene measurement.
//
// Inc1's M5 Opus validator ("Caveat A", Inc1 Plan §M5 Progress Log) flagged that the
// 170-220x number above is the mechanism's all-or-nothing EXTREME ENDPOINT (all 16 trees
// request residency vs. none do), not the realistic mixed near/far operating point the
// direction doc's original framing described. Recommendation: "measure one mixed scene
// through the actual live trigger before quoting 170-220x as the realistic-scene number."
//
// This test builds one scene of kNumTrees (same N as above, for direct comparability),
// places each tree at a believable world position (a handful genuinely close/in-frustum/
// resolvable, most genuinely far/out-of-frustum/below-resolvability), and — unlike the A/B
// test above, which manually toggles RequestBrickResidency — drives the actual per-instance
// decision through Vixen::SVO::InstanceWantsBrickResidency (ResidencyTrigger.h, Inc1 M4c),
// the exact same pure function VulkanGraphApplication::UpdateBodySceneResidency calls once
// per camera-changed frame in the live app. Only trees whose lone instance passes the
// combined frustum+resolvability gate get RequestBrickResidency(true) called on them —
// mirroring the real per-frame live-trigger flow, not an idealized "some trees are magically
// resident" assumption.
//
// Same INITIAL-vs-on-demand caveat as above applies: every tree starts mip-only via
// BuildTrees() (RequestBrickResidency(false) before Compile), so 100% of the bricks-uploaded
// bytes measured below flow through BatchedUploader on the on-demand path — nothing here
// depends on the direct-memcpy initial-population path BatchedUploaderStats can't see.
namespace {

// Mirrors test_residency_trigger.cpp's own Camera convention (looking down +Z from the
// origin, standard basis) so this scene's numbers are derived the same way, not guessed.
struct SceneCamera {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    glm::vec3 dir{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    float fovDeg = 60.0f;
    float aspect = 16.0f / 9.0f;
};

constexpr float kScreenHeightPx = 1080.0f;
constexpr float kLeafSizeM      = 0.01f;   // ResolvableLevel.h's 1cm-voxel convention
constexpr float kPxThreshold    = 1.0f;
constexpr float kNearPlane      = 0.1f;
constexpr float kFarPlane       = 500.0f;  // matches BuildRenderGraph.cpp's real camera far plane
constexpr float kBodyRadius     = 24.0f;   // matches BuildRenderGraph.cpp's shared body bounding radius

// A believable mixed scene, not chosen to prove a point: kNumTrees (16) total, split into
// a small cluster of NEAR bodies genuinely in front of the camera close enough to resolve
// the brick tier (2^6 = 64 cells/axis, kShellDepth) under a normal 60deg FOV, and the
// remaining majority placed FAR down the same forward axis, well past the distance where
// minResolvableLevel would ever cross the brick-tier threshold at this FOV -- i.e. a scene
// that looks like "a few bodies the player is next to, many more scattered across the rest
// of a large explorable region," not an artificial 50/50 split.
constexpr uint32_t kNumNear = 5;   // near/in-frustum/resolvable  -> expect residency requested
constexpr uint32_t kNumFar  = kNumTrees - kNumNear;  // 11: far/below-resolvability -> stay mip-only

// Near bodies: 8-40m straight ahead (resolvable per ScenarioA_DistanceDriven's own 10m
// precedent in test_residency_trigger.cpp, spread out so they are not literally coincident).
// Far bodies: 1500-2500m straight ahead (same 2000m precedent ScenarioA/B use as their
// "not yet resolvable at 60deg" reference point), spread so they are not all identical.
glm::vec3 NearBodyPos(uint32_t i) {
    return glm::vec3(0.0f, 0.0f, 8.0f + static_cast<float>(i) * 8.0f);  // 8, 16, 24, 32, 40m
}
glm::vec3 FarBodyPos(uint32_t i) {
    return glm::vec3(0.0f, 0.0f, 1500.0f + static_cast<float>(i) * 100.0f);  // 1500..2500m
}

}  // namespace

TEST_F(BandwidthAbMeasurementTest,
       MixedSceneThroughLiveResidencyTriggerMeasuresRealisticBandwidthReduction) {
    static_assert(kNumNear < kNumTrees, "Need at least one far body for this to be a mix");
    static_assert(kNumFar > 0, "Need at least one far body for this to be a mix");

    const SceneCamera cam;  // fixed 60deg FOV, looking down +Z from the origin

    // --- Build one scene of kNumTrees trees, all starting mip-only (BuildTrees's own
    // precondition), then assign each a world position and evaluate the SAME per-instance
    // decision function the live app calls (InstanceWantsBrickResidency), not a manual
    // near/far label chosen by this test.
    TreeSet mixedSet = BuildTrees();

    uint32_t requestedCount = 0;
    std::vector<bool> wantsResidency(kNumTrees, false);
    for (uint32_t i = 0; i < kNumTrees; ++i) {
        const glm::vec3 pos = (i < kNumNear) ? NearBodyPos(i) : FarBodyPos(i - kNumNear);

        // Stash a single instance on this tree so GetInstances()-based production code
        // (and this test's own record-keeping) has a real position to point at; the
        // trigger call itself takes the position directly, matching
        // UpdateBodySceneResidency's own per-instance loop.
        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0] = pos.x;
        inst.worldPos[1] = pos.y;
        inst.worldPos[2] = pos.z;
        mixedSet.nodes[i]->SetInstances({inst});

        const bool wants = Vixen::SVO::InstanceWantsBrickResidency(
            pos, kBodyRadius,
            cam.pos, cam.dir, cam.up, cam.right,
            cam.fovDeg, cam.aspect, kScreenHeightPx, kNearPlane, kFarPlane,
            Vixen::RenderGraph::BodyOctreeSceneNode::GetBrickTierLevel(),
            kLeafSizeM, kPxThreshold);
        wantsResidency[i] = wants;
        if (wants) {
            ++requestedCount;
        }
    }

    // Sanity on the scene itself (not the mechanism) -- if this fails, the constants above
    // stopped describing a genuine mix and the percentage below would be meaningless.
    ASSERT_GT(requestedCount, 0u) << "Scene must have at least one tree the live trigger "
                                     "actually grants residency to, or this isn't a mix.";
    ASSERT_LT(requestedCount, kNumTrees) << "Scene must have at least one tree the live "
                                            "trigger denies residency to, or this isn't a mix.";
    for (uint32_t i = 0; i < kNumNear; ++i) {
        EXPECT_TRUE(wantsResidency[i]) << "Near body " << i << " at " << NearBodyPos(i).z
                                        << "m was expected to pass the live trigger.";
    }
    for (uint32_t i = kNumNear; i < kNumTrees; ++i) {
        EXPECT_FALSE(wantsResidency[i]) << "Far body " << (i - kNumNear) << " at "
                                         << FarBodyPos(i - kNumNear).z
                                         << "m was expected to fail the live trigger.";
    }

    // --- Service ONLY the trees the live trigger actually granted (mirrors
    // UpdateBodySceneResidency: RequestBrickResidency(true) called only for
    // trigger==true trees; trigger==false trees are simply never called, staying
    // mip-only exactly like the INC1 condition in the A/B test above).
    const auto statsBefore = uploaderObserver_->GetStats();
    for (uint32_t i = 0; i < kNumTrees; ++i) {
        if (wantsResidency[i]) {
            mixedSet.nodes[i]->RequestBrickResidency(true);
        }
    }
    auto tick = [&]() {
        for (uint32_t i = 0; i < kNumTrees; ++i) {
            mixedSet.frameIndices[i] += 1;
            SetHandleVal<uint32_t>(mixedSet.frRes[i], mixedSet.frameIndices[i]);
            mixedSet.nodes[i]->Execute();
        }
    };
    tick();
    deviceShell_->WaitAllUploads();
    tick();  // PollBrickUploadCompletion settles brickResident + queues config re-upload
    deviceShell_->WaitAllUploads();
    const auto statsAfter = uploaderObserver_->GetStats();

    const uint64_t mixedBytes = statsAfter.totalBytesUploaded - statsBefore.totalBytesUploaded;
    const uint64_t mixedUploads = statsAfter.totalUploads - statsBefore.totalUploads;
    TeardownTrees(mixedSet);

    // --- All-resident baseline for the SAME kNumTrees, for the percentage-reduction
    // comparison (this is the A/B test's own baseline condition, re-run here rather than
    // shared, so this test is self-contained and independently re-runnable).
    TreeSet baselineSet = BuildTrees();
    AbResult baseline = RunCondition(baselineSet, /*requestResidency=*/true,
                                      uploaderObserver_, deviceShell_.get());
    TeardownTrees(baselineSet);

    const double pctOfBaseline = baseline.totalBytesUploaded > 0
        ? (100.0 * static_cast<double>(mixedBytes) / static_cast<double>(baseline.totalBytesUploaded))
        : 0.0;
    const double pctReduction = 100.0 - pctOfBaseline;
    const double speedup = mixedBytes > 0
        ? static_cast<double>(baseline.totalBytesUploaded) / static_cast<double>(mixedBytes)
        : 0.0;

    std::printf(
        "\n"
        "=== Sparse-Mip ESVO LOD Inc2 M1 -- realistic MIXED-scene bandwidth measurement ===\n"
        "  Scene: %u trees total (%u near/in-frustum/resolvable, %u far/below-threshold)\n"
        "  Live ResidencyTrigger granted residency to: %u of %u trees\n"
        "  MIXED scene (through the live trigger):\n"
        "    totalBytesUploaded = %llu bytes\n"
        "    totalUploads       = %llu\n"
        "  ALL-RESIDENT BASELINE (all %u trees, pre-Inc1-equivalent):\n"
        "    totalBytesUploaded = %llu bytes\n"
        "    totalUploads       = %llu\n"
        "  RESULT: mixed scene uploads %.1f%% of the all-resident baseline's bytes "
        "(%.1f%% reduction, %.1fx less data moved)\n"
        "  NOTE: this is the REALISTIC-MIX number for THIS scene's near/far ratio (%u/%u = "
        "%.0f%% resident) -- it is arithmetically bounded by, and expected to differ from,\n"
        "  the all-or-nothing 170-220x extreme-endpoint number measured by the test above; "
        "see the direction doc's status banner for how the two relate.\n"
        "===================================================================================\n",
        kNumTrees, kNumNear, kNumFar,
        requestedCount, kNumTrees,
        static_cast<unsigned long long>(mixedBytes),
        static_cast<unsigned long long>(mixedUploads),
        kNumTrees,
        static_cast<unsigned long long>(baseline.totalBytesUploaded),
        static_cast<unsigned long long>(baseline.totalUploads),
        pctOfBaseline, pctReduction, speedup,
        requestedCount, kNumTrees,
        100.0 * static_cast<double>(requestedCount) / static_cast<double>(kNumTrees));

    // --- The actual gate: the mixed scene must upload a NONZERO but STRICTLY SMALLER
    // amount than the all-resident baseline -- proving the live trigger is genuinely
    // gating a subset (not accidentally degenerating to all-or-nothing), and that the
    // subset it grants actually costs real, measurable bytes (proving the near trees'
    // bricks really did upload, not just get silently skipped).
    EXPECT_GT(mixedBytes, 0u)
        << "The near/in-frustum/resolvable trees must upload a real, non-zero amount -- "
           "otherwise the live trigger granted nothing and this isn't testing a mix.";
    EXPECT_LT(mixedBytes, baseline.totalBytesUploaded)
        << "The mixed scene must upload strictly fewer bytes than the all-resident "
           "baseline -- otherwise the far trees' mip-only gate isn't saving anything.";
    EXPECT_EQ(requestedCount, kNumNear)
        << "Exactly the " << kNumNear << " near trees (and no far trees) should have "
           "passed the live trigger in this scene.";
}
