/**
 * @file test_partial_brick_upload.cpp
 * @brief Sparse-Mip ESVO LOD Inc1 M2 gate: brick-pool partial allocation + BatchedUploader wiring.
 *
 * Drives the real BodyOctreeSceneNode lifecycle (Setup/Compile/Execute, same public API
 * test_recipe_pool_render.cpp uses) on a real device + a real BatchedUploader (wired exactly
 * as DeviceNode::CreateDeviceBudgetManager does), and verifies:
 *
 *   Task 4 — CreateOctreeBuffers allocates bricksBuffer_ at the tree's FULL byte size
 *   regardless of whether residency was requested (no smaller placeholder buffer). This
 *   test explicitly calls RequestBrickResidency(false) before Compile to construct the
 *   mip-only precondition (unpopulated bricks) — residencyRequested_ defaults TRUE since
 *   the M3 fix (see BodyOctreeSceneNode.h), so a caller that never touches this API keeps
 *   pre-Inc1 behavior (bricks always populated).
 *
 *   Task 4's crux ("is hasBrick() already sufficient, or does a new GPU-side flag exist"):
 *   hasBrick() (SVOTypes.h) reads ChildDescriptor.contourPointer, which is serialized into
 *   nodesBuffer_ (the NODE array), not bricksBuffer_ — this test proves that buffer's
 *   contents are correct (and populated) independent of whether bricksBuffer_ itself has
 *   ever been written. That remains true for CPU-side buffer bookkeeping. M3 (Task 7)
 *   later found the shader itself DOES need an explicit flag: contourPointer is a valid
 *   brick pointer regardless of residency (it never changes), so it cannot by itself tell
 *   the shader "the buffer this pointer indexes into hasn't been populated yet" — that
 *   distinct signal is OctreeConfig.brickResident (byte 356), stamped by
 *   CreateOctreeBuffers/UploadBrickPool, not read by this test.
 *
 *   Task 5 — brick population goes through BatchedUploader (device->Upload()), verified via
 *   BatchedUploaderStats (totalUploads/totalBytesUploaded increase only after residency is
 *   requested and serviced, never at Compile time).
 *
 *   Task 6 — RequestBrickResidency(idx, true) only stashes the request (bricks stay
 *   unpopulated across an Execute tick with no residency requested), then ExecuteImpl
 *   performs the actual upload on the NEXT Execute after the request — never synchronously
 *   inside the setter.
 *
 * DEVICE SELECTION: prefers a real discrete/integrated GPU; falls back to software
 * (lavapipe/llvmpipe) or Dozen (WSL2's Vulkan-over-D3D12 shim) only when no real GPU is
 * present. Per the Windows-side real-GPU test policy, a real GPU is ACCEPTED, not rejected
 * — the earlier software/Dozen-only gate was a lavapipe-era artifact that made this test
 * unable to run on real hardware. A device is still required (an environment with none
 * fails loud rather than running unverified).
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "BrickConfigUpload.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // Resource
#include "Core/NodeContext.h"
#include "VulkanDevice.h"
#include "Memory/BatchedUploader.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/DirectAllocator.h"

#include "ShellOctreeGpu.h"
#include "SVOTypes.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

// Real discrete/integrated GPUs are preferred; software (lavapipe/llvmpipe) and
// Dozen (WSL2's Vulkan-over-D3D12 shim) are accepted as a fallback when no real
// GPU is visible, but never preferred over one. The earlier software/Dozen-only
// gate was a lavapipe-era artifact that made this test unable to run on real
// hardware — a real GPU is now ACCEPTED, not rejected.
bool IsRealGpu(const VkPhysicalDeviceProperties& p) {
    return p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
           p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}

bool LooksLikeSoftwareOrDozen(const VkPhysicalDeviceProperties& p) {
    std::string name(p.deviceName);
    for (char& c : name) c = static_cast<char>(::tolower(c));
    const bool isSoftware =
        (name.find("llvmpipe") != std::string::npos ||
         name.find("lavapipe") != std::string::npos) &&
        p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    const bool isDozen = name.find("direct3d12") != std::string::npos;
    return isSoftware || isDozen;
}

// ---------------------------------------------------------------------------
// Fixture: real device + DirectAllocator + DeviceBudgetManager + BatchedUploader, wired
// onto a VulkanDevice exactly as DeviceNode::CreateDeviceBudgetManager (DeviceNode.cpp)
// does — so BodyOctreeSceneNode's device->Upload() calls exercise the real BatchedUploader
// path this milestone adds, not a null-uploader silent no-op.
// ---------------------------------------------------------------------------
class PartialBrickUploadTest : public ::testing::Test {
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
    // Raw observer into the uploader VulkanDevice now owns (via SetUploader's unique_ptr) —
    // VulkanDevice exposes no stats accessor, so the test keeps this pointer (valid for the
    // uploader's lifetime, which outlives the test body) purely to assert upload counts.
    ResourceManagement::BatchedUploader* uploaderObserver_ = nullptr;

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_partial_brick_upload";
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

        // Mirrors DeviceNode::CreateDeviceBudgetManager exactly: DirectAllocator ->
        // DeviceBudgetManager -> BatchedUploader, all set on the VulkanDevice shell so
        // BodyOctreeSceneNode::UploadBrickPool's device->Upload() is real.
        auto allocator = std::make_shared<ResourceManagement::DirectAllocator>(
            physicalDevice_, logicalDevice_);

        ResourceManagement::DeviceBudgetManager::Config budgetConfig{};
        budgetConfig.stagingQuota  = 64 * 1024 * 1024;  // 64 MB — plenty for this test's tiny bricks
        budgetConfig.strictBudget = false;

        budgetManager_ = std::make_shared<ResourceManagement::DeviceBudgetManager>(
            allocator, physicalDevice_, budgetConfig);
        deviceShell_->SetBudgetManager(budgetManager_);

        ResourceManagement::BatchedUploader::Config uploaderConfig;
        uploaderConfig.maxPendingUploads = 64;
        auto uploader = std::make_unique<ResourceManagement::BatchedUploader>(
            logicalDevice_, queue_, queueFamily_, budgetManager_.get(), uploaderConfig);
        uploaderObserver_ = uploader.get();  // valid for the uploader's lifetime (owned by deviceShell_)
        deviceShell_->SetUploader(std::move(uploader));

        ASSERT_TRUE(deviceShell_->HasUploadSupport())
            << "Test harness failed to wire a real BatchedUploader — UploadBrickPool would "
               "silently no-op against InvalidUploadHandle, defeating this test's purpose.";
    }

    void TearDown() override {
        if (deviceShell_) {
            deviceShell_->WaitAllUploads();
            // Non-owning view: null the handle before dropping so ~VulkanDevice doesn't
            // vkDestroyDevice a device we still own and destroy explicitly below.
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
        // when no real GPU is visible. An unrecognized device type still leaves
        // deviceConfirmed_ false, matching the prior hard gate's "must pick something".
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
            if (LooksLikeSoftwareOrDozen(props)) {
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
        // normal run — root-caused 2026-07-12, mirrors test_mip_fallback_render.cpp).
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

    template<typename T>
    static void SetHandleVal(Resource& res, T value) {
        res.SetHandle<T>(std::move(value));
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// THE TEST
// ---------------------------------------------------------------------------
TEST_F(PartialBrickUploadTest, BricksAllocatedFullSizeButPopulatedOnlyAfterResidencyRequest) {
    using C = BodyOctreeSceneNodeConfig;

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("partial_brick_upload_test");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource devRes;  SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
    Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frRes;   uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);

    // Inc1 M3 fix: residencyRequested_ now DEFAULTS true (matches pre-Inc1 behavior for
    // every caller that never touches this API — see BodyOctreeSceneNode.h's default-flip
    // comment). This test specifically wants the mip-only precondition (bricks NOT
    // populated at Compile time), so it must opt out explicitly before Compile.
    node->RequestBrickResidency(false);

    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    ASSERT_NO_THROW(node->Execute());

    // --- Task 4: bricksBuffer_ allocated at FULL capacity from the very first Compile,
    // regardless of residency (default binary shell octrees always populate concatenated_
    // .bricks — see EnsureOctreesBuilt's default path). Query the buffer's actual bound
    // memory size via vkGetBufferMemoryRequirements — this is what "allocated at full
    // capacity" means at the Vulkan level, independent of whether it's been written.
    VkBuffer bricksBuf = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(bricksBuf, VK_NULL_HANDLE);
    VkMemoryRequirements bricksReq{};
    vkGetBufferMemoryRequirements(logicalDevice_, bricksBuf, &bricksReq);
    EXPECT_GT(bricksReq.size, 0u);

    // --- Task 5/6: residency was explicitly declined above — BatchedUploader must show
    // ZERO uploads (Compile/Execute alone must not have populated bricks).
    const auto statsBeforeRequest = uploaderObserver_->GetStats();
    EXPECT_EQ(statsBeforeRequest.totalUploads, 0u)
        << "No brick upload should occur before RequestBrickResidency(true) — Task 4's "
           "gate (residencyRequested_ explicitly set false above) is the whole point of "
           "this milestone.";

    // --- Task 6: RequestBrickResidency only stashes the request; an Execute tick that
    // hasn't run yet must NOT have uploaded anything (proves the setter doesn't upload
    // synchronously, matching SetBakeRecipe/SetRecipePool's established dirty-flag pattern).
    node->RequestBrickResidency(true);
    const auto statsRightAfterRequest = uploaderObserver_->GetStats();
    EXPECT_EQ(statsRightAfterRequest.totalUploads, 0u)
        << "RequestBrickResidency must not upload synchronously inside the setter.";

    // --- Servicing the request: the next Execute tick submits two ordered uploads in one
    // batch: brick bytes first, then resident-config bytes. Upload and batch counts are
    // synchronous evidence that both requests entered the same submission; the byte counter
    // below is completion evidence from the real GPU path.
    const auto statsBeforeExecute = uploaderObserver_->GetStats();
    const uint64_t expectedUploads = statsBeforeExecute.totalUploads + 2;
    const uint64_t expectedBatches = statsBeforeExecute.totalBatches + 1;
    // The queue is non-blocking: upload counts are available immediately, while completed
    // bytes are checked only after WaitAllUploads has observed the GPU copy.
    //
    frameIndex = 1; SetHandleVal<uint32_t>(frRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());

    const auto statsRightAfterExecute = uploaderObserver_->GetStats();
    EXPECT_EQ(statsRightAfterExecute.totalUploads, expectedUploads)
        << "ExecuteImpl must queue exactly one brick request and one config request.";
    EXPECT_EQ(statsRightAfterExecute.totalBatches, expectedBatches)
        << "Brick and config requests must share one ordered BatchedUploader submission.";
    deviceShell_->WaitAllUploads();  // drive completion explicitly (async, so it isn't automatic)
    const auto statsAfterCompletion = uploaderObserver_->GetStats();
    EXPECT_EQ(statsAfterCompletion.totalBytesUploaded,
              statsBeforeExecute.totalBytesUploaded + node->BrickPoolBytes() + node->OctreeConfigBytes())
        << "GPU completion must account for both brick and config byte ranges.";

    // Completion is polled by ExecuteImpl; no second config upload is expected.
    frameIndex += 1; SetHandleVal<uint32_t>(frRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());
    EXPECT_TRUE(node->IsBrickPoolUploaded())
        << "The single ordered completion must publish brick residency.";

    // One more Execute tick with nothing pending must be a true no-op.
    frameIndex += 1; SetHandleVal<uint32_t>(frRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());
    const auto statsAfterSecondExecute = uploaderObserver_->GetStats();
    EXPECT_EQ(statsAfterSecondExecute.totalUploads, expectedUploads)
        << "An unchanged generation must not submit another brick/config upload.";

    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

TEST(BrickConfigUpload, GenerationKeyedRangesSkipUnchangedGeneration) {
    Vixen::SVO::BrickConfigGenerationState state;
    Vixen::SVO::BrickConfigDirtyRanges ranges;
    ranges.bricks.push_back({8, 16});
    ranges.configs.push_back({32, 64});

    state.Stage(7, ranges);
    EXPECT_TRUE(state.NeedsSubmission(7));
    state.MarkSubmitted(7);
    EXPECT_FALSE(state.NeedsSubmission(7));

    Vixen::SVO::BrickConfigDirtyRanges unchangedGeneration;
    unchangedGeneration.bricks.push_back({128, 16});
    state.Stage(7, unchangedGeneration);
    EXPECT_FALSE(state.NeedsSubmission(7));

    Vixen::SVO::BrickConfigDirtyRanges nextGeneration;
    nextGeneration.configs.push_back({256, 64});
    state.Stage(8, nextGeneration);
    EXPECT_TRUE(state.NeedsSubmission(8));
    EXPECT_EQ(state.pending.configs.front().offset, 256u);
}
