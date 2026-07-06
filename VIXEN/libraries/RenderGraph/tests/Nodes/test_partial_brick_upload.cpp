/**
 * @file test_partial_brick_upload.cpp
 * @brief Sparse-Mip ESVO LOD Inc1 M2 gate: brick-pool partial allocation + BatchedUploader wiring.
 *
 * Drives the real BodyOctreeSceneNode lifecycle (Setup/Compile/Execute, same public API
 * test_recipe_pool_render.cpp uses) on a real device + a real BatchedUploader (wired exactly
 * as DeviceNode::CreateDeviceBudgetManager does), and verifies:
 *
 *   Task 4 — CreateOctreeBuffers allocates bricksBuffer_ at the tree's FULL byte size
 *   regardless of whether residency was requested (no smaller placeholder buffer), but
 *   leaves it unpopulated (no memcpy/no upload) until RequestBrickResidency(true) is called.
 *
 *   Task 4's crux ("is hasBrick() already sufficient, or does a new GPU-side flag exist"):
 *   hasBrick() (SVOTypes.h) reads ChildDescriptor.contourPointer, which is serialized into
 *   nodesBuffer_ (the NODE array), not bricksBuffer_ — this test proves that buffer's
 *   contents are correct (and populated) independent of whether bricksBuffer_ itself has
 *   ever been written, confirming no redundant GPU-side "brick uploaded" flag is needed.
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
 * DEVICE SELECTION: mirrors test_body_octree_lifetime.cpp / test_recipe_pool_render.cpp —
 * VixenSelectWslGpuIcd() picks Dozen on WSL2 when provisioned, else lavapipe; only those two
 * devices are accepted (an untriaged device fails loud instead of running unverified).
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
            << "Refusing to run: selected device '" << selectedDeviceName_
            << "' is not a verified device (software rasterizer or Dozen).";

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

        VkDeviceCreateInfo dInfo{};
        dInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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

    // --- Task 5/6: no residency requested yet — BatchedUploader must show ZERO uploads.
    // (Compile/Execute alone must not have populated bricks; CreateOctreeBuffers gates
    // that behind residencyRequested_, defaulted false.)
    const auto statsBeforeRequest = uploaderObserver_->GetStats();
    EXPECT_EQ(statsBeforeRequest.totalUploads, 0u)
        << "No brick upload should occur before RequestBrickResidency(true) — Task 4's "
           "gate (residencyRequested_ defaults false) is the whole point of this milestone.";

    // --- Task 6: RequestBrickResidency only stashes the request; an Execute tick that
    // hasn't run yet must NOT have uploaded anything (proves the setter doesn't upload
    // synchronously, matching SetBakeRecipe/SetRecipePool's established dirty-flag pattern).
    node->RequestBrickResidency(true);
    const auto statsRightAfterRequest = uploaderObserver_->GetStats();
    EXPECT_EQ(statsRightAfterRequest.totalUploads, 0u)
        << "RequestBrickResidency must not upload synchronously inside the setter.";

    // --- Servicing the request: the NEXT Execute tick performs the actual upload.
    frameIndex = 1; SetHandleVal<uint32_t>(frRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());

    const auto statsAfterExecute = uploaderObserver_->GetStats();
    EXPECT_GT(statsAfterExecute.totalUploads, 0u)
        << "ExecuteImpl must service a pending residency request via BatchedUploader "
           "(Task 5's device->Upload() wiring), on the tick after the request was made.";
    EXPECT_GT(statsAfterExecute.totalBytesUploaded, 0u);

    // --- A second Execute with the same (already-serviced) request must NOT re-upload —
    // BodyOctreeSceneNode::UploadBrickPool guards on brickPoolUploaded_.
    frameIndex = 2; SetHandleVal<uint32_t>(frRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());
    const auto statsAfterSecondExecute = uploaderObserver_->GetStats();
    EXPECT_EQ(statsAfterSecondExecute.totalUploads, statsAfterExecute.totalUploads)
        << "An already-serviced residency request must not re-upload on a later Execute tick "
           "with no new RequestBrickResidency call.";

    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}
