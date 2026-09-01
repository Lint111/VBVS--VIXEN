/**
 * @file test_mip_fallback_render.cpp
 * @brief Sparse-Mip ESVO LOD Inc1 M3 gate: shader-side mip fallback read (Tasks 7-9).
 *
 * Bakes a single sphere via ConcatenateSdfWithMips (MipBake.h) so the pool carries a
 * real per-node mip sample alongside the usual node/brick/channel data, calls
 * SetRecipePool + RequestBrickResidency(false), and renders. With bricks never
 * uploaded, every leaf hit-test must fall back to Task 7's mip[nodeIdx] read — this
 * test asserts the result is a recognizable silhouette (fillRatio-style pixel-coverage
 * AND shape check, not just "some pixels lit": the Inc2 M6 precedent this Plan's Task 9
 * explicitly cites found a silhouette-only check insufficient), not a blank/black frame.
 *
 * No-regression: the SAME pool rendered with RequestBrickResidency(true) (bricks fully
 * uploaded) must produce a materially similar hit-pixel count/shape (the true brick
 * march), and an existing binary-shell-octree scene (no mip pool, no residency call —
 * BodyOctreeSceneNode's post-M3 default) must render identically to pre-Inc1.
 *
 * DEVICE SELECTION: prefers a real discrete/integrated GPU; falls back to software
 * (lavapipe/llvmpipe) or Dozen only when no real GPU is present. Per the Windows-side
 * real-GPU test policy, a real GPU is ACCEPTED, not rejected — the earlier
 * software/Dozen-only gate was a lavapipe-era artifact that made this test unable to
 * run at all on real hardware.
 *
 * Run: ./test_mip_fallback_render
 *   Output: /tmp/mip_fallback_render.png (mip-only), /tmp/mip_fallback_resident.png (resident).
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"
#include "MipBake.h"
#include "SdfBake.h"
#include "SdfRecipes.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "Recipe/SdfInstruction.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd
#include "Memory/BatchedUploader.h"
#include "Memory/DeviceBudgetManager.h"
#include "Memory/DirectAllocator.h"
#include "Generated/LightingConfig.g.h"  // Sampled Lighting Inc0 M3: real default light content
                                          // (a zeroed LightingConfig has lightCount=0 -> pure
                                          // black shaded output -> invisible to any luminance
                                          // threshold this test's own render checks use)

#include <vulkan/vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch.spv) must be defined by CMake"
#endif

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

namespace {

struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float raySizeCoef; float raySizeBias; int32_t instanceCount;
    int32_t _pad0;         glm::ivec2 debugTargetPixel;  // (-1,-1) disables; offset 80, shader ~line 237
    uint32_t accumFrameCount;  // Sampled Lighting Inc2 M2 (bytes 88-91) — unused (no
                               // accumulation config bound; accumulationConfig.enabled==0
                               // keeps the temporal-accum seam a pure passthrough).
    uint32_t _pad1;            // std430 push-constant block rounds up to a 16-byte multiple
                               // (leading vec3 forces 16-byte block alignment) -- SPIR-V
                               // reflection reports 96 bytes total, not 92.
};
static_assert(sizeof(PushConstants) == 96, "PushConstants must be 96 bytes (std430 push block, 16-byte rounded)");

// ---------------------------------------------------------------------------
// KI-032 fix: this file's colorImg (binding 0) readback went permanently dark when
// commit 784adff7 (Sampled Lighting Inc3 M1, KI-018) split shading out of
// BodyInstanceRayMarch.comp into DirectLighting.comp/SpatialReuseShade.comp -- this
// shader now writes ONLY HitRecordBuffer (binding 18) and idOutputImage (binding 9),
// never outputImage. The silhouette/coverage checks below now read HitRecord instead --
// same mirror struct test_hitrecord_readback.cpp/test_recipe_pool_render.cpp already
// established (see KI-032's "Status: PARTIALLY RESOLVED" entry in Known-Issues.md).
// DO NOT revert to a colorImg readback (784adff7) -- see KI-032.
// ---------------------------------------------------------------------------
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[4];  // std430 tail padding -- see test_hitrecord_readback.cpp's identical mirror
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");

constexpr uint32_t kHitRecordFlagHit = 0x1u;

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    f.seekg(0); f.read(reinterpret_cast<char*>(code.data()), sz);
    return code;
}

constexpr float kWorldGridSize = 10.0f;

Vixen::SVO::BodyInstanceGpu MakeInst(float x, float y, float z, float scale,
                                      uint32_t octreeIndex) {
    Vixen::SVO::BodyInstanceGpu i{};
    i.worldPos[0] = x; i.worldPos[1] = y; i.worldPos[2] = z;
    i.renderScale = scale; i.octreeIndex = octreeIndex;
    i.color[0] = 1.0f; i.color[1] = 1.0f; i.color[2] = 1.0f;
    return i;
}

PushConstants MakeCamera(const glm::vec3& eye, const glm::vec3& target, uint32_t w, uint32_t h,
                          int32_t instanceCount) {
    const glm::vec3 dir    = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right  = glm::normalize(glm::cross(dir, worldUp));
    const glm::vec3 up     = glm::normalize(glm::cross(right, dir));
    PushConstants pc{};
    pc.cameraPos = eye;  pc.time = 0.0f;
    pc.cameraDir = dir;  pc.fov  = 45.0f;
    pc.cameraUp  = up;   pc.aspect = static_cast<float>(w) / static_cast<float>(h);
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;   // LOD cutoff disabled — isolate Task 7's trigger
    pc.instanceCount = instanceCount;
    pc.debugTargetPixel = glm::ivec2(-1, -1);  // disabled (shader: (-1,-1) skips debug capture)
    return pc;
}

}  // namespace

// ---------------------------------------------------------------------------
// Minimal Vulkan fixture — mirrors test_recipe_pool_render.cpp, +binding 13 (mip pool).
// ---------------------------------------------------------------------------
class MipFallbackRenderTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    bool             deviceConfirmed_ = false;
    std::string      selectedDeviceName_;
    std::unique_ptr<VulkanDevice> deviceShell_;
    // Real device.Upload() (VulkanDevice::Upload -> uploader_) is a no-op returning
    // InvalidUploadHandle unless a BatchedUploader is attached via SetUploader() --
    // in production this wiring happens ONLY in DeviceNode::CreateDeviceBudgetManager
    // (DeviceNode.cpp:436-519), which this hand-built fixture device bypasses entirely.
    // Any test exercising a POST-Compile async residency grant (UploadBrickPool's
    // device->Upload call) needs this mirrored here, or the upload silently fails and
    // the "after grant" render is actually just a re-render of the "before" state.
    std::shared_ptr<ResourceManagement::DeviceBudgetManager> budgetManager_;

    // Real discrete/integrated GPUs are preferred; software (lavapipe/llvmpipe) and
    // Dozen (WSL2's Vulkan-over-D3D12 shim) are accepted as a fallback when no real
    // GPU is visible, but never preferred over one.
    static bool IsRealGpu(const VkPhysicalDeviceProperties& p) {
        return p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    static bool LooksLikeSoftwareOrDozen(const VkPhysicalDeviceProperties& p) {
        std::string n(p.deviceName); for (char& c : n) c = char(::tolower(c));
        const bool isSoftware =
            (n.find("llvmpipe") != std::string::npos ||
             n.find("lavapipe") != std::string::npos) &&
            p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool isDozen = n.find("direct3d12") != std::string::npos;
        return isSoftware || isDozen;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();
        VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "test_mip_fallback_render"; ai.apiVersion = VK_API_VERSION_1_3;
        const auto layers = EnabledValidationLayers();
        const char* exts[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        VkInstanceCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &ai;
        ci.enabledLayerCount = uint32_t(layers.size()); ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = exts;
        ASSERT_EQ(vkCreateInstance(&ci, nullptr, &instance_), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(PickUsableDevice());
        ASSERT_TRUE(deviceConfirmed_);
        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCmdPool());
        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
        deviceShell_->queue = queue_;
        deviceShell_->graphicsQueueIndex = queueFamily_;
        vkGetPhysicalDeviceProperties(physicalDevice_, &deviceShell_->gpuProperties);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &deviceShell_->gpuMemoryProperties);

        // Mirror DeviceNode::CreateDeviceBudgetManager (DeviceNode.cpp:436-519) so
        // deviceShell_->Upload() actually works for tests that grant residency
        // POST-Compile (see the deviceShell_/budgetManager_ member comments above).
        auto allocator = std::make_shared<ResourceManagement::DirectAllocator>(
            physicalDevice_, logicalDevice_);
        uint64_t deviceLocalMemory = 0;
        for (uint32_t i = 0; i < deviceShell_->gpuMemoryProperties.memoryHeapCount; ++i) {
            const auto& heap = deviceShell_->gpuMemoryProperties.memoryHeaps[i];
            if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) deviceLocalMemory += heap.size;
        }
        ResourceManagement::DeviceBudgetManager::Config budgetConfig{};
        budgetConfig.deviceMemoryBudget  = static_cast<uint64_t>(deviceLocalMemory * 0.9);
        budgetConfig.deviceMemoryWarning = static_cast<uint64_t>(deviceLocalMemory * 0.8);
        budgetConfig.stagingQuota = std::min<uint64_t>(256ull * 1024 * 1024, deviceLocalMemory / 16);
        budgetConfig.strictBudget = false;
        budgetManager_ = std::make_shared<ResourceManagement::DeviceBudgetManager>(
            allocator, physicalDevice_, budgetConfig);
        deviceShell_->SetBudgetManager(budgetManager_);

        ResourceManagement::BatchedUploader::Config uploaderConfig;
        uploaderConfig.maxPendingUploads = 64;
        uploaderConfig.flushDeadline = std::chrono::milliseconds{16};
        auto uploader = std::make_unique<ResourceManagement::BatchedUploader>(
            deviceShell_->device, deviceShell_->queue, deviceShell_->graphicsQueueIndex,
            budgetManager_.get(), uploaderConfig,
            &deviceShell_->SubmitMutex(deviceShell_->queue));
        deviceShell_->SetUploader(std::move(uploader));
    }

    void TearDown() override {
        if (deviceShell_) { deviceShell_->device = VK_NULL_HANDLE; deviceShell_.reset(); }
        budgetManager_.reset();  // must go before vkDestroyDevice below (owns device-tied allocations)
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
        if (logicalDevice_ != VK_NULL_HANDLE) vkDestroyDevice(logicalDevice_, nullptr);
        if (instance_     != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    }

    // Prefers a real discrete/integrated GPU; falls back to software/Dozen only if no
    // real GPU is visible. Either way some usable device is required — an unrecognized
    // device type still leaves deviceConfirmed_ false, matching the prior hard gate's
    // "must actually pick something" contract.
    void PickUsableDevice() {
        uint32_t cnt = 0; ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &cnt, nullptr), VK_SUCCESS);
        ASSERT_GT(cnt, 0u) << "No Vulkan devices visible.";
        std::vector<VkPhysicalDevice> devs(cnt);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &cnt, devs.data()), VK_SUCCESS);
        for (auto dev : devs) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(dev, &p);
            if (IsRealGpu(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; deviceConfirmed_ = true; return; }
        }
        for (auto dev : devs) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(dev, &p);
            if (LooksLikeSoftwareOrDozen(p)) { physicalDevice_ = dev; selectedDeviceName_ = p.deviceName; deviceConfirmed_ = true; return; }
        }
        VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(devs[0], &p);
        selectedDeviceName_ = p.deviceName;
    }

    void CreateLogicalDevice() {
        uint32_t qfCnt = 0; vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCnt);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCnt, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCnt; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found);
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = queueFamily_; qi.queueCount = 1; qi.pQueuePriorities = &prio;

        // Query timelineSemaphore support (not hardcoded true, mirrors VulkanDevice::CreateDevice's
        // QueryAvailableDeviceFeatures discipline -- VulkanDevice.cpp:120-149,210-222) before
        // enabling it: BatchedUploader::Config defaults useTimelineSemaphores=true and creates
        // VK_SEMAPHORE_TYPE_TIMELINE semaphores unconditionally, so a device created WITHOUT this
        // feature enabled fails VUID-VkSemaphoreTypeCreateInfo-timelineSemaphore-03252 on every
        // uploader flush -- root-caused 2026-07-12 as the reason a post-grant residency upload
        // degraded to a multi-minute stall instead of a normal ~30s GPU-test run.
        VkPhysicalDeviceVulkan12Features vulkan12Features{};
        vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 supported2{};
        supported2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported2.pNext = &vulkan12Features;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &supported2);
        ASSERT_TRUE(vulkan12Features.timelineSemaphore)
            << "Device does not support timelineSemaphore -- BatchedUploader requires it "
               "(or Config::useTimelineSemaphores=false, not used by this fixture)";
        vulkan12Features.pNext = nullptr;  // reset chain link before reuse as the enable struct

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vulkan12Features;
        features2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

        VkDeviceCreateInfo di{}; di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.pNext = &features2;             // feature chain, not pEnabledFeatures, carries the flags
        di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = nullptr;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &di, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCmdPool() {
        VkCommandPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &pi, nullptr, &commandPool_), VK_SUCCESS);
    }

    template<typename T>
    static void SetHandleVal(Resource& res, T value) { res.SetHandle<T>(std::move(value)); }

    uint32_t FindMemType(uint32_t filter, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties mp{}; vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((filter & (1u<<i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
        return UINT32_MAX;
    }

    void CreateImage(uint32_t w, uint32_t h, VkFormat fmt, VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w,h,1};
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ci, nullptr, &img), VK_SUCCESS);
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(logicalDevice_, img, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size; ai.memoryTypeIndex = FindMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &mem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, img, mem, 0), VK_SUCCESS);
    }

    VkImageView MakeView(VkImage img, VkFormat fmt) {
        VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkImageView v = VK_NULL_HANDLE; vkCreateImageView(logicalDevice_, &vi, nullptr, &v); return v;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& buf, VkDeviceMemory& mem, bool zero) {
        VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &buf), VK_SUCCESS);
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(logicalDevice_, buf, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &mem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, buf, mem, 0), VK_SUCCESS);
        if (zero) { void* m=nullptr; vkMapMemory(logicalDevice_, mem, 0, size, 0, &m); std::memset(m,0,size_t(size)); vkUnmapMemory(logicalDevice_, mem); }
    }

    // Sampled Lighting Inc0 M3: the same directional-light default LightingConfigNode uploads
    // in the real graph (MakeDefaultLightingConfig — direction normalize(1,1,-1), white
    // radiance, ambientIntensity 0.3) — this harness bypasses LightingConfigNode entirely, so
    // it must write this itself or the shaded output is pure black (lightCount=0).
    static Vixen::Gpu::LightingConfig MakeTestDefaultLightingConfig() {
        Vixen::Gpu::LightingConfig cfg{};
        cfg.lightCount       = 1u;
        cfg.ambientIntensity = 0.3f;
        const float dx = 1.0f, dy = 1.0f, dz = -1.0f;
        const float invLen = 1.0f / std::sqrt(dx*dx + dy*dy + dz*dz);
        cfg.lights[0].direction_or_positionX = dx * invLen;
        cfg.lights[0].direction_or_positionY = dy * invLen;
        cfg.lights[0].direction_or_positionZ = dz * invLen;
        cfg.lights[0].kind      = 0u;
        cfg.lights[0].radianceX = 1.0f;
        cfg.lights[0].radianceY = 1.0f;
        cfg.lights[0].radianceZ = 1.0f;
        cfg.lights[0].range     = 0.0f;
        return cfg;
    }

    void UploadBufferContent(VkDeviceMemory mem, const void* data, size_t size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memcpy(m, data, size);
        vkUnmapMemory(logicalDevice_, mem);
    }

    // Read the node's binding-5 config SSBO back through its externally visible VkBuffer.
    // BodyOctreeSceneNode deliberately exposes the buffer, not its backing allocation, so
    // this transfer is the only way to observe the GPU-visible residency stamp and active
    // compact pool base. The source buffer has TRANSFER_SRC usage for this diagnostic.
    void ReadbackOctreeConfigs(VkBuffer configBuf, uint32_t configCount,
                               std::vector<Vixen::SVO::OctreeConfig>& configs) {
        ASSERT_GT(configCount, 0u);
        const VkDeviceSize bytes = VkDeviceSize(configCount) * sizeof(Vixen::SVO::OctreeConfig);
        VkBuffer readback = VK_NULL_HANDLE;
        VkDeviceMemory readbackMem = VK_NULL_HANDLE;
        CreateHostBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readbackMem, false);

        // Ensure uploads submitted on any device queue are complete before this independent
        // readback submission. The in-submit barrier below supplies the copy visibility edge.
        ASSERT_EQ(vkDeviceWaitIdle(logicalDevice_), VK_SUCCESS);
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = commandPool_;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

        VkBufferMemoryBarrier sourceBarrier{};
        sourceBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        sourceBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        sourceBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sourceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sourceBarrier.buffer = configBuf;
        sourceBarrier.offset = 0;
        sourceBarrier.size = bytes;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             1, &sourceBarrier, 0, nullptr);

        VkBufferCopy copy{};
        copy.size = bytes;
        vkCmdCopyBuffer(cmd, configBuf, readback, 1, &copy);

        VkBufferMemoryBarrier readbackBarrier{};
        readbackBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        readbackBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        readbackBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        readbackBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readbackBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readbackBarrier.buffer = readback;
        readbackBarrier.offset = 0;
        readbackBarrier.size = bytes;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                             1, &readbackBarrier, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, readbackMem, 0, bytes, 0, &mapped), VK_SUCCESS);
        configs.resize(configCount);
        std::memcpy(configs.data(), mapped, static_cast<size_t>(bytes));
        vkUnmapMemory(logicalDevice_, readbackMem);

        vkDestroyBuffer(logicalDevice_, readback, nullptr);
        vkFreeMemory(logicalDevice_, readbackMem, nullptr);
    }

    // Render using the real BodyInstanceRayMarch shader (bindings 0-5,9-22; binding 8 does
    // not exist in the reflected SPIR-V — see the descriptor-layout comment below).
    void RenderToRgba(VkBuffer nodes, VkBuffer bricks, VkBuffer mats, VkBuffer cfg,
                      VkBuffer inst, VkBuffer sdf, VkBuffer lookup, VkBuffer mip,
                      const PushConstants& pc, uint32_t w, uint32_t h,
                      std::vector<uint8_t>& rgba, double& ms,
                      std::vector<HitRecordCpu>* outHitRecords = nullptr,
                      uint32_t* outInstanceIterCount = nullptr) {
        ASSERT_TRUE(deviceConfirmed_);
        // Baked-perf-pipeline M2: RayTraceBuffer (binding 4) is real, non-placeholder -- see
        // test_body_instance_occlusion_reject.cpp's identical fix for the fuller citation.
        constexpr VkDeviceSize kRayTraceBufferSize = 16 /*header*/ + 256 /*slots*/ * (16 + 64 * 48) /*TRACE_RAY_SIZE*/;
        VkBuffer traceBuf=VK_NULL_HANDLE;
        VkDeviceMemory traceMem=VK_NULL_HANDLE;
        CreateHostBuffer(kRayTraceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        VkBuffer dummySdf=VK_NULL_HANDLE, dummyLookup=VK_NULL_HANDLE, dummyMip=VK_NULL_HANDLE, dummyIter=VK_NULL_HANDLE, dummyTierRef=VK_NULL_HANDLE, dummyOccGrid=VK_NULL_HANDLE;
        VkDeviceMemory dSdfMem=VK_NULL_HANDLE, dLookupMem=VK_NULL_HANDLE, dMipMem=VK_NULL_HANDLE, dIterMem=VK_NULL_HANDLE, dTierRefMem=VK_NULL_HANDLE, dOccGridMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyIter,dIterMem,true);  // Inc1 M4b binding 14
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyTierRef,dTierRefMem,true);  // tier-crossing binding 15
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyOccGrid,dOccGridMem,true);  // M6 Task 13 occupancy grid binding 16
        if (sdf    == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySdf,dSdfMem,true); sdf = dummySdf; }
        if (lookup == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLookup,dLookupMem,true); lookup = dummyLookup; }
        if (mip    == VK_NULL_HANDLE) { CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyMip,dMipMem,true); mip = dummyMip; }

        // Sampled Lighting Inc0-Inc2 bindings (17-22): this harness only checks the raymarch
        // geometry/color output, not full shading pipeline correctness — LightingConfig gets a
        // real default light (see MakeTestDefaultLightingConfig; a zeroed one is pure black and
        // invisible to luminance checks), ShadowConfig/AccumulationConfig stay zeroed (enabled==0
        // skips shadow rays / keeps the temporal-accum seam a pure passthrough), HitRecord is
        // round-tripped internally, PrevCameraConfig/historyImage are unused this scope.
        VkBuffer dummyLighting=VK_NULL_HANDLE, dummyHitRecord=VK_NULL_HANDLE, dummyShadow=VK_NULL_HANDLE,
                 dummyAccum=VK_NULL_HANDLE, dummyPrevCam=VK_NULL_HANDLE;
        VkDeviceMemory dLightingMem=VK_NULL_HANDLE, dHitRecordMem=VK_NULL_HANDLE, dShadowMem=VK_NULL_HANDLE,
                       dAccumMem=VK_NULL_HANDLE, dPrevCamMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyLighting,dLightingMem,true);
        {
            const Vixen::Gpu::LightingConfig defaultLighting = MakeTestDefaultLightingConfig();
            ASSERT_NO_FATAL_FAILURE(UploadBufferContent(dLightingMem, &defaultLighting, sizeof(defaultLighting)));
        }
        // HitRecord.glsl's HitRecord struct is 64 bytes; the shader indexes it by the FULL
        // flat pixel count (up to w*h-1) every dispatch, so this buffer MUST be sized for the
        // actual w*h being rendered here.
        const VkDeviceSize hitRecordBufSize = VkDeviceSize(w) * VkDeviceSize(h) * 64;
        CreateHostBuffer(hitRecordBufSize,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyHitRecord,dHitRecordMem,true);
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyShadow,dShadowMem,true);
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyAccum,dAccumMem,true);
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummyPrevCam,dPrevCamMem,true);
        // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: InstanceSkipMaskBuffer (binding 35) placeholder.
        VkBuffer dummySkipMask=VK_NULL_HANDLE;
        VkDeviceMemory dSkipMaskMem=VK_NULL_HANDLE;
        CreateHostBuffer(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,dummySkipMask,dSkipMaskMem,true);

        VkImage colorImg=VK_NULL_HANDLE, idImg=VK_NULL_HANDLE, historyImg=VK_NULL_HANDLE;
        VkDeviceMemory colorMem=VK_NULL_HANDLE, idMem=VK_NULL_HANDLE, historyMem=VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R8G8B8A8_UNORM, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R32_UINT, idImg, idMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w,h,VK_FORMAT_R8G8B8A8_UNORM, historyImg, historyMem));
        VkImageView colorView   = MakeView(colorImg,   VK_FORMAT_R8G8B8A8_UNORM);
        VkImageView idView      = MakeView(idImg,      VK_FORMAT_R32_UINT);
        VkImageView historyView = MakeView(historyImg, VK_FORMAT_R8G8B8A8_UNORM);

        const auto spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(spirv.empty()) << "SPIR-V missing: " << GLSL_RAYMARCH_SPV;
        VkShaderModuleCreateInfo smc{}; smc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smc.codeSize = spirv.size()*4; smc.pCode = spirv.data();
        VkShaderModule sm = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smc, nullptr, &sm), VK_SUCCESS);

        auto bindL = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{}; lb.binding=b; lb.descriptorType=t;
            lb.descriptorCount=1; lb.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; return lb;
        };
        // NOTE: binding 8 (ShaderCounters debug SSBO) deliberately absent — removed from the
        // shader's reflected interface by 8509f58b (ENABLE_SHADER_COUNTERS compiled out
        // unconditionally; see SceneBindings.glsl's binding-8 comment). Including it here
        // desyncs this local layout from the SPIR-V module's actual resource interface,
        // which is a VUID-VkComputePipelineCreateInfo-layout-07988-class validation error
        // (root-caused 2026-07-15, Recipe-Parameterization M4).
        const std::array<VkDescriptorSetLayoutBinding,21> bindings = {
            bindL(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bindL(10,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bindL(14,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Inc1 M4b: per-instance iteration debug
            bindL(15,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Tiered-ESVO: TierRefTableBuffer
            bindL(16,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // M6 Task 13: OccupancyGridBuffer
            bindL(17,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc0 M3: LightingConfigSSBO
            bindL(18,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc1 M3: HitRecordBuffer
            bindL(19,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc1 M4: ShadowConfigSSBO
            bindL(20,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc2 M1: AccumulationConfigSSBO
            bindL(21,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),   // Sampled Lighting Inc2 M1: historyImage
            bindL(22,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Sampled Lighting Inc2 M3: PrevCameraConfigSSBO
            bindL(35,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Recipe-Live-App-Bucketed-Dispatch Inc4 M1: InstanceSkipMaskBuffer
        };
        VkDescriptorSetLayoutCreateInfo dslci{}; dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = uint32_t(bindings.size()); dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{}; pcr.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; pcr.size=sizeof(pc);
        VkPipelineLayoutCreateInfo plci{}; plci.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount=1; plci.pSetLayouts=&dsl; plci.pushConstantRangeCount=1; plci.pPushConstantRanges=&pcr;
        VkPipelineLayout pl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pl), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{}; cpci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; cpci.stage.module=sm; cpci.stage.pName="main";
        cpci.layout=pl;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_,VK_NULL_HANDLE,1,&cpci,nullptr,&pipeline), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize,2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 18},
        }};
        VkDescriptorPoolCreateInfo dpci{}; dpci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets=1; dpci.poolSizeCount=uint32_t(poolSizes.size()); dpci.pPoolSizes=poolSizes.data();
        VkDescriptorPool pool2 = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_,&dpci,nullptr,&pool2), VK_SUCCESS);
        VkDescriptorSetAllocateInfo dsai{}; dsai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool=pool2; dsai.descriptorSetCount=1; dsai.pSetLayouts=&dsl;
        VkDescriptorSet ds = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_,&dsai,&ds), VK_SUCCESS);

        VkDescriptorImageInfo colImg{VK_NULL_HANDLE,colorView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo idImgI{VK_NULL_HANDLE,idView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo historyImgI{VK_NULL_HANDLE,historyView,VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo nodesI{nodes,0,VK_WHOLE_SIZE}, bricksI{bricks,0,VK_WHOLE_SIZE},
            matsI{mats,0,VK_WHOLE_SIZE}, traceI{traceBuf,0,VK_WHOLE_SIZE}, cfgI{cfg,0,VK_WHOLE_SIZE},
            instI{inst,0,VK_WHOLE_SIZE},
            sdfI{sdf,0,VK_WHOLE_SIZE}, lookupI{lookup,0,VK_WHOLE_SIZE}, iterI{dummyIter,0,VK_WHOLE_SIZE}, mipI{mip,0,VK_WHOLE_SIZE},
            tierRefI{dummyTierRef,0,VK_WHOLE_SIZE}, occGridI{dummyOccGrid,0,VK_WHOLE_SIZE},
            lightingI{dummyLighting,0,VK_WHOLE_SIZE}, hitRecordI{dummyHitRecord,0,VK_WHOLE_SIZE},
            shadowI{dummyShadow,0,VK_WHOLE_SIZE}, accumI{dummyAccum,0,VK_WHOLE_SIZE},
            prevCamI{dummyPrevCam,0,VK_WHOLE_SIZE},
            skipMaskI{dummySkipMask,0,VK_WHOLE_SIZE};

        auto wI = [&](uint32_t b, VkDescriptorImageInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo=info; return w;
        };
        auto wB = [&](uint32_t b, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=ds; w.dstBinding=b; w.descriptorCount=1;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo=info; return w;
        };
        const std::array<VkWriteDescriptorSet,21> writes = {
            wI(0,&colImg), wB(1,&nodesI), wB(2,&bricksI), wB(3,&matsI), wB(4,&traceI),
            wB(5,&cfgI), wI(9,&idImgI), wB(10,&instI), wB(11,&sdfI), wB(12,&lookupI), wB(13,&mipI),
            wB(14,&iterI),  // Inc1 M4b: per-instance iteration debug
            wB(15,&tierRefI),  // Tiered-ESVO: TierRefTableBuffer
            wB(16,&occGridI),  // M6 Task 13: OccupancyGridBuffer
            wB(17,&lightingI), wB(18,&hitRecordI), wB(19,&shadowI), wB(20,&accumI),
            wI(21,&historyImgI), wB(22,&prevCamI),
            wB(35,&skipMaskI),  // Recipe-Live-App-Bucketed-Dispatch Inc4 M1
        };
        vkUpdateDescriptorSets(logicalDevice_, uint32_t(writes.size()), writes.data(), 0, nullptr);

        VkCommandBufferAllocateInfo cbai{}; cbai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool=commandPool_; cbai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount=1;
        VkCommandBuffer cmd=VK_NULL_HANDLE; ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_,&cbai,&cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

        auto toGeneral = [&](VkImage img) {
            VkImageMemoryBarrier b{}; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.image=img; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
        };
        toGeneral(colorImg); toGeneral(idImg); toGeneral(historyImg);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, nullptr);
        vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w+7)/8, (h+7)/8, 1);

        // Binding 14 is a per-instance, non-atomic store. It is meaningful only for a
        // single-pixel dispatch; never interpret it after the normal full-frame render,
        // where all pixels race on instanceIterCount[0].
        if (outInstanceIterCount != nullptr) {
            VkBufferMemoryBarrier iterBarrier{};
            iterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            iterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            iterBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            iterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            iterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            iterBarrier.buffer = dummyIter;
            iterBarrier.offset = 0;
            iterBarrier.size = sizeof(uint32_t);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                                 1, &iterBarrier, 0, nullptr);
        }

        // KI-032 fix: barrier the HitRecord SSBO (shader write -> host read) before the host
        // reads it below -- same pattern test_recipe_pool_render.cpp's identical fix uses.
        VkBufferMemoryBarrier hitRecordBarrier{}; hitRecordBarrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hitRecordBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; hitRecordBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        hitRecordBarrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; hitRecordBarrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        hitRecordBarrier.buffer=dummyHitRecord; hitRecordBarrier.offset=0; hitRecordBarrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0,0,nullptr,1,&hitRecordBarrier,0,nullptr);

        VkImageMemoryBarrier toSrc{}; toSrc.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout=VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        toSrc.image=colorImg; toSrc.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        toSrc.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&toSrc);

        const VkDeviceSize rbSz = VkDeviceSize(w)*h*4;
        VkBuffer rb=VK_NULL_HANDLE; VkDeviceMemory rbMem=VK_NULL_HANDLE;
        CreateHostBuffer(rbSz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rb, rbMem, false);
        VkBufferImageCopy cp{}; cp.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; cp.imageExtent={w,h,1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &cp);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount=1; si.pCommandBuffers=&cmd;
        ASSERT_TRUE(deviceConfirmed_);
        const auto t0 = std::chrono::steady_clock::now();
        ASSERT_EQ(vkQueueSubmit(queue_,1,&si,VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
        const auto t1 = std::chrono::steady_clock::now();
        ms = double(std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count());

        void* mapped=nullptr; ASSERT_EQ(vkMapMemory(logicalDevice_,rbMem,0,rbSz,0,&mapped), VK_SUCCESS);
        rgba.assign(size_t(w)*h*4, 0); std::memcpy(rgba.data(), mapped, size_t(rbSz));
        vkUnmapMemory(logicalDevice_, rbMem);

        if (outHitRecords != nullptr) {
            void* hrMapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, dHitRecordMem, 0, hitRecordBufSize, 0, &hrMapped), VK_SUCCESS);
            outHitRecords->assign(size_t(w) * h, HitRecordCpu{});
            std::memcpy(outHitRecords->data(), hrMapped, size_t(hitRecordBufSize));
            vkUnmapMemory(logicalDevice_, dHitRecordMem);
        }

        if (outInstanceIterCount != nullptr) {
            void* iterMapped = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, dIterMem, 0, sizeof(uint32_t), 0, &iterMapped), VK_SUCCESS);
            std::memcpy(outInstanceIterCount, iterMapped, sizeof(uint32_t));
            vkUnmapMemory(logicalDevice_, dIterMem);
        }

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_,rb,nullptr); vkFreeMemory(logicalDevice_,rbMem,nullptr);
        vkDestroyDescriptorPool(logicalDevice_,pool2,nullptr);
        vkDestroyPipeline(logicalDevice_,pipeline,nullptr);
        vkDestroyPipelineLayout(logicalDevice_,pl,nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_,dsl,nullptr);
        vkDestroyShaderModule(logicalDevice_,sm,nullptr);
        vkDestroyImageView(logicalDevice_,colorView,nullptr); vkDestroyImageView(logicalDevice_,idView,nullptr);
        vkDestroyImageView(logicalDevice_,historyView,nullptr);
        vkDestroyImage(logicalDevice_,colorImg,nullptr); vkFreeMemory(logicalDevice_,colorMem,nullptr);
        vkDestroyImage(logicalDevice_,idImg,nullptr);    vkFreeMemory(logicalDevice_,idMem,nullptr);
        vkDestroyImage(logicalDevice_,historyImg,nullptr); vkFreeMemory(logicalDevice_,historyMem,nullptr);
        vkDestroyBuffer(logicalDevice_,traceBuf,nullptr); vkFreeMemory(logicalDevice_,traceMem,nullptr);
        if (dummySdf    != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummySdf,nullptr);    vkFreeMemory(logicalDevice_,dSdfMem,nullptr); }
        if (dummyLookup != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyLookup,nullptr); vkFreeMemory(logicalDevice_,dLookupMem,nullptr); }
        if (dummyMip    != VK_NULL_HANDLE) { vkDestroyBuffer(logicalDevice_,dummyMip,nullptr);    vkFreeMemory(logicalDevice_,dMipMem,nullptr); }
        vkDestroyBuffer(logicalDevice_,dummyIter,nullptr); vkFreeMemory(logicalDevice_,dIterMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyTierRef,nullptr); vkFreeMemory(logicalDevice_,dTierRefMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyOccGrid,nullptr); vkFreeMemory(logicalDevice_,dOccGridMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyLighting,nullptr);   vkFreeMemory(logicalDevice_,dLightingMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyHitRecord,nullptr);  vkFreeMemory(logicalDevice_,dHitRecordMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyShadow,nullptr);     vkFreeMemory(logicalDevice_,dShadowMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyAccum,nullptr);      vkFreeMemory(logicalDevice_,dAccumMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummyPrevCam,nullptr);    vkFreeMemory(logicalDevice_,dPrevCamMem,nullptr);
        vkDestroyBuffer(logicalDevice_,dummySkipMask,nullptr);   vkFreeMemory(logicalDevice_,dSkipMaskMem,nullptr);
    }

    // Bakes one sphere via ConcatenateSdfWithMips (real mip pool), renders it at
    // the given residency, and returns pixel-coverage + per-row-band stats used to
    // check the silhouette is round (not just "some pixels lit").
    struct RenderStats {
        int hitPixels = 0;
        int centerColBandHits = 0;   // hits in the vertical center column band (should be tall for a sphere)
        int edgeColBandHits   = 0;   // hits near the left/right edges (should be near-zero for a sphere)
    };

    // Perf fix (root-caused 2026-07-12): bakes the sphere ONCE and caches it
    // (function-local static, lazily initialized on first call) instead of re-baking
    // per call -- BakeRecipeToSdfWorld's per-voxel Gaia ECS createVoxel loop (~10^5
    // voxels, ~5 archetype migrations + a per-voxel query each) costs tens of
    // seconds PER bake, and every caller of this helper bakes the IDENTICAL
    // radius-22/n=64/band=2.5/depth=3 sphere. ConcatenateSdfWithMips itself is cheap
    // (serialize + mip-bake over an already-built octree, not a re-bake) and must
    // stay per-call since RenderPoolAndMeasure consumes (std::move) its pool.
    static const Vixen::SVO::SdfBodyOctree& CachedSphereBody() {
        static const Vixen::SVO::SdfBodyOctree* const cached = [] {
            constexpr float kRadius = 22.0f;
            const glm::vec3 center(32.0f, 32.0f, 32.0f);
            Vixen::SVO::RecipeParams rp{}; rp.radius = kRadius;
            Vixen::SVO::SdfBakeResult baked =
                Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, center, rp,
                                                  /*n=*/64, /*bandVoxels=*/2.5f, /*brickDepth=*/3);
            return new Vixen::SVO::SdfBodyOctree(Vixen::SVO::BuildSdfBodyOctree(baked, 3));
        }();
        return *cached;
    }

    void BakeRenderAndMeasure(bool residencyRequested, const char* outPath, RenderStats& stats) {
        std::vector<const Vixen::SVO::SdfBodyOctree*> ptrs{&CachedSphereBody()};
        Vixen::SVO::ConcatenatedOctrees pool = Vixen::SVO::ConcatenateSdfWithMips(ptrs);
        ASSERT_GT(pool.mipPool.size(), 0u) << "ConcatenateSdfWithMips must bake a non-empty mip pool";

        RenderPoolAndMeasure(std::move(pool), residencyRequested, outPath, stats);
    }

    // Lazy-Procedural-Delta-Baseline Inc0 M1 Task 3b — the same render+measure
    // machinery, but driven from a pre-built pool (e.g. BakeRegistryToPool's
    // output) rather than baking a sphere inline. Proves a SetRecipePool-fed
    // node renders the mip fallback with REAL samples when the pool came from
    // the production baker path, not just the direct ConcatenateSdfWithMips call.
    void RenderPoolAndMeasure(Vixen::SVO::ConcatenatedOctrees pool, bool residencyRequested,
                              const char* outPath, RenderStats& stats) {
        using C = BodyOctreeSceneNodeConfig;

        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance("mip_fallback_test");
        auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
        ASSERT_NE(node, nullptr);

        Resource devRes;  SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
        Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frRes;   uint32_t frameIndex=0; SetHandleVal<uint32_t>(frRes, frameIndex);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);

        node->SetRecipePool(std::move(pool));
        node->RequestBrickResidency(residencyRequested);

        // BodyInstanceRayMarch.comp places a body at worldPos, scaled by renderScale,
        // over the octree's [0, kWorldGridSize] local extent (see BodyCentre below) —
        // NOT at the SDF bake-space `center` (that's an internal grid coordinate of the
        // baked octree, unrelated to world placement). worldPos=(0,0,0), renderScale=1
        // is the simplest placement: the body's true world-space centre is exactly
        // 0.5*kWorldGridSize along each axis.
        constexpr float kRenderScale = 1.0f;
        const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
            MakeInst(0.0f, 0.0f, 0.0f, kRenderScale, 0u),
        };
        const glm::vec3 bodyCentre(0.5f * kWorldGridSize * kRenderScale);
        node->SetInstances(instances);
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());

        // A residency change requested AFTER Compile (residencyRequested_ defaults
        // true, per the M3 fix) needs one more Execute tick to service the dirty flag
        // when explicitly requesting FALSE — but RequestBrickResidency(false) above was
        // called BEFORE Setup/Compile, so CreateOctreeBuffers already saw it; no extra
        // tick needed either way. Confirmed by test: exercised both true/false below.

        auto buf = [&](int slot) -> VkBuffer {
            return node->GetOutput(slot, 0)->GetHandle<VkBuffer>();
        };
        VkBuffer nodes   = buf(C::OCTREE_NODES_BUFFER_Slot::index);
        VkBuffer bricks  = buf(C::OCTREE_BRICKS_BUFFER_Slot::index);
        VkBuffer mats    = buf(C::OCTREE_MATERIALS_BUFFER_Slot::index);
        VkBuffer cfgBuf  = buf(C::OCTREE_CONFIG_BUFFER_Slot::index);
        VkBuffer instBuf = buf(C::INSTANCE_BUFFER_Slot::index);
        // BuildRenderGraph wires the compact shell pair to shader bindings 11/12. The
        // source pair remains a separate producer output and is not the active render payload
        // after BodyOctreeSceneNode derives its shell cache.
        VkBuffer shellDataBuf   = buf(C::SHELL_DATA_BUFFER_Slot::index);
        VkBuffer shellLookupBuf = buf(C::SHELL_LOOKUP_BUFFER_Slot::index);
        VkBuffer mipBuf  = buf(C::OCTREE_MIPPOOL_BUFFER_Slot::index);
        ASSERT_NE(nodes, VK_NULL_HANDLE); ASSERT_NE(cfgBuf, VK_NULL_HANDLE);
        ASSERT_NE(shellDataBuf, VK_NULL_HANDLE); ASSERT_NE(shellLookupBuf, VK_NULL_HANDLE);
        ASSERT_NE(mipBuf, VK_NULL_HANDLE);

        constexpr uint32_t kW=512, kH=512;
        // Fit the sphere (radius kRadius in grid-voxel units, occupying roughly
        // ±kRadius/n of the [0,kWorldGridSize] world extent) with margin at 45° FOV.
        const float dist = 2.2f * kWorldGridSize * kRenderScale;
        const glm::vec3 eye = bodyCentre + glm::vec3(0.0f, 0.0f, dist);
        const PushConstants pc = MakeCamera(eye, bodyCentre, kW, kH, 1);

        std::vector<uint8_t> rgba; double ms = 0.0;
        std::vector<HitRecordCpu> hitRecords;
        ASSERT_NO_FATAL_FAILURE(RenderToRgba(nodes, bricks, mats, cfgBuf, instBuf,
                                             shellDataBuf, shellLookupBuf, mipBuf,
                                             pc, kW, kH, rgba, ms, &hitRecords));

        // KI-032 fix: PNG rendered from HitRecord.albedo (still written post-KI-018), not the
        // dead colorImg -- see this file's HitRecordCpu comment.
        {
            std::vector<uint8_t> rgb(size_t(kW)*kH*3);
            for (uint32_t i = 0; i < kW*kH; ++i) {
                const HitRecordCpu& rec = hitRecords[i];
                const bool hit = (rec.flags & kHitRecordFlagHit) != 0u;
                rgb[i*3+0] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[0], 0.0f, 1.0f) * 255.0f) : 0;
                rgb[i*3+1] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[1], 0.0f, 1.0f) * 255.0f) : 0;
                rgb[i*3+2] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[2], 0.0f, 1.0f) * 255.0f) : 0;
            }
            stbi_write_png(outPath, int(kW), int(kH), 3, rgb.data(), int(kW)*3);
        }

        stats = RenderStats{};
        // Center column band: x in [kW*0.45, kW*0.55) — a round silhouette centered
        // on screen should be hit almost everywhere along y in this band.
        // Edge column band: x in [0, kW*0.05) — outside a centered sphere's radius,
        // should be almost entirely sky (near-zero hits) for a proper round shape.
        // KI-032 fix: hit test reads HitRecordBuffer.flags instead of the dead colorImg.
        const uint32_t centerXLo = uint32_t(kW*0.45f), centerXHi = uint32_t(kW*0.55f);
        const uint32_t edgeXHi   = uint32_t(kW*0.05f);
        for (uint32_t y = 0; y < kH; ++y) {
            for (uint32_t x = 0; x < kW; ++x) {
                const uint32_t i = y*kW + x;
                const bool hit = (hitRecords[i].flags & kHitRecordFlagHit) != 0u;
                if (!hit) continue;
                ++stats.hitPixels;
                if (x >= centerXLo && x < centerXHi) ++stats.centerColBandHits;
                if (x < edgeXHi) ++stats.edgeColBandHits;
            }
        }
        std::printf("[MIP-FALLBACK] residencyRequested=%d | total=%d centerBand=%d edgeBand=%d | render=%.0f ms -> %s\n",
                    int(residencyRequested), stats.hitPixels, stats.centerColBandHits,
                    stats.edgeColBandHits, ms, outPath);

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
    }

    // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4b — multi-octree post-grant
    // correctness. Builds a pool of `octreeCount` mip-baked spheres (concatenated via
    // ConcatenateSdfWithMips, so a shell cache derives at Compile — DeriveShellCache/
    // CreateShellBuffers only run when concatenated_.channelPool is non-empty, which
    // requires a REAL Stored-SDF pool, not the earlier single-octree tests' minimum),
    // places the ONE instance at `targetOctreeIndex` (>=1 for the bug this milestone
    // fixes: PollBrickUploadCompletion's phase-2 config re-upload was clobbering
    // CreateShellBuffers' shell-compact poolBrickBase rewrite for index >=1), renders
    // BEFORE residency is granted (mip fallback, camera-facing) and AFTER an
    // ExecuteImpl-driven grant (RequestBrickResidency(true) called POST-Compile, so
    // it goes through the real async UploadBrickPool/PollBrickUploadCompletion state
    // machine Task 4b patches — not the pre-Compile CreateOctreeBuffers path the
    // other tests above exercise). A wrong config buffer post-grant would corrupt
    // channelPool addressing for this instance and either blank the render or hit a
    // wildly wrong sample -- both are non-round/degenerate silhouettes the same
    // round-shape check below already catches.
    void RenderMultiOctreePostGrantAndMeasure(uint32_t octreeCount, uint32_t targetOctreeIndex,
                                              RenderStats& beforeStats, RenderStats& afterStats) {
        using C = BodyOctreeSceneNodeConfig;
        ASSERT_LT(targetOctreeIndex, octreeCount);

        // Perf fix (root-caused 2026-07-12): reuses the SAME cached bake
        // CachedSphereBody() (below) provides -- this fixture's sphere is byte-identical
        // (radius-22/n=64/band=2.5/depth=3/center(32,32,32)) to every other test in this
        // file, so the whole suite now pays for exactly ONE bake total, not one per test.
        // ConcatenateSdfWithMips derives each octree's poolBrickBase from its LOOP INDEX
        // (MipBake.h:336-353), not object identity, so pointing every slot at the same
        // cached SdfBodyOctree still produces the distinct-per-index poolBrickBase
        // values line ~786 below asserts on.
        std::vector<const Vixen::SVO::SdfBodyOctree*> ptrs(octreeCount, &CachedSphereBody());
        Vixen::SVO::ConcatenatedOctrees pool = Vixen::SVO::ConcatenateSdfWithMips(ptrs);
        ASSERT_EQ(pool.count, octreeCount);
        ASSERT_GT(pool.mipPool.size(), 0u);
        // Distinct poolBrickBase per octree proves this fixture actually exercises
        // Task 4b's addressing bug (a single-octree pool has poolBrickBase==0
        // everywhere and can't distinguish source-vs-compact re-uploads).
        if (octreeCount > 1u) {
            ASSERT_NE(pool.configs[0].poolBrickBase, pool.configs[1].poolBrickBase);
        }
        const uint32_t sourcePoolBrickBase = pool.configs[targetOctreeIndex].poolBrickBase;
        const uint32_t sourceBrickLookupBase = pool.configs[targetOctreeIndex].brickLookupBase;

        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance("multi_octree_grant_test");
        auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
        ASSERT_NE(node, nullptr);
        // DIAGNOSTIC (temporary, root-causing the before==after identical-render bug):
        // NODE_LOG_INFO is disabled by default per-node; only NODE_LOG_ERROR bypasses that
        // gate (NodeLogging.h:48-49). Force INFO+terminal output so the residency/upload
        // state-machine's own log lines (RequestBrickResidency dirty=, UploadBrickPool
        // no-op/failed, PollBrickUploadCompletion phase transitions) are actually visible.
        if (auto* lg = node->GetLogger()) { lg->SetEnabled(true); lg->SetTerminalOutput(true); }

        Resource devRes;  SetHandleVal<VulkanDevice*>(devRes, deviceShell_.get());
        Resource poolRes; SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frRes;   uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &devRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frRes);

        node->SetRecipePool(std::move(pool));
        // No RequestBrickResidency call here — M2's capability-derived default
        // takes over (every tree is mip-baked -> boots lazy), matching this test's
        // "before grant" render below with no explicit pin needed.

        constexpr float kRenderScale = 1.0f;
        const std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
            MakeInst(0.0f, 0.0f, 0.0f, kRenderScale, targetOctreeIndex),
        };
        const glm::vec3 bodyCentre(0.5f * kWorldGridSize * kRenderScale);
        node->SetInstances(instances);
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        EXPECT_FALSE(node->IsResidencyRequested())
            << "every octree in this pool is mip-capable -> M2 default must derive LAZY";

        frameIndex = 0; SetHandleVal<uint32_t>(frRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());  // boot tick: mip-only, no brick upload queued

        auto buf = [&](int slot) -> VkBuffer {
            return node->GetOutput(slot, 0)->GetHandle<VkBuffer>();
        };
        const VkBuffer configBuf = buf(C::OCTREE_CONFIG_BUFFER_Slot::index);
        const VkBuffer sourceSdfBuf = buf(C::OCTREE_SDF_BUFFER_Slot::index);
        const VkBuffer sourceLookupBuf = buf(C::OCTREE_BRICKLOOKUP_BUFFER_Slot::index);
        const VkBuffer shellDataBuf = buf(C::SHELL_DATA_BUFFER_Slot::index);
        const VkBuffer shellLookupBuf = buf(C::SHELL_LOOKUP_BUFFER_Slot::index);
        const VkBuffer mipBuf = buf(C::OCTREE_MIPPOOL_BUFFER_Slot::index);
        ASSERT_NE(configBuf, VK_NULL_HANDLE);
        ASSERT_NE(sourceSdfBuf, VK_NULL_HANDLE); ASSERT_NE(sourceLookupBuf, VK_NULL_HANDLE);
        ASSERT_NE(shellDataBuf, VK_NULL_HANDLE); ASSERT_NE(shellLookupBuf, VK_NULL_HANDLE);
        ASSERT_NE(mipBuf, VK_NULL_HANDLE);
        constexpr uint32_t kW = 512, kH = 512;
        const float dist = 2.2f * kWorldGridSize * kRenderScale;
        const glm::vec3 eye = bodyCentre + glm::vec3(0.0f, 0.0f, dist);
        const PushConstants pc = MakeCamera(eye, bodyCentre, kW, kH, 1);

        auto reportConfigState = [&](const char* stage) {
            std::vector<Vixen::SVO::OctreeConfig> gpuConfigs;
            ASSERT_NO_FATAL_FAILURE(ReadbackOctreeConfigs(configBuf, octreeCount, gpuConfigs));
            ASSERT_EQ(gpuConfigs.size(), octreeCount);
            const auto& cfg = gpuConfigs[targetOctreeIndex];
            std::printf("[MULTI-OCTREE-GRANT] %s config index=%u brickResident=%u "
                        "readinessMask=%u "
                        "poolBrickBase=%u brickLookupBase=%u sourcePoolBrickBase=%u "
                        "sourceBrickLookupBase=%u\n",
                        stage, targetOctreeIndex, cfg.brickResident, cfg._tailPad[0],
                        cfg.poolBrickBase, cfg.brickLookupBase, sourcePoolBrickBase,
                        sourceBrickLookupBase);
        };

        // A one-pixel dispatch is the only valid binding-14 observation: the shader writes
        // instanceIterCount[instIdx] non-atomically, so a full image would make every pixel
        // a multi-writer race. Nonzero iterations prove the second-tree traversal entered;
        // the HitRecord flag independently proves whether that traversal produced a hit.
        auto probeTraversal = [&](const char* stage, VkBuffer channelPool, VkBuffer brickLookup) {
            PushConstants probePc = pc;
            probePc.debugTargetPixel = glm::ivec2(0, 0);
            std::vector<uint8_t> probeRgba;
            std::vector<HitRecordCpu> probeHits;
            double probeMs = 0.0;
            uint32_t iterCount = 0u;
            ASSERT_NO_FATAL_FAILURE(RenderToRgba(
                buf(C::OCTREE_NODES_BUFFER_Slot::index),
                buf(C::OCTREE_BRICKS_BUFFER_Slot::index),
                buf(C::OCTREE_MATERIALS_BUFFER_Slot::index), configBuf,
                buf(C::INSTANCE_BUFFER_Slot::index), channelPool, brickLookup, mipBuf,
                probePc, 1u, 1u, probeRgba, probeMs, &probeHits, &iterCount));
            ASSERT_EQ(probeHits.size(), 1u);
            const bool hit = (probeHits[0].flags & kHitRecordFlagHit) != 0u;
            std::printf("[MULTI-OCTREE-GRANT] %s binding14-1x1 iter=%u hit=%u hitT=%.5f\n",
                        stage, iterCount, unsigned(hit), probeHits[0].hitT);
        };

        reportConfigState("boot");
        probeTraversal("boot source-pair", sourceSdfBuf, sourceLookupBuf);

        auto measure = [&](const char* outPath, RenderStats& stats) {
            std::vector<uint8_t> rgba; double ms = 0.0;
            std::vector<HitRecordCpu> hitRecords;
            ASSERT_NO_FATAL_FAILURE(RenderToRgba(
                buf(C::OCTREE_NODES_BUFFER_Slot::index), buf(C::OCTREE_BRICKS_BUFFER_Slot::index),
                buf(C::OCTREE_MATERIALS_BUFFER_Slot::index), configBuf,
                buf(C::INSTANCE_BUFFER_Slot::index), shellDataBuf, shellLookupBuf, mipBuf,
                pc, kW, kH, rgba, ms, &hitRecords));
            // KI-032 fix: PNG rendered from HitRecord.albedo (still written post-KI-018), not
            // the dead colorImg -- see this file's HitRecordCpu comment.
            {
                std::vector<uint8_t> rgb(size_t(kW)*kH*3);
                for (uint32_t i = 0; i < kW*kH; ++i) {
                    const HitRecordCpu& rec = hitRecords[i];
                    const bool hit = (rec.flags & kHitRecordFlagHit) != 0u;
                    rgb[i*3+0] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[0], 0.0f, 1.0f) * 255.0f) : 0;
                    rgb[i*3+1] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[1], 0.0f, 1.0f) * 255.0f) : 0;
                    rgb[i*3+2] = hit ? static_cast<uint8_t>(std::clamp(rec.albedo[2], 0.0f, 1.0f) * 255.0f) : 0;
                }
                stbi_write_png(outPath, int(kW), int(kH), 3, rgb.data(), int(kW)*3);
            }
            stats = RenderStats{};
            const uint32_t centerXLo = uint32_t(kW*0.45f), centerXHi = uint32_t(kW*0.55f);
            const uint32_t edgeXHi   = uint32_t(kW*0.05f);
            for (uint32_t y = 0; y < kH; ++y) {
                for (uint32_t x = 0; x < kW; ++x) {
                    const uint32_t i = y*kW + x;
                    const bool hit = (hitRecords[i].flags & kHitRecordFlagHit) != 0u;
                    if (!hit) continue;
                    ++stats.hitPixels;
                    if (x >= centerXLo && x < centerXHi) ++stats.centerColBandHits;
                    if (x < edgeXHi) ++stats.edgeColBandHits;
                }
            }
            std::printf("[MULTI-OCTREE-GRANT] octreeIndex=%u total=%d centerBand=%d edgeBand=%d -> %s\n",
                        targetOctreeIndex, stats.hitPixels, stats.centerColBandHits,
                        stats.edgeColBandHits, outPath);
        };

        measure("/tmp/multi_octree_grant_before.png", beforeStats);

        // Grant residency POST-Compile — exercises the real UploadBrickPool/
        // PollBrickUploadCompletion async state machine (Task 4b's actual target),
        // not CreateOctreeBuffers' pre-Compile path.
        node->RequestBrickResidency(true);
        // Time-bounded poll, not a fixed tick count (root-caused 2026-07-12: a fixed
        // 6-tick loop silently PASSES vacuously when the GPU copy hasn't completed yet --
        // no error, "after" just re-renders "before"'s state, and the gate can't tell the
        // difference between "genuinely instant" and "never actually landed"). Poll real
        // elapsed wall-clock time against IsBrickPoolUploaded() -- NOT BootBytesUploaded(),
        // which is a DIFFERENT signal that flips non-zero the instant the upload is QUEUED
        // (UploadBrickPool, BodyOctreeSceneNode.cpp:967-968), before the GPU copy has
        // actually landed (that's IsBrickPoolUploaded(), set only inside
        // PollBrickUploadCompletion once device->IsUploadComplete() is true,
        // BodyOctreeSceneNode.cpp:991). A first version of this fix polled BootBytesUploaded
        // and stopped after 1 tick / 2.23ms every time, well before the real GPU completion
        // -- proven by "after" still rendering byte-identical to "before" despite the poll
        // reporting success; this is why BOTH signals matter and must not be conflated.
        constexpr auto kUploadPollTimeout = std::chrono::seconds(30);
        const auto pollStart = std::chrono::steady_clock::now();
        int ticksUntilUploaded = 0;
        while (!node->IsBrickPoolUploaded() &&
               std::chrono::steady_clock::now() - pollStart < kUploadPollTimeout) {
            ++frameIndex; SetHandleVal<uint32_t>(frRes, frameIndex);
            ASSERT_NO_THROW(node->Execute());
            ++ticksUntilUploaded;
        }
        const auto phase1Elapsed = std::chrono::steady_clock::now() - pollStart;
        std::printf("[MULTI-OCTREE-GRANT] phase1 (brick upload) poll: %d ticks, %.2f ms wall-clock, "
                    "IsBrickPoolUploaded=%d\n",
                    ticksUntilUploaded,
                    std::chrono::duration<double, std::milli>(phase1Elapsed).count(),
                    int(node->IsBrickPoolUploaded()));
        ASSERT_TRUE(node->IsBrickPoolUploaded())
            << "Brick upload did not complete within " << kUploadPollTimeout.count()
            << "s of wall-clock polling (" << ticksUntilUploaded << " Execute() ticks) -- "
               "either PollBrickUploadCompletion never observed GPU completion, or the "
               "machine is under such extreme scheduling contention that even a real, "
               "already-submitted small GPU copy cannot complete in 30s. Re-run when the "
               "machine is less loaded before treating this as a code regression.";

        // Phase 2 (BodyOctreeSceneNode.cpp:1023-1030): the shell-compact config re-upload
        // that actually stamps brickResident=1 into the buffer the shader samples -- until
        // THIS lands, the shader still reads the pre-grant config and renders the mip
        // fallback regardless of phase 1's completion. No public accessor exists for
        // pendingConfigUploadHandle_'s state, so poll by TIME (a fixed small number of
        // ticks proved sufficient once phase 1's real timing was known -- both phases
        // complete within single-digit ms once actually triggered, per the 2026-07-12
        // measurement: phase 1 lands same-tick when polled correctly) rather than
        // reintroducing an unobserved fixed-count assumption.
        constexpr auto kConfigPollTimeout = std::chrono::seconds(5);
        const auto configPollStart = std::chrono::steady_clock::now();
        int configTicks = 0;
        while (std::chrono::steady_clock::now() - configPollStart < kConfigPollTimeout &&
               configTicks < 8) {
            ++frameIndex; SetHandleVal<uint32_t>(frRes, frameIndex);
            ASSERT_NO_THROW(node->Execute());
            ++configTicks;
        }
        std::printf("[MULTI-OCTREE-GRANT] phase2 (config re-upload) drain: %d ticks, %.2f ms wall-clock\n",
                    configTicks,
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - configPollStart).count());

        reportConfigState("post-grant");
        probeTraversal("post-grant source-pair", sourceSdfBuf, sourceLookupBuf);
        probeTraversal("post-grant shell-pair", shellDataBuf, shellLookupBuf);

        measure("/tmp/multi_octree_grant_after.png", afterStats);

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
    }
};

// ---------------------------------------------------------------------------
// Task 9 gate: mip-only tree (residency NEVER requested) renders a recognizable
// silhouette from mip samples alone — non-trivial pixel coverage AND a round
// shape (dense center-column hits, near-empty edge-column hits), not just
// "some pixels are lit" (the Inc2 M6 precedent this Plan's Task 9 cites).
// ---------------------------------------------------------------------------
TEST_F(MipFallbackRenderTest, MipOnlyTreeRendersRoundSilhouette) {
    std::printf("[ device ] %s\n", selectedDeviceName_.c_str());
    ASSERT_TRUE(deviceConfirmed_);

    RenderStats stats;
    ASSERT_NO_FATAL_FAILURE(
        BakeRenderAndMeasure(/*residencyRequested=*/false, "/tmp/mip_fallback_render.png", stats));

    EXPECT_GT(stats.hitPixels, 5000)
        << "Mip-only tree should render a non-trivial silhouette from mip samples alone";
    // Round-shape check: the center column band (spanning the sphere's widest point)
    // must be almost entirely hit; the edge band (outside the sphere's radius) must be
    // almost entirely empty. A silhouette-only pixel-count check can't tell a round
    // blob from a degenerate full-screen fill or a thin sliver — this can.
    const int centerBandRows = int(512 * 0.10f);  // band width in x, full height in y -> 512 rows tall
    EXPECT_GT(stats.centerColBandHits, int(512 * 0.5f))
        << "Center column band should be substantially covered by a centered sphere";
    EXPECT_LT(stats.edgeColBandHits, centerBandRows / 4)
        << "Edge column band should be mostly empty (sky) for a round, centered silhouette "
           "— a full-screen fill or degenerate shape would light this band up too";
}

// ---------------------------------------------------------------------------
// No-regression: the SAME baked pool with residency explicitly requested TRUE
// (bricks fully uploaded, real trilinear SDF march) must ALSO render a
// comparable silhouette — proves Task 7's existence check doesn't misfire and
// suppress the real march path when bricks ARE resident.
// ---------------------------------------------------------------------------
TEST_F(MipFallbackRenderTest, ResidentTreeRendersComparableSilhouette) {
    ASSERT_TRUE(deviceConfirmed_);

    RenderStats mipOnly, resident;
    ASSERT_NO_FATAL_FAILURE(
        BakeRenderAndMeasure(/*residencyRequested=*/false, "/tmp/mip_fallback_mip_only.png", mipOnly));
    ASSERT_NO_FATAL_FAILURE(
        BakeRenderAndMeasure(/*residencyRequested=*/true, "/tmp/mip_fallback_resident.png", resident));

    EXPECT_GT(resident.hitPixels, 5000)
        << "Fully-resident tree must render a non-trivial silhouette (real brick march)";
    // Both should show a round silhouette of the SAME sphere/camera, but they are NOT
    // expected to match pixel-for-pixel: Task 7's fallback is a hard-switch "does this
    // leaf's brick have ANY occupied (near-surface-band) voxel" test (coverage > 0),
    // not a true iso-surface intersection test (direction doc point 4 / plan Task 7:
    // "v1 = hard switch... a coarse... representation, not an iso-surface march"). A
    // narrow-band SDF (bandVoxels=2.5) bakes occupied voxels within ~2.5 voxels of the
    // true surface in every direction, so grazing rays that clip a near-surface leaf's
    // bounding cube without crossing the true curved surface still register a mip hit —
    // the mip-only silhouette is EXPECTED to be somewhat larger than the resident
    // iso-surface march's, not equal. The bound below catches genuine breakage (a
    // vanishing or wildly exploded silhouette), not this documented coarseness.
    const double ratio = double(mipOnly.hitPixels) / double(resident.hitPixels);
    EXPECT_GT(ratio, 0.5) << "Mip-only silhouette is suspiciously smaller than the resident render";
    EXPECT_LT(ratio, 6.0) << "Mip-only silhouette is implausibly larger than the resident render "
                             "(expect some growth from the coarse hard-switch test, not an explosion)";
}

// ---------------------------------------------------------------------------
// Lazy-Procedural-Delta-Baseline Inc0 M1 Task 3b — this is the M2 gate's
// offscreen twin: a pool baked through the PRODUCTION path (RecipeRegistry ->
// BakeRegistryToPool, exactly what a real SetRecipePool caller would use, not
// the direct ConcatenateSdfWithMips call the tests above exercise) must ALSO
// render the mip fallback with real samples when bricks are never made
// resident — proving BakeRegistryToPool's Task 1 mip wiring is load-bearing,
// not just ConcatenateSdfWithMips in isolation.
// ---------------------------------------------------------------------------
TEST_F(MipFallbackRenderTest, RegistryBakedPoolRendersMipFallback) {
    ASSERT_TRUE(deviceConfirmed_);

    Vixen::SVO::RecipeRegistry reg;
    Vixen::SVO::RecipeRegistry::RecipeEntry sphere{};
    Vixen::SVO::Recipe::SdfInstruction in{};
    in.opCode  = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    in.data[0] = 0.0f; in.data[1] = 0.0f; in.data[2] = 0.0f; in.data[3] = 22.0f;  // object-centered
    sphere.bytecode = { in };
    ASSERT_EQ(reg.Register(1u, sphere), Vixen::SVO::RecipeRegistry::RegisterResult::Ok);

    Vixen::SVO::RecipeBakeConfig cfg{};  // defaults: center=(32,32,32), n=64, band=2.5, depth=3
    Vixen::SVO::RecipeBakeResult baked = Vixen::SVO::BakeRegistryToPool(reg, cfg);
    ASSERT_TRUE(baked.ok) << baked.err;
    ASSERT_GT(baked.pool.mipPool.size(), 0u)
        << "BakeRegistryToPool must bake+attach mips for its callers (Task 1)";

    RenderStats stats;
    ASSERT_NO_FATAL_FAILURE(RenderPoolAndMeasure(
        std::move(baked.pool), /*residencyRequested=*/false,
        "/tmp/mip_fallback_registry_baked.png", stats));

    EXPECT_GT(stats.hitPixels, 5000)
        << "Registry-baked, non-resident tree should render a non-trivial silhouette from mip samples alone";
    const int centerBandRows = int(512 * 0.10f);
    EXPECT_GT(stats.centerColBandHits, int(512 * 0.5f))
        << "Center column band should be substantially covered by a centered sphere";
    EXPECT_LT(stats.edgeColBandHits, centerBandRows / 4)
        << "Edge column band should be mostly empty (sky) for a round, centered silhouette";
}

// ---------------------------------------------------------------------------
// Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4b gate: a body on octree index
// >=1 in a multi-octree, all-mip-capable pool must render CORRECTLY both
// before AND after a post-Compile residency grant. Before M2 Task 4b's fix,
// PollBrickUploadCompletion's phase-2 config re-upload clobbered
// CreateShellBuffers' shell-compact poolBrickBase rewrite with the SOURCE
// pool's poolBrickBase for octree 1, corrupting SDF channelPool addressing
// the instant residency was granted -- this test's "after" render is the one
// that would have broken.
// ---------------------------------------------------------------------------
TEST_F(MipFallbackRenderTest, MultiOctreeSecondBodyRendersCorrectlyAfterResidencyGrant) {
    ASSERT_TRUE(deviceConfirmed_);

    RenderStats before, after;
    ASSERT_NO_FATAL_FAILURE(RenderMultiOctreePostGrantAndMeasure(
        /*octreeCount=*/2u, /*targetOctreeIndex=*/1u, before, after));

    const int centerBandRows = int(512 * 0.10f);

    EXPECT_GT(before.hitPixels, 5000)
        << "Boot (mip-only, octree index 1) should render a non-trivial silhouette";
    EXPECT_GT(before.centerColBandHits, int(512 * 0.5f));
    EXPECT_LT(before.edgeColBandHits, centerBandRows / 4);

    EXPECT_GT(after.hitPixels, 5000)
        << "Post-grant (real brick march, octree index 1) must ALSO render a non-trivial "
           "silhouette -- a wrong config buffer would corrupt addressing and blank/garble this";
    EXPECT_GT(after.centerColBandHits, int(512 * 0.5f))
        << "Post-grant silhouette must still be round/centered, not corrupted by a "
           "source-vs-compact poolBrickBase mismatch";
    EXPECT_LT(after.edgeColBandHits, centerBandRows / 4);

    // Same coarseness-vs-exact-march tolerance as ResidentTreeRendersComparableSilhouette
    // above (mip fallback is a hard-switch coverage test, not a true iso-surface march).
    const double ratio = double(before.hitPixels) / double(after.hitPixels);
    EXPECT_GT(ratio, 0.5) << "Post-grant silhouette is suspiciously smaller than boot";
    EXPECT_LT(ratio, 6.0) << "Post-grant silhouette is implausibly larger than boot "
                             "(possible addressing corruption)";
}
