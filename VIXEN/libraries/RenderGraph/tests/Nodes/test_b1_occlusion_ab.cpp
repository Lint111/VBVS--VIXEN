/**
 * @file test_b1_occlusion_ab.cpp
 * @brief Raster-proxy B1/B2 A/B gate: runs baseline -> B1 -> B1+B2 on one
 *        synthetic scene and proves exclusion, effective-iteration quality, and
 *        byte-identical visible output.
 *
 * Scene: one large occluder body directly in front of the camera, >=3 small
 * bodies fully behind it (same screen-space footprint, farther along view),
 * and one control body off to the side (never occluded). All Stored/ESVO
 * (providerKind=0) on octreeIndex 0, sharing the reject test's collinear/
 * off-axis camera-ray construction so the shader's ESVO traversal is exercised
 * exactly as test_body_instance_occlusion_reject.cpp already proved it is.
 *
 * Pipeline (mirrors the real B1 frame order — see docs/superpowers B1 plan):
 *   1. Pass A: march with an all-zero skip mask (binding 35), B1-ON SPV
 *      (binding 36 depthDistanceImage populated). Read back per-instance
 *      iterCounts (binding 14) + RGBA.
 *   2. HiZDownsample.comp: reduce the depth image into a tile-max image.
 *   3. InstanceOcclusionCull.comp: per-instance HiZ cull, writing the B1
 *      camera-visibility region of the split skip mask (binding 35 for pass B).
 *   4. Pass B: march again with the cull-produced mask. Read back iterCounts
 *      + RGBA.
 *
 * Asserts: exclusion bits correct, camera-culled instances still cast a real
 * shadow ray, skipped instances' pass-B iterCounts==0,
 * >=40% total-iteration reduction, byte-identical RGBA (static scene, so
 * culled instances contributed no visible pixels), and the produced mask
 * cross-checked against the CPU mirror (InstanceOcclusionCullMirror.h) fed
 * from the REAL config buffer read back off the device — proving the GPU
 * chain matches the CPU mirror end-to-end, not just a hand-typed expectation.
 *
 * DEVICE SELECTION: identical contract to test_body_instance_occlusion_reject.cpp
 * (real GPU preferred, lavapipe/Dozen fallback, some usable device hard-asserted).
 *
 * Run: ./test_b1_occlusion_ab
 */

#include <gtest/gtest.h>

#include "Headers.h"  // MUST be first: defines GLM_FORCE_DEPTH_ZERO_TO_ONE (mirrors CameraNode.cpp)

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu, Vixen::SVO::OctreeConfig
#include "Generated/OctreeConfig.g.h"          // Vixen::Gpu::OctreeConfig (432 B, static_asserted)
#include "Nodes/InstanceOcclusionCullMirror.h" // CPU mirror under end-to-end parity check
#include "Nodes/ProxyIntervalPrepassMirror.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;
using Vixen::RenderGraph::Mirror::CullInstance;
using Vixen::RenderGraph::Mirror::CullMaskWord;
using Vixen::RenderGraph::Mirror::CullOctreeConfig;
using Vixen::RenderGraph::Mirror::CullParams;
using Vixen::RenderGraph::Mirror::HiZTileCount;

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch_b1.spv) must be defined by CMake"
#endif
#ifndef GLSL_RAYMARCH_B2_SPV
#error "GLSL_RAYMARCH_B2_SPV (path to compiled BodyInstanceRayMarch_b2.spv) must be defined by CMake"
#endif
#ifndef HIZ_DOWNSAMPLE_SPV
#error "HIZ_DOWNSAMPLE_SPV (path to compiled HiZDownsample.spv) must be defined by CMake"
#endif
#ifndef INSTANCE_OCCLUSION_CULL_SPV
#error "INSTANCE_OCCLUSION_CULL_SPV (path to compiled InstanceOcclusionCull.spv) must be defined by CMake"
#endif
#ifndef SHADOW_RAY_TRACE_SPV
#error "SHADOW_RAY_TRACE_SPV (path to compiled ShadowRayTrace.spv) must be defined by CMake"
#endif

namespace {

// Byte-identical to BodyInstanceRayMarch.comp's PushConstants block (see
// test_body_instance_occlusion_reject.cpp's identical copy for the layout
// derivation and the M2/accum-frame history that grew it to 96 bytes).
struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;       // DEGREES
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float   raySizeCoef;
    float   raySizeBias;
    int32_t instanceCount;
    int32_t _pad0;
    glm::ivec2 debugTargetPixel = glm::ivec2(-1, -1);
    uint32_t   accumFrameCount = 1u;
    uint32_t   _pad1 = 0u;
};
static_assert(sizeof(PushConstants) == 96, "PushConstants must be 96 bytes");

// VIXEN_B2_PROXY_PREPASS appends proxyAabbCount at byte 96. Pad the test-side
// block to the next 16-byte boundary while keeping the shader-visible prefix
// byte-identical to the production push gatherer.
struct PushConstantsB2 {
    PushConstants base;
    uint32_t proxyAabbCount;
    uint32_t _pad[3]{};
};
static_assert(sizeof(PushConstantsB2) == 112, "B2 PushConstants must be 112 bytes");

// Byte-identical to HiZDownsample.comp's push_constant block.
struct HiZPush {
    uint32_t srcWidth;
    uint32_t srcHeight;
};
static_assert(sizeof(HiZPush) == 8, "HiZPush must be 8 bytes");

// Byte-identical to InstanceOcclusionCull.comp's push_constant block.
struct CullPush {
    glm::mat4 prevViewProj;
    glm::vec4 prevCamPos;
    uint32_t dims[4];  // srcWidth, srcHeight, instanceCount, pad
};
static_assert(sizeof(CullPush) == 96, "CullPush must be 96 bytes");

// Byte-identical to ShadowRayQueue.glsl's ShadowRayRequest.
struct ShadowRayRequestCpu {
    glm::vec3 origin; float tmin;
    glm::vec3 dir;    float tmax;
};
static_assert(sizeof(ShadowRayRequestCpu) == 32, "ShadowRayRequestCpu must be 32 bytes");

// M2c fix (see test_body_instance_raymarch_render.cpp's identical comment): the
// march shader stopped writing colorImg/binding 0 once shading moved to
// DirectLighting.comp — "same visible output" must be judged from HitRecordBuffer
// (binding 18, the buffer this shader actually still writes), not colorImg.
struct HitRecordCpu {
    float albedo[3];
    float roughness;
    float worldNormal[3];
    float hitT;
    float worldPos[3];
    uint32_t flags;
    uint32_t _pad0[4];
};
static_assert(sizeof(HitRecordCpu) == 64, "HitRecordCpu std430 mirror size");
constexpr uint32_t kHitRecordFlagHit = 0x1u;

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize sz = f.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(code.data()), sz);
    return code;
}

constexpr uint32_t kSkipMaskWords =
    2u * Vixen::RenderGraph::Mirror::kInstanceMaskWordCount;
constexpr uint32_t kCameraMaskWordBase =
    Vixen::RenderGraph::Mirror::kCameraVisibilityMaskWordBase;

}  // namespace

class B1OcclusionAbTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             softwareConfirmed_ = false;

    std::unique_ptr<VulkanDevice> deviceShell_;

    static bool IsRealGpu(const VkPhysicalDeviceProperties& props) {
        return props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
               props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    static bool LooksLikeSoftware(const VkPhysicalDeviceProperties& props) {
        std::string name(props.deviceName);
        for (char& c : name) c = static_cast<char>(::tolower(c));
        const bool isSoftware =
            (name.find("llvmpipe") != std::string::npos ||
             name.find("lavapipe") != std::string::npos) &&
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool isDozen = name.find("direct3d12") != std::string::npos;
        return isSoftware || isDozen;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo appInfo{};
        appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test_b1_occlusion_ab";
        appInfo.apiVersion       = VK_API_VERSION_1_3;

        const auto  enabledLayers = EnabledValidationLayers();
        const char* extensions[]  = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

        VkInstanceCreateInfo instInfo{};
        instInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo        = &appInfo;
        instInfo.enabledLayerCount       = static_cast<uint32_t>(enabledLayers.size());
        instInfo.ppEnabledLayerNames     = enabledLayers.empty() ? nullptr : enabledLayers.data();
        instInfo.enabledExtensionCount   = 1;
        instInfo.ppEnabledExtensionNames = extensions;

        ASSERT_EQ(vkCreateInstance(&instInfo, nullptr, &instance_), VK_SUCCESS)
            << "vkCreateInstance failed — is a Vulkan device available?";

        ASSERT_NO_FATAL_FAILURE(PickSoftwarePhysicalDevice());
        ASSERT_TRUE(softwareConfirmed_)
            << "Refusing to run: no usable Vulkan device found (real GPU, software "
               "rasterizer, or Dozen); nearest was '" << selectedDeviceName_ << "'.";

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());

        deviceShell_ = std::make_unique<VulkanDevice>(&physicalDevice_);
        deviceShell_->device = logicalDevice_;
    }

    void TearDown() override {
        if (deviceShell_) { deviceShell_->device = VK_NULL_HANDLE; deviceShell_.reset(); }
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) { vkDestroyDevice(logicalDevice_, nullptr); logicalDevice_ = VK_NULL_HANDLE; }
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
    }

    void PickSoftwarePhysicalDevice() {
        uint32_t count = 0;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, nullptr), VK_SUCCESS);
        ASSERT_GT(count, 0u) << "No Vulkan physical devices visible.";
        std::vector<VkPhysicalDevice> devices(count);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), VK_SUCCESS);
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (IsRealGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                softwareConfirmed_ = true; return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (LooksLikeSoftware(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                softwareConfirmed_ = true; return;
            }
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[0], &props);
        selectedDeviceName_ = props.deviceName;
        softwareConfirmed_  = false;
    }

    void CreateLogicalDevice() {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
        ASSERT_GT(qfCount, 0u);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qfs.data());
        bool found = false;
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found) << "No compute queue family on the software device";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_; qInfo.queueCount = 1; qInfo.pQueuePriorities = &priority;

        VkPhysicalDeviceFeatures features{};
        features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1; dInfo.pQueueCreateInfos = &qInfo;
        dInfo.pEnabledFeatures = &features;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &dInfo, nullptr, &logicalDevice_), VK_SUCCESS);
        vkGetDeviceQueue(logicalDevice_, queueFamily_, 0, &queue_);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(logicalDevice_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
    }

    template<typename T>
    static void SetHandleVal(Resource& res, T value) { res.SetHandle<T>(std::move(value)); }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags required) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & required) == required) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    void CreateImage(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags extraUsage,
                     VkImage& outImage, VkDeviceMemory& outMem) {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {w, h, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | extraUsage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ci, nullptr, &outImage), VK_SUCCESS);

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(logicalDevice_, outImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, outImage, outMem, 0), VK_SUCCESS);
    }

    VkImageView CreateView(VkImage image, VkFormat format) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        EXPECT_EQ(vkCreateImageView(logicalDevice_, &vi, nullptr, &view), VK_SUCCESS);
        return view;
    }

    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& outBuf, VkDeviceMemory& outMem, bool zero) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ASSERT_EQ(vkCreateBuffer(logicalDevice_, &bi, nullptr, &outBuf), VK_SUCCESS);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(logicalDevice_, outBuf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &outMem), VK_SUCCESS);
        ASSERT_EQ(vkBindBufferMemory(logicalDevice_, outBuf, outMem, 0), VK_SUCCESS);
        if (zero) {
            void* m = nullptr;
            ASSERT_EQ(vkMapMemory(logicalDevice_, outMem, 0, size, 0, &m), VK_SUCCESS);
            std::memset(m, 0, static_cast<size_t>(size));
            vkUnmapMemory(logicalDevice_, outMem);
        }
    }

    void UploadBuffer(VkDeviceMemory mem, const void* data, size_t size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memcpy(m, data, size);
        vkUnmapMemory(logicalDevice_, mem);
    }

    // One-shot command-buffer submit-and-wait, shared by every dispatch step below.
    void SubmitAndWait(VkCommandBuffer cmd) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: software device not confirmed; refusing vkQueueSubmit.";
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);
    }

    VkCommandBuffer BeginOneShot() {
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        EXPECT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        EXPECT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);
        return cmd;
    }

    // ------------------------------------------------------------------
    // Pass A/B march: byte-identical binding layout to
    // test_body_instance_raymarch_render.cpp (23 bindings: 0-5, 8-22, 35, 36) —
    // that render test's SPV IS body_instance_raymarch_spv_b1, the same
    // B1-ON variant this test links against (binding 36 depthDistanceImage
    // is #ifdef VIXEN_B1_OCCLUSION_CULL-gated in the shader, so any caller
    // of this SPV must bind exactly this superset or vkCreateComputePipelines
    // rejects the layout).
    // ------------------------------------------------------------------
    void March(VkBuffer nodesBuf, VkBuffer bricksBuf, VkBuffer materialsBuf,
              VkBuffer configBuf, VkBuffer instanceBuf,
              VkBuffer skipMaskBuf, VkDeviceSize skipMaskBufSize,
              VkImage depthImg, VkImageView depthView, bool clearDepth,
              const PushConstants& pc, uint32_t w, uint32_t h,
              uint32_t maxInstances,
              std::vector<uint32_t>& outIterCounts,
              std::vector<HitRecordCpu>& outHitRecords,
              const char* spirvPath = GLSL_RAYMARCH_SPV,
              VkBuffer proxyIntervalBuf = VK_NULL_HANDLE,
              uint32_t proxyAabbCount = 0u) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not the software rasterizer; refusing to submit.";
        const bool b2Enabled = proxyIntervalBuf != VK_NULL_HANDLE;

        constexpr VkDeviceSize kRayTraceBufferSize = 16 + 256 * (16 + 64 * 48);
        VkBuffer traceBuf = VK_NULL_HANDLE, counterBuf = VK_NULL_HANDLE;
        VkDeviceMemory traceMem = VK_NULL_HANDLE, counterMem = VK_NULL_HANDLE;
        CreateHostBuffer(kRayTraceBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, counterBuf, counterMem, true);

        VkBuffer dummySdf = VK_NULL_HANDLE, dummyLookup = VK_NULL_HANDLE, dummyMip = VK_NULL_HANDLE;
        VkDeviceMemory dummySdfMem = VK_NULL_HANDLE, dummyLookupMem = VK_NULL_HANDLE, dummyMipMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummySdf, dummySdfMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyLookup, dummyLookupMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyMip, dummyMipMem, true);

        const VkDeviceSize iterBufSize = static_cast<VkDeviceSize>(maxInstances) * sizeof(uint32_t);
        VkBuffer iterBuf = VK_NULL_HANDLE; VkDeviceMemory iterMem = VK_NULL_HANDLE;
        CreateHostBuffer(iterBufSize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         iterBuf, iterMem, /*zero=*/true);

        VkBuffer dummyTierRef = VK_NULL_HANDLE, dummyOccGrid = VK_NULL_HANDLE;
        VkDeviceMemory dummyTierRefMem = VK_NULL_HANDLE, dummyOccGridMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyTierRef, dummyTierRefMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyOccGrid, dummyOccGridMem, true);

        VkBuffer dummyLighting = VK_NULL_HANDLE, hitRecordBuf = VK_NULL_HANDLE,
                 dummyShadow = VK_NULL_HANDLE, dummyAccum = VK_NULL_HANDLE, dummyPrevCam = VK_NULL_HANDLE;
        VkDeviceMemory dummyLightingMem = VK_NULL_HANDLE, hitRecordMem = VK_NULL_HANDLE,
                       dummyShadowMem = VK_NULL_HANDLE, dummyAccumMem = VK_NULL_HANDLE, dummyPrevCamMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyLighting, dummyLightingMem, true);
        const VkDeviceSize hitRecordBufSize = VkDeviceSize(w) * VkDeviceSize(h) * 64;
        CreateHostBuffer(hitRecordBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hitRecordBuf, hitRecordMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyShadow, dummyShadowMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyAccum, dummyAccumMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, dummyPrevCam, dummyPrevCamMem, true);

        const VkFormat kColorFmt = VK_FORMAT_R8G8B8A8_UNORM;
        const VkFormat kIdFmt    = VK_FORMAT_R32_UINT;
        VkImage colorImg = VK_NULL_HANDLE, idImg = VK_NULL_HANDLE, historyImg = VK_NULL_HANDLE;
        VkDeviceMemory colorMem = VK_NULL_HANDLE, idMem = VK_NULL_HANDLE, historyMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kIdFmt, 0, idImg, idMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, 0, historyImg, historyMem));
        VkImageView colorView   = CreateView(colorImg, kColorFmt);
        VkImageView idView      = CreateView(idImg, kIdFmt);
        VkImageView historyView = CreateView(historyImg, kColorFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE);
        ASSERT_NE(idView, VK_NULL_HANDLE);
        ASSERT_NE(historyView, VK_NULL_HANDLE);

        const std::vector<uint32_t> spirv = ReadSpirv(spirvPath);
        ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << spirvPath;
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t); smci.pCode = spirv.data();
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shaderModule), VK_SUCCESS);

        auto bind = [](uint32_t b, VkDescriptorType t) {
            VkDescriptorSetLayoutBinding lb{};
            lb.binding = b; lb.descriptorType = t; lb.descriptorCount = 1;
            lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return lb;
        };
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            bind(0,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(1,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(2,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(3,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(4,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(5,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(8,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(9,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(21, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            bind(22, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
            bind(36, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };
        if (b2Enabled) {
            bindings.push_back(bind(42, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
        }
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size()); dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0;
        pcr.size = b2Enabled ? sizeof(PushConstantsB2) : sizeof(PushConstants);
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shaderModule; cpci.stage.pName = "main";
        cpci.layout = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  4},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, b2Enabled ? 20u : 19u},
        }};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size()); dpci.pPoolSizes = poolSizes.data();
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &descPool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = descPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &descSet), VK_SUCCESS);

        VkDescriptorImageInfo colorInfo{VK_NULL_HANDLE, colorView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo idInfo{VK_NULL_HANDLE, idView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo historyInfo{VK_NULL_HANDLE, historyView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo depthInfo{VK_NULL_HANDLE, depthView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo nodesInfo{nodesBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo bricksInfo{bricksBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo matsInfo{materialsBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo traceInfo{traceBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo configInfo{configBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo counterInfo{counterBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo instInfo{instanceBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo sdfInfo{dummySdf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo lookupInfo{dummyLookup, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo mipInfo{dummyMip, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo iterInfo{iterBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo tierRefInfo{dummyTierRef, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo occGridInfo{dummyOccGrid, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo lightingInfo{dummyLighting, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo hitRecordInfo{hitRecordBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo shadowInfo{dummyShadow, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo accumInfo{dummyAccum, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo prevCamInfo{dummyPrevCam, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo skipMaskInfo{skipMaskBuf, 0, skipMaskBufSize};
        VkDescriptorBufferInfo proxyIntervalInfo{proxyIntervalBuf, 0, VK_WHOLE_SIZE};

        auto wImg = [&](uint32_t b, VkDescriptorImageInfo* info) {
            VkWriteDescriptorSet w2{};
            w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w2.dstSet = descSet; w2.dstBinding = b; w2.descriptorCount = 1;
            w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w2.pImageInfo = info;
            return w2;
        };
        auto wBuf = [&](uint32_t b, VkDescriptorType t, VkDescriptorBufferInfo* info) {
            VkWriteDescriptorSet w2{};
            w2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w2.dstSet = descSet; w2.dstBinding = b; w2.descriptorCount = 1;
            w2.descriptorType = t; w2.pBufferInfo = info;
            return w2;
        };
        std::vector<VkWriteDescriptorSet> writes = {
            wImg(0, &colorInfo),
            wBuf(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &nodesInfo),
            wBuf(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bricksInfo),
            wBuf(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &matsInfo),
            wBuf(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &traceInfo),
            wBuf(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &configInfo),
            wBuf(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &counterInfo),
            wImg(9, &idInfo),
            wBuf(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instInfo),
            wBuf(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &sdfInfo),
            wBuf(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lookupInfo),
            wBuf(13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &mipInfo),
            wBuf(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &iterInfo),
            wBuf(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &tierRefInfo),
            wBuf(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &occGridInfo),
            wBuf(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lightingInfo),
            wBuf(18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hitRecordInfo),
            wBuf(19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &shadowInfo),
            wBuf(20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &accumInfo),
            wImg(21, &historyInfo),
            wBuf(22, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &prevCamInfo),
            wBuf(35, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &skipMaskInfo),
            wImg(36, &depthInfo),
        };
        if (b2Enabled) {
            writes.push_back(wBuf(42, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  &proxyIntervalInfo));
        }
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);

        VkCommandBuffer cmd = BeginOneShot();

        auto barrierToGeneral = [&](VkImage img) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        barrierToGeneral(colorImg);
        barrierToGeneral(idImg);
        barrierToGeneral(historyImg);
        // Pass A transitions the depth image fresh; pass B's depth image is already
        // GENERAL (holding pass A's real distances) and must NOT be re-transitioned
        // from UNDEFINED (that would discard the content the cull step just consumed).
        if (clearDepth) {
            barrierToGeneral(depthImg);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
        if (b2Enabled) {
            PushConstantsB2 b2Push{};
            b2Push.base = pc;
            b2Push.proxyAabbCount = proxyAabbCount;
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(b2Push), &b2Push);
        } else {
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(pc), &pc);
        }
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        // Barrier the iteration debug SSBO + HitRecord SSBO (shader write -> host read)
        // before copying anything out. colorImg/binding 0 stays bound (the shader still
        // declares it) but is NEVER read back — the M2c fix (see HitRecordCpu's own
        // comment above) permanently stopped this shader from writing it once shading
        // moved to DirectLighting.comp; "what did pass A/B actually render" must be
        // judged from HitRecordBuffer (binding 18, still real per-pixel output).
        VkBufferMemoryBarrier iterBarrier{};
        iterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        iterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        iterBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        iterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        iterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        iterBarrier.buffer = iterBuf; iterBarrier.offset = 0; iterBarrier.size = VK_WHOLE_SIZE;
        VkBufferMemoryBarrier hitRecordBarrier = iterBarrier;
        hitRecordBarrier.buffer = hitRecordBuf;
        const std::array<VkBufferMemoryBarrier, 2> toHostBarriers = {iterBarrier, hitRecordBarrier};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(toHostBarriers.size()),
                             toHostBarriers.data(), 0, nullptr);

        // Leave the depth image in GENERAL (not TRANSFER_SRC) — the HiZ downsample step
        // right after this call reads it as a storage image, and pass B's march reads/
        // writes it again; no host readback of the depth image is needed by this test.
        VkImageMemoryBarrier depthToGeneralRead{};
        depthToGeneralRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthToGeneralRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL; depthToGeneralRead.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        depthToGeneralRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; depthToGeneralRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToGeneralRead.image = depthImg; depthToGeneralRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        depthToGeneralRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        depthToGeneralRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &depthToGeneralRead);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));

        void* iterMapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, iterMem, 0, iterBufSize, 0, &iterMapped), VK_SUCCESS);
        outIterCounts.assign(maxInstances, 0u);
        std::memcpy(outIterCounts.data(), iterMapped, static_cast<size_t>(iterBufSize));
        vkUnmapMemory(logicalDevice_, iterMem);

        void* hitRecordMapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, hitRecordMem, 0, hitRecordBufSize, 0, &hitRecordMapped), VK_SUCCESS);
        outHitRecords.assign(static_cast<size_t>(w) * h, HitRecordCpu{});
        std::memcpy(outHitRecords.data(), hitRecordMapped, static_cast<size_t>(hitRecordBufSize));
        vkUnmapMemory(logicalDevice_, hitRecordMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImageView(logicalDevice_, idView, nullptr);
        vkDestroyImageView(logicalDevice_, historyView, nullptr);
        vkDestroyImage(logicalDevice_, colorImg, nullptr);   vkFreeMemory(logicalDevice_, colorMem, nullptr);
        vkDestroyImage(logicalDevice_, idImg, nullptr);      vkFreeMemory(logicalDevice_, idMem, nullptr);
        vkDestroyImage(logicalDevice_, historyImg, nullptr); vkFreeMemory(logicalDevice_, historyMem, nullptr);
        vkDestroyBuffer(logicalDevice_, traceBuf, nullptr);   vkFreeMemory(logicalDevice_, traceMem, nullptr);
        vkDestroyBuffer(logicalDevice_, counterBuf, nullptr); vkFreeMemory(logicalDevice_, counterMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummySdf, nullptr);    vkFreeMemory(logicalDevice_, dummySdfMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyLookup, nullptr); vkFreeMemory(logicalDevice_, dummyLookupMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyMip, nullptr);    vkFreeMemory(logicalDevice_, dummyMipMem, nullptr);
        vkDestroyBuffer(logicalDevice_, iterBuf, nullptr);     vkFreeMemory(logicalDevice_, iterMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyTierRef, nullptr); vkFreeMemory(logicalDevice_, dummyTierRefMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyOccGrid, nullptr); vkFreeMemory(logicalDevice_, dummyOccGridMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyLighting, nullptr);  vkFreeMemory(logicalDevice_, dummyLightingMem, nullptr);
        vkDestroyBuffer(logicalDevice_, hitRecordBuf, nullptr); vkFreeMemory(logicalDevice_, hitRecordMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyShadow, nullptr);    vkFreeMemory(logicalDevice_, dummyShadowMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyAccum, nullptr);     vkFreeMemory(logicalDevice_, dummyAccumMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyPrevCam, nullptr);   vkFreeMemory(logicalDevice_, dummyPrevCamMem, nullptr);
    }

    // ------------------------------------------------------------------
    // HiZDownsample.comp dispatch: reduces depthImg (w*h R32F) into
    // tileImg (ceil(w/16)*ceil(h/16) R32F). Bindings 0/1 self-contained
    // (no SceneBindings.glsl namespace, per the shader's own header).
    // ------------------------------------------------------------------
    void DispatchHiZDownsample(VkImageView depthView, VkImageView tileView,
                               uint32_t srcW, uint32_t srcH,
                               uint32_t tilesX, uint32_t tilesY) {
        const std::vector<uint32_t> spirv = ReadSpirv(HIZ_DOWNSAMPLE_SPV);
        ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << HIZ_DOWNSAMPLE_SPV;
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = spirv.size() * sizeof(uint32_t); smci.pCode = spirv.data();
        VkShaderModule shader = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shader), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[2] = {};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding = i; bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 2; dslci.pBindings = bindings;
        VkDescriptorSetLayout dsl{};
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(HiZPush);
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
        VkPipelineLayout pipelineLayout{};
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shader; cpci.stage.pName = "main";
        cpci.layout = pipelineLayout;
        VkPipeline pipeline{};
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
        VkDescriptorPool pool{};
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &pool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
        VkDescriptorSet set{};
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &set), VK_SUCCESS);

        VkDescriptorImageInfo srcInfo{VK_NULL_HANDLE, depthView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo dstInfo{VK_NULL_HANDLE, tileView, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[0].pImageInfo = &srcInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[1].pImageInfo = &dstInfo;
        vkUpdateDescriptorSets(logicalDevice_, 2, writes, 0, nullptr);

        VkCommandBuffer cmd = BeginOneShot();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
        HiZPush push{srcW, srcH};
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (tilesX + 7) / 8, (tilesY + 7) / 8, 1);

        // Barrier: HiZ write -> cull read (next step reads tileMaxImage as a storage image).
        VkMemoryBarrier toRead{};
        toRead.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &toRead, 0, nullptr, 0, nullptr);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyDescriptorPool(logicalDevice_, pool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shader, nullptr);
    }

    // ------------------------------------------------------------------
    // InstanceOcclusionCull.comp dispatch: same descriptor/push layout as
    // test_instance_occlusion_cull_device.cpp's proven working dispatch.
    // ------------------------------------------------------------------
    void DispatchInstanceOcclusionCull(VkBuffer instBuf, VkBuffer cfgBuf, VkImageView tileView,
                                       VkBuffer maskBuf, VkDeviceSize maskBufSize,
                                       const CullPush& push) {
        auto code = ReadSpirv(INSTANCE_OCCLUSION_CULL_SPV);
        ASSERT_FALSE(code.empty()) << "missing SPIR-V at " INSTANCE_OCCLUSION_CULL_SPV;
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = code.size() * 4; smci.pCode = code.data();
        VkShaderModule shader{};
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shader), VK_SUCCESS);

        VkDescriptorSetLayoutBinding bindings[4] = {};
        for (uint32_t i = 0; i < 4; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = (i == 2) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 4; dslci.pBindings = bindings;
        VkDescriptorSetLayout dsl{};
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(CullPush);
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
        VkPipelineLayout pipelineLayout{};
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shader; cpci.stage.pName = "main";
        cpci.layout = pipelineLayout;
        VkPipeline pipeline{};
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline), VK_SUCCESS);

        VkDescriptorPoolSize poolSizes[2] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        };
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = 2; dpci.pPoolSizes = poolSizes;
        VkDescriptorPool pool{};
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &pool), VK_SUCCESS);

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
        VkDescriptorSet set{};
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &set), VK_SUCCESS);

        VkDescriptorBufferInfo instInfo{instBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo cfgInfo{cfgBuf, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo maskInfo{maskBuf, 0, maskBufSize};
        VkDescriptorImageInfo tileInfo{VK_NULL_HANDLE, tileView, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[4] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].pBufferInfo = &instInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &cfgInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = set; writes[2].dstBinding = 2; writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[2].pImageInfo = &tileInfo;
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = set; writes[3].dstBinding = 3; writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[3].pBufferInfo = &maskInfo;
        vkUpdateDescriptorSets(logicalDevice_, 4, writes, 0, nullptr);

        VkCommandBuffer cmd = BeginOneShot();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, 1, 1, 1);  // 64 threads cover the six B1 camera words

        VkMemoryBarrier toHost{};
        toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &toHost, 0, nullptr, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, pool, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shader, nullptr);
    }

    // Dispatch one real ShadowRayTrace request against the same ESVO instance buffer and
    // split skip-mask buffer used by the B1 chain. One request/one invocation keeps the
    // regression deterministic and avoids the binding-14 multi-writer race documented by
    // the per-pixel iteration readback below.
    void DispatchShadowRay(VkBuffer nodesBuf, VkBuffer bricksBuf, VkBuffer materialsBuf,
                           VkBuffer configBuf, VkBuffer instanceBuf,
                           VkBuffer skipMaskBuf, VkDeviceSize skipMaskBufSize,
                           const PushConstants& push, const ShadowRayRequestCpu& request,
                           uint32_t& outVisible) {
        auto code = ReadSpirv(SHADOW_RAY_TRACE_SPV);
        ASSERT_FALSE(code.empty()) << "missing SPIR-V at " SHADOW_RAY_TRACE_SPV;
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = code.size() * sizeof(uint32_t); smci.pCode = code.data();
        VkShaderModule shader = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateShaderModule(logicalDevice_, &smci, nullptr, &shader), VK_SUCCESS);

        VkBuffer rayTraceBuf = VK_NULL_HANDLE, channelPoolBuf = VK_NULL_HANDLE;
        VkBuffer brickLookupBuf = VK_NULL_HANDLE, mipPoolBuf = VK_NULL_HANDLE;
        VkBuffer tierRefBuf = VK_NULL_HANDLE, requestBuf = VK_NULL_HANDLE, resultBuf = VK_NULL_HANDLE;
        VkDeviceMemory rayTraceMem = VK_NULL_HANDLE, channelPoolMem = VK_NULL_HANDLE;
        VkDeviceMemory brickLookupMem = VK_NULL_HANDLE, mipPoolMem = VK_NULL_HANDLE;
        VkDeviceMemory tierRefMem = VK_NULL_HANDLE, requestMem = VK_NULL_HANDLE, resultMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, rayTraceBuf, rayTraceMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, channelPoolBuf, channelPoolMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, brickLookupBuf, brickLookupMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mipPoolBuf, mipPoolMem, true);
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, tierRefBuf, tierRefMem, true);
        CreateHostBuffer(sizeof(request), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         requestBuf, requestMem, false);
        CreateHostBuffer(sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         resultBuf, resultMem, true);
        UploadBuffer(requestMem, &request, sizeof(request));

        auto bind = [](uint32_t binding) {
            VkDescriptorSetLayoutBinding out{};
            out.binding = binding;
            out.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            out.descriptorCount = 1;
            out.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            return out;
        };
        const std::array<VkDescriptorSetLayoutBinding, 13> bindings = {{
            bind(1), bind(2), bind(3), bind(4), bind(5), bind(10), bind(11),
            bind(12), bind(13), bind(15), bind(35), bind(37), bind(38),
        }};
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size());
        dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0; pcr.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreatePipelineLayout(logicalDevice_, &plci, nullptr, &pipelineLayout), VK_SUCCESS);

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = shader; cpci.stage.pName = "main";
        cpci.layout = pipelineLayout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateComputePipelines(logicalDevice_, VK_NULL_HANDLE, 1, &cpci,
                                           nullptr, &pipeline), VK_SUCCESS);

        const std::array<VkDescriptorPoolSize, 1> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(bindings.size())},
        }};
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = poolSizes.data();
        VkDescriptorPool pool = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorPool(logicalDevice_, &dpci, nullptr, &pool), VK_SUCCESS);
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = pool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &dsl;
        VkDescriptorSet set = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateDescriptorSets(logicalDevice_, &dsai, &set), VK_SUCCESS);

        const std::array<VkDescriptorBufferInfo, 13> infos = {{
            {nodesBuf, 0, VK_WHOLE_SIZE},
            {bricksBuf, 0, VK_WHOLE_SIZE},
            {materialsBuf, 0, VK_WHOLE_SIZE},
            {rayTraceBuf, 0, VK_WHOLE_SIZE},
            {configBuf, 0, VK_WHOLE_SIZE},
            {instanceBuf, 0, VK_WHOLE_SIZE},
            {channelPoolBuf, 0, VK_WHOLE_SIZE},
            {brickLookupBuf, 0, VK_WHOLE_SIZE},
            {mipPoolBuf, 0, VK_WHOLE_SIZE},
            {tierRefBuf, 0, VK_WHOLE_SIZE},
            {skipMaskBuf, 0, skipMaskBufSize},
            {requestBuf, 0, VK_WHOLE_SIZE},
            {resultBuf, 0, VK_WHOLE_SIZE},
        }};
        std::array<VkWriteDescriptorSet, 13> writes{};
        for (size_t i = 0; i < writes.size(); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = bindings[i].binding;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);

        VkCommandBuffer cmd = BeginOneShot();
        VkMemoryBarrier requestReady{};
        requestReady.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        requestReady.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        requestReady.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &requestReady,
                             0, nullptr, 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                                0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(cmd, 1, 1, 1);
        VkMemoryBarrier resultReady{};
        resultReady.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        resultReady.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        resultReady.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &resultReady,
                             0, nullptr, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));

        uint32_t result = 0u;
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, resultMem, 0, sizeof(result), 0, &mapped), VK_SUCCESS);
        std::memcpy(&result, mapped, sizeof(result));
        vkUnmapMemory(logicalDevice_, resultMem);
        outVisible = result;

        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, pool, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shader, nullptr);
        vkDestroyBuffer(logicalDevice_, rayTraceBuf, nullptr); vkFreeMemory(logicalDevice_, rayTraceMem, nullptr);
        vkDestroyBuffer(logicalDevice_, channelPoolBuf, nullptr); vkFreeMemory(logicalDevice_, channelPoolMem, nullptr);
        vkDestroyBuffer(logicalDevice_, brickLookupBuf, nullptr); vkFreeMemory(logicalDevice_, brickLookupMem, nullptr);
        vkDestroyBuffer(logicalDevice_, mipPoolBuf, nullptr); vkFreeMemory(logicalDevice_, mipPoolMem, nullptr);
        vkDestroyBuffer(logicalDevice_, tierRefBuf, nullptr); vkFreeMemory(logicalDevice_, tierRefMem, nullptr);
        vkDestroyBuffer(logicalDevice_, requestBuf, nullptr); vkFreeMemory(logicalDevice_, requestMem, nullptr);
        vkDestroyBuffer(logicalDevice_, resultBuf, nullptr); vkFreeMemory(logicalDevice_, resultMem, nullptr);
    }

    // Device->host readback of an arbitrary SSBO via vkCmdCopyBuffer (same pattern as
    // test_shell_revalidate_node.cpp's shellFlagsBuf readback) — used to pull the REAL
    // OctreeConfig the march/cull shaders consumed, so the mirror cross-check proves
    // GPU==mirror end-to-end rather than assuming a hand-typed config.
    void ReadBackBuffer(VkBuffer srcBuf, VkDeviceSize size, void* outData) {
        VkBuffer rbBuf = VK_NULL_HANDLE; VkDeviceMemory rbMem = VK_NULL_HANDLE;
        CreateHostBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rbBuf, rbMem, false);
        VkCommandBuffer cmd = BeginOneShot();
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, srcBuf, rbBuf, 1, &region);
        VkBufferMemoryBarrier toHost{};
        toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer = rbBuf; toHost.offset = 0; toHost.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, 1, &toHost, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, rbMem, 0, size, 0, &mapped), VK_SUCCESS);
        std::memcpy(outData, mapped, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, rbMem);
        vkDestroyBuffer(logicalDevice_, rbBuf, nullptr);
        vkFreeMemory(logicalDevice_, rbMem, nullptr);
    }

    // Host->device write of an arbitrary SSBO via a staging buffer + vkCmdCopyBuffer --
    // the reverse of ReadBackBuffer. Used ONLY to patch OctreeConfig.traceBoundsMin/Max:
    // BodyOctreeSceneNode's real octree build leaves those two fields at the documented
    // memset(0) "no tighter bound" sentinel (ShellOctreeGpu.h) for a fresh synthetic
    // shell scene like this test's -- the MARCH shader tolerates that (getOctreeTraceBounds
    // falls back to the full [0,1]^3 cube, TraceWorld.glsl:109-113), but the CULL shader's
    // validBounds check has NO such fallback (InstanceOcclusionCull.comp:94-99) and
    // returns "never occluded" for every instance when bounds are degenerate. The brief's
    // own requirement ("valid traceBounds... max>min, within [0,1] -- the cull requires
    // validity") anticipates exactly this; test_instance_occlusion_cull_device.cpp's own
    // proven scene sidesteps it by hand-constructing its OctreeConfig from scratch instead
    // of building a real octree -- this test uses the real BodyOctreeSceneNode build (so
    // localToWorld/node & brick offsets are all genuine), then patches ONLY the two
    // trace-bounds fields post-build via this helper.
    void WriteBackBuffer(VkBuffer dstBuf, const void* data, VkDeviceSize size) {
        VkBuffer stagingBuf = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        CreateHostBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingBuf, stagingMem, false);
        UploadBuffer(stagingMem, data, static_cast<size_t>(size));
        VkCommandBuffer cmd = BeginOneShot();
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, stagingBuf, dstBuf, 1, &region);
        VkBufferMemoryBarrier toShaderRead{};
        toShaderRead.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.buffer = dstBuf; toShaderRead.offset = 0; toShaderRead.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &toShaderRead, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));
        vkDestroyBuffer(logicalDevice_, stagingBuf, nullptr);
        vkFreeMemory(logicalDevice_, stagingMem, nullptr);
    }
};

namespace {

constexpr float kBaseRadiusAu  = 0.05f;
constexpr float kWorldGridSize = 10.0f;

Vixen::SVO::BodyInstanceGpu MakeInstance(float x, float y, float z, float scale,
                                         uint32_t octreeIndex, float r, float g, float b) {
    Vixen::SVO::BodyInstanceGpu i{};
    i.worldPos[0] = x; i.worldPos[1] = y; i.worldPos[2] = z;
    i.renderScale = scale; i.octreeIndex = octreeIndex;
    i.color[0] = r; i.color[1] = g; i.color[2] = b;
    i.providerKind = 0u;  // Stored/ESVO — explicit, though value-init already defaults to 0.
    return i;
}

}  // namespace

// ---------------------------------------------------------------------------
// The B1 M4 decisive test: full march(A) -> HiZ -> cull -> march(B) chain on
// one occluder + 3 occluded + 1 control body.
// ---------------------------------------------------------------------------
TEST_F(B1OcclusionAbTest, OccludedInstancesDropIterationsAndPixelsStayIdentical) {
    std::cout << "[ b1-ab ] selected physical device: '" << selectedDeviceName_
              << "' (device confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_b1_occlusion_ab");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // Same off-axis collinear-ray direction as test_body_instance_occlusion_reject.cpp
    // (avoids an axis-aligned ESVO node-boundary edge case at dead-center).
    //
    // RACE HAZARD (found the hard way): InstanceIterDebugBuffer (binding 14) has ONE
    // uint32 slot per INSTANCE, written unconditionally by every thread/pixel that
    // processes that instance -- with no atomics, so the LAST thread to finish is
    // whichever value survives. A wide multi-pixel dispatch where a small body's
    // on-screen disc covers only a handful of the dispatch's total pixels means the
    // overwhelming majority of threads MISS that instance's AABB and write 0 --
    // whichever thread finishes last (miss-dominant statistically) clobbers any
    // earlier hit-thread's nonzero value. This is why an earlier 64x64/40deg attempt
    // at this same scene showed iterCount=0 for occ1-3 even though their AABBs
    // genuinely intersect the exact-center ray (verified independently via a raw
    // ray-AABB check and a HitRecordBuffer readback at that pixel).
    //
    // Fix: dispatch EXACTLY 2 pixels (a 2x1 image), each pixel's ray aimed EXACTLY at
    // one target direction -- pixel (0,0) down lineDir (the occluder stack), pixel
    // (1,0) at the control body -- so each instance's slot has EXACTLY ONE writer.
    // getRayDir(uv) derives ray directions from ONE shared camera basis via NDC
    // offset; with the camera aimed at the BISECTOR of the two target directions and
    // fov chosen so tan(fov/2) places pixel 0 at ndc.x=-0.5 and pixel 1 at ndc.x=+0.5
    // exactly on each target, both rays land EXACTLY where intended (derivation: half
    // the angle between lineDir and controlRayDir is 5deg here, so
    // tanHalfFov = 2*tan(5deg) => fov ~= 19.85deg; aspect=1, imgSize=(2,1) so ndc.y
    // stays 0 for both pixels, meaning cameraUp's own choice is irrelevant here).
    const glm::vec3 lineDir = glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f));
    const float scale = kBaseRadiusAu * 4.0f;
    const float R = 0.5f * kWorldGridSize * scale;  // ShaderBodyRadius: the ACTUAL rendered radius
    auto placeOnLine = [&](float t, float r, float g, float b) {
        const glm::vec3 p = lineDir * t - glm::vec3(R);  // offsets to the shader's rendered centre
        return MakeInstance(p.x, p.y, p.z, scale, 0, r, g, b);
    };
    const std::vector<Vixen::SVO::BodyInstanceGpu> unsorted = {
        placeOnLine(0.0f,  1.0f, 1.0f, 1.0f),  // occluder: closest
        placeOnLine(20.0f, 1.0f, 0.0f, 0.0f),  // occluded #1
        placeOnLine(30.0f, 0.0f, 1.0f, 0.0f),  // occluded #2
        placeOnLine(40.0f, 0.0f, 0.0f, 1.0f),  // occluded #3
    };

    const glm::vec3 eye = -lineDir * 10.0f;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 camRight = glm::normalize(glm::cross(lineDir, worldUp));

    // Control: offset 10 degrees from lineDir (toward camRight) as seen from eye --
    // well clear of the occluder's ~5.7 deg angular radius, so it is NEVER behind
    // anything on lineDir, at distance 8 for an unmistakably large disc (this
    // geometry only needs to place the body; the dispatch below aims pixel (1,0)
    // EXACTLY at controlRayDir, so its precise on-screen disc size no longer
    // matters the way it did under the old wide-FOV framing approach).
    const glm::vec3 controlRayDir =
        glm::normalize(std::cos(glm::radians(10.0f)) * lineDir + std::sin(glm::radians(10.0f)) * camRight);
    const glm::vec3 controlCentre = eye + controlRayDir * 8.0f;
    const glm::vec3 controlP = controlCentre - glm::vec3(R);
    Vixen::SVO::BodyInstanceGpu control =
        MakeInstance(controlP.x, controlP.y, controlP.z, scale, 0, 1.0f, 1.0f, 0.0f);

    // Camera basis: cameraDir = bisector(lineDir, controlRayDir); cameraRight/Up
    // orthonormal to it. fov derived so tan(halfFov)*aspect places pixel 0 exactly
    // on lineDir and pixel 1 exactly on controlRayDir (aspect=1, kW=2, kH=1 below).
    const glm::vec3 camDir  = glm::normalize(lineDir + controlRayDir);
    const glm::vec3 camRightBasis = glm::normalize(glm::cross(camDir, worldUp));
    const glm::vec3 camUpBasis    = glm::normalize(glm::cross(camRightBasis, camDir));
    const float halfAngleBetween = std::acos(glm::clamp(glm::dot(lineDir, controlRayDir), -1.0f, 1.0f)) * 0.5f;
    const float tanHalfFov = 2.0f * std::tan(halfAngleBetween);
    const float fovDeg = glm::degrees(2.0f * std::atan(tanHalfFov));

    // Deliberately BACK-TO-FRONT array order (the exact reverse of production's own
    // front-to-back sort -- SortInstancesFrontToBack is skipped here on purpose).
    // TraceWorld.glsl's per-ray instance loop processes array ORDER; once ANY
    // instance's traversal legitimately WINS the pixel (isCloserHit), it tightens
    // bestT, and every LATER-processed instance whose AABB entry point is farther
    // than that new bestT gets `instanceIterCount[instIdx]=0` BEFORE its own ESVO
    // traversal ever runs (shaders/TraceWorld.glsl's `entryTWorld > bestT` reject) --
    // a real, already-shipped optimization. A front-to-back (or even a partial,
    // occluder-processed-early) order would let EACH occluded body's own hit reject
    // every FARTHER body processed after it in a chain, so pass A would already show
    // most of them at iterCount=0 for a reason totally unrelated to this test's HiZ
    // cull (verified live: front-to-back zeroed everything; "occ1-3 first, occluder
    // last" still let occ1's own hit chain-reject occ2, which chain-rejected occ3).
    // STRICT back-to-front (farthest instance processed FIRST, nearest LAST) is the
    // one order where this can never happen: each successively-nearer instance's own
    // entryTWorld is by construction <= whatever bestT the farther ones already set,
    // so nothing is ever entry-rejected -- every instance's ESVO traversal genuinely
    // runs in pass A (nonzero iterCount for all 5), and the occluder, processed LAST,
    // still legitimately wins the final hit/colour via isCloserHit. Pass B's
    // skip-mask check (isInstanceSkipped, checked at the TOP of the same loop,
    // unconditionally, before even the AABB test) then zeroes occ1-3 regardless of
    // this processing order -- isolating the HiZ cull's own, measurable contribution
    // from the pre-existing per-ray reject.
    std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        unsorted[3], unsorted[2], unsorted[1],  // occ3, occ2, occ1 -- farthest processed FIRST
        unsorted[0],                            // occluder -- nearest, processed LAST, wins the hit
        control,
    };

    // Recover each body's index by matching worldPos back to the construction-order
    // list above (exact float compare is safe -- nothing here reorders elements).
    auto indexOf = [&](const Vixen::SVO::BodyInstanceGpu& want) -> uint32_t {
        for (uint32_t i = 0; i < instances.size(); ++i) {
            if (instances[i].worldPos[0] == want.worldPos[0] &&
                instances[i].worldPos[1] == want.worldPos[1] &&
                instances[i].worldPos[2] == want.worldPos[2]) {
                return i;
            }
        }
        ADD_FAILURE() << "instance not found in array";
        return UINT32_MAX;
    };
    const uint32_t idxOccluder = indexOf(unsorted[0]);
    const uint32_t idxOcc1     = indexOf(unsorted[1]);
    const uint32_t idxOcc2     = indexOf(unsorted[2]);
    const uint32_t idxOcc3     = indexOf(unsorted[3]);
    const uint32_t idxControl  = indexOf(control);
    ASSERT_NE(idxOccluder, UINT32_MAX); ASSERT_NE(idxOcc1, UINT32_MAX);
    ASSERT_NE(idxOcc2, UINT32_MAX);     ASSERT_NE(idxOcc3, UINT32_MAX);
    ASSERT_NE(idxControl, UINT32_MAX);
    std::printf("[b1-ab] array indices: occluder=%u occ1=%u occ2=%u occ3=%u control=%u\n",
                idxOccluder, idxOcc1, idxOcc2, idxOcc3, idxControl);

    node->SetInstances(instances);
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());

    VkBuffer nodesBuf     = node->GetOutput(C::OCTREE_NODES_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer bricksBuf    = node->GetOutput(C::OCTREE_BRICKS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer materialsBuf = node->GetOutput(C::OCTREE_MATERIALS_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer configBuf    = node->GetOutput(C::OCTREE_CONFIG_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    VkBuffer instanceBuf  = node->GetOutput(C::INSTANCE_BUFFER_Slot::index, 0)->GetHandle<VkBuffer>();
    ASSERT_NE(nodesBuf, VK_NULL_HANDLE);    ASSERT_NE(bricksBuf, VK_NULL_HANDLE);
    ASSERT_NE(materialsBuf, VK_NULL_HANDLE); ASSERT_NE(configBuf, VK_NULL_HANDLE);
    ASSERT_NE(instanceBuf, VK_NULL_HANDLE);

    // Patch traceBoundsMin/Max to a valid, non-degenerate box (the cull shader requires
    // it -- see WriteBackBuffer's own comment for the full why). Read the REAL config
    // first and only overwrite the two trace-bounds fields, keeping every other
    // genuinely-built field (localToWorld, node/brick offsets, ...) untouched. [0,1]^3
    // (the full local cube) is itself a valid, honest bound for this shell body -- not
    // a fabricated tight box -- since the star shell genuinely reaches all six cube
    // faces (ShellVoxelizer.h's watertight-at-any-depth band).
    {
        Vixen::Gpu::OctreeConfig patchedConfig{};
        ASSERT_NO_FATAL_FAILURE(ReadBackBuffer(configBuf, sizeof(patchedConfig), &patchedConfig));
        patchedConfig.traceBoundsMinX = 0.0f; patchedConfig.traceBoundsMinY = 0.0f; patchedConfig.traceBoundsMinZ = 0.0f;
        patchedConfig.traceBoundsMaxX = 1.0f; patchedConfig.traceBoundsMaxY = 1.0f; patchedConfig.traceBoundsMaxZ = 1.0f;
        ASSERT_NO_FATAL_FAILURE(WriteBackBuffer(configBuf, &patchedConfig, sizeof(patchedConfig)));
    }

    // Exactly 2 pixels: (0,0) aimed at lineDir (the occluder stack), (1,0) aimed at
    // controlRayDir (the control) -- see the derivation comment above. This is the
    // race-free replacement for an earlier wide multi-pixel attempt.
    constexpr uint32_t kW = 2, kH = 1;
    PushConstants pc{};
    pc.cameraPos = eye; pc.time = 0.0f;
    pc.cameraDir = camDir; pc.fov = fovDeg;
    pc.cameraUp = camUpBasis;       pc.aspect = 1.0f;
    pc.cameraRight = camRightBasis; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;
    pc.instanceCount = static_cast<int32_t>(instances.size());
    pc.debugTargetPixel = glm::ivec2(-1, -1);

    const uint32_t maxInstances = static_cast<uint32_t>(instances.size());
    ASSERT_EQ(maxInstances, 5u);

    // instanceIterCount (binding 14) has ONE slot per INSTANCE, not per pixel — every
    // pixel's TraceWorld() instance loop visits ALL instances (misses included) and
    // writes instanceIterCount[instIdx] unconditionally, so the kW=2,kH=1 dispatch
    // above puts BOTH in-bounds pixel-threads in the SAME workgroup writing the SAME
    // 5 slots with no synchronization between them (pixel 0's miss-zero for idxControl
    // races pixel 1's real hit-write, and vice versa for idxOccluder/occ1/occ2/occ3).
    // SceneBindings.glsl's own binding-14 comment already documents this buffer as
    // "only meaningful for a single-pixel dispatch" — the 2-pixel dispatch violates
    // that contract. WSL/Dozen's scheduling happens to let both pixels' genuine hits
    // win the race every time (verified); native AMD's wavefront execution does not
    // (verified: real device readback all-zero for every slot on both pixels' own
    // genuinely-hit instances, while the SAME pixels' HitRecord/depth outputs — which
    // don't share a slot across pixels — are correct). Fix: measure instanceIterCount
    // via two SEPARATE single-pixel dispatches (one per target direction, run one at a
    // time with a full submit-and-wait between them so there is no concurrent writer),
    // merged by taking whichever run reports the nonzero value per slot — a genuinely
    // skipped/missed instance is 0 from both runs, a genuinely processed one is nonzero
    // from exactly one. The combined kW=2,kH=1 dispatch stays exactly as it is for the
    // depth image / HitRecordBuffer / colour outputs, which have no cross-pixel index
    // collision and are unaffected by this race.
    auto measureSinglePixelIterCounts = [&](const glm::vec3& targetDir,
                                            VkBuffer skipMaskForThisMeasurement,
                                            VkDeviceSize skipMaskForThisMeasurementSize,
                                            const char* spirvPath = GLSL_RAYMARCH_SPV,
                                            VkBuffer proxyIntervalForThisMeasurement = VK_NULL_HANDLE,
                                            uint32_t proxyAabbCountForThisMeasurement = 0u) {
        const glm::vec3 dirRight = glm::normalize(glm::cross(targetDir, worldUp));
        const glm::vec3 dirUp    = glm::normalize(glm::cross(dirRight, targetDir));
        PushConstants singlePc{};
        singlePc.cameraPos = eye; singlePc.time = 0.0f;
        singlePc.cameraDir = targetDir; singlePc.fov = 45.0f;  // dead-center UV ignores fov entirely
        singlePc.cameraUp = dirUp;       singlePc.aspect = 1.0f;
        singlePc.cameraRight = dirRight; singlePc.debugMode = 0;
        singlePc.raySizeCoef = 0.0f; singlePc.raySizeBias = 0.0f;
        singlePc.instanceCount = static_cast<int32_t>(instances.size());
        singlePc.debugTargetPixel = glm::ivec2(-1, -1);

        VkImage scratchDepthImg = VK_NULL_HANDLE; VkDeviceMemory scratchDepthMem = VK_NULL_HANDLE;
        CreateImage(1, 1, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, scratchDepthImg, scratchDepthMem);
        VkImageView scratchDepthView = CreateView(scratchDepthImg, VK_FORMAT_R32_SFLOAT);

        std::vector<uint32_t> singleIter; std::vector<HitRecordCpu> singleHitRecords;
        March(nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
              skipMaskForThisMeasurement, skipMaskForThisMeasurementSize,
              scratchDepthImg, scratchDepthView, /*clearDepth=*/true,
              singlePc, /*w=*/1, /*h=*/1, maxInstances, singleIter, singleHitRecords,
              spirvPath, proxyIntervalForThisMeasurement,
              proxyAabbCountForThisMeasurement);

        vkDestroyImageView(logicalDevice_, scratchDepthView, nullptr);
        vkDestroyImage(logicalDevice_, scratchDepthImg, nullptr);
        vkFreeMemory(logicalDevice_, scratchDepthMem, nullptr);
        return singleIter;
    };
    auto mergeIterCounts = [](std::vector<uint32_t>& dst, const std::vector<uint32_t>& src) {
        ASSERT_EQ(dst.size(), src.size());
        for (size_t i = 0; i < dst.size(); ++i) {
            if (dst[i] == 0u) dst[i] = src[i];  // a genuine hit from either run wins over a genuine 0
        }
    };

    // Depth image (binding 36, R32F) — shared across pass A / HiZ / cull / pass B,
    // exactly like the real per-frame lifecycle (one depth buffer, written by A,
    // consumed by HiZ, then overwritten again by B).
    const VkFormat kDepthFmt = VK_FORMAT_R32_SFLOAT;
    VkImage depthImg = VK_NULL_HANDLE; VkDeviceMemory depthMem = VK_NULL_HANDLE;
    ASSERT_NO_FATAL_FAILURE(CreateImage(kW, kH, kDepthFmt, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, depthImg, depthMem));
    VkImageView depthView = CreateView(depthImg, kDepthFmt);
    ASSERT_NE(depthView, VK_NULL_HANDLE);

    const uint32_t tilesX = HiZTileCount(kW);
    const uint32_t tilesY = HiZTileCount(kH);
    VkImage tileImg = VK_NULL_HANDLE; VkDeviceMemory tileMem = VK_NULL_HANDLE;
    ASSERT_NO_FATAL_FAILURE(CreateImage(tilesX, tilesY, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, tileImg, tileMem));
    VkImageView tileView = CreateView(tileImg, VK_FORMAT_R32_SFLOAT);
    ASSERT_NE(tileView, VK_NULL_HANDLE);
    {
        // The tile image starts UNDEFINED; HiZ writes every texel unconditionally
        // (one thread per tile, bounds-checked against tilesX/tilesY -- every tile in
        // this exact tilesX*tilesY grid gets a real write, none left stale), so no
        // clear is needed — just the UNDEFINED->GENERAL layout transition.
        VkCommandBuffer cmd = BeginOneShot();
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = tileImg; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));
    }

    // Skip-mask buffer (binding 35): all-zero for pass A, cull-produced for pass B.
    const VkDeviceSize maskBufSize = static_cast<VkDeviceSize>(kSkipMaskWords) * sizeof(uint32_t);
    VkBuffer maskBuf = VK_NULL_HANDLE; VkDeviceMemory maskMem = VK_NULL_HANDLE;
    CreateHostBuffer(maskBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maskBuf, maskMem, /*zero=*/true);

    // --- Pass A: all-zero mask, B1-ON SPV, clear+write the depth image fresh. ---
    std::vector<uint32_t> iterA; std::vector<HitRecordCpu> hitRecordsA;
    ASSERT_NO_FATAL_FAILURE(March(nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
                                  maskBuf, maskBufSize, depthImg, depthView, /*clearDepth=*/true,
                                  pc, kW, kH, maxInstances, iterA, hitRecordsA));
    ASSERT_EQ(iterA.size(), 5u);
    // Race-free re-measurement of instanceIterCount (see measureSinglePixelIterCounts's
    // own comment above) — pass A's skip mask is all-zero, same maskBuf both dispatches.
    iterA.assign(maxInstances, 0u);
    mergeIterCounts(iterA, measureSinglePixelIterCounts(lineDir, maskBuf, maskBufSize));
    mergeIterCounts(iterA, measureSinglePixelIterCounts(controlRayDir, maskBuf, maskBufSize));
    uint32_t sumA = 0; for (uint32_t v : iterA) sumA += v;
    std::printf("[b1-ab] pass A iterCounts: occluder=%u occ1=%u occ2=%u occ3=%u control=%u\n",
                iterA[idxOccluder], iterA[idxOcc1], iterA[idxOcc2], iterA[idxOcc3], iterA[idxControl]);
    EXPECT_GT(iterA[idxOccluder], 0u) << "occluder itself must still traverse in pass A";
    EXPECT_GT(iterA[idxControl], 0u) << "control must traverse in pass A (never occluded)";

    // --- HiZ downsample: depthImg -> tileImg. ---
    ASSERT_NO_FATAL_FAILURE(DispatchHiZDownsample(depthView, tileView, kW, kH, tilesX, tilesY));

    // --- Instance occlusion cull: writes maskBuf in place. ---
    const glm::mat4 viewProj =
        glm::perspective(glm::radians(pc.fov), 1.0f, 0.01f, 1000.0f) *
        glm::lookAt(pc.cameraPos, pc.cameraPos + pc.cameraDir, pc.cameraUp);
    CullPush cullPush{};
    cullPush.prevViewProj = viewProj;
    cullPush.prevCamPos = glm::vec4(pc.cameraPos, 1.0f);
    cullPush.dims[0] = kW; cullPush.dims[1] = kH; cullPush.dims[2] = maxInstances; cullPush.dims[3] = 0u;
    ASSERT_NO_FATAL_FAILURE(DispatchInstanceOcclusionCull(instanceBuf, configBuf, tileView,
                                                          maskBuf, maskBufSize, cullPush));

    uint32_t producedMask[kSkipMaskWords] = {};
    {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, maskMem, 0, maskBufSize, 0, &m), VK_SUCCESS);
        std::memcpy(producedMask, m, static_cast<size_t>(maskBufSize));
        vkUnmapMemory(logicalDevice_, maskMem);
    }
    std::printf("[b1-ab] produced skip mask ownership0=0x%08x camera0=0x%08x\n",
                producedMask[0], producedMask[kCameraMaskWordBase]);

    // (a) exclusion bits: occluded #1-3 must be marked skipped; the occluder and
    // control (indices resolved right after array construction above) must NOT be.
    auto bitSet = [&](uint32_t idx) {
        return (producedMask[kCameraMaskWordBase + (idx >> 5)] & (1u << (idx & 31u))) != 0u;
    };
    EXPECT_TRUE(bitSet(idxOcc1)) << "occluded #1 (idx " << idxOcc1 << ") must be marked skipped";
    EXPECT_TRUE(bitSet(idxOcc2)) << "occluded #2 (idx " << idxOcc2 << ") must be marked skipped";
    EXPECT_TRUE(bitSet(idxOcc3)) << "occluded #3 (idx " << idxOcc3 << ") must be marked skipped";
    EXPECT_FALSE(bitSet(idxOccluder)) << "occluder itself must NOT be marked skipped";
    EXPECT_FALSE(bitSet(idxControl))  << "control must NOT be marked skipped";

    // Hard rule regression: occ1 is camera-visibility culled above, but it is still
    // in front of this independent shadow request. The request starts after the
    // front occluder and reaches occ1 at world-ray t ~= 15, so the expected result
    // is 0 (occluded). With the old single-word contract, cull bits landed in the
    // shadow-visible region and this exact assertion returned 1.
    ShadowRayRequestCpu shadowRequest{};
    shadowRequest.origin = lineDir * 5.0f;
    shadowRequest.tmin = 0.0f;
    shadowRequest.dir = lineDir;
    shadowRequest.tmax = 30.0f;
    PushConstants shadowPc = pc;
    shadowPc.raySizeCoef = 0.0f;
    shadowPc.raySizeBias = 0.0f;
    uint32_t shadowVisible = 1u;
    ASSERT_NO_FATAL_FAILURE(DispatchShadowRay(
        nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
        maskBuf, maskBufSize, shadowPc, shadowRequest, shadowVisible));
    std::printf("[b1-shadow] camera-culled occ1 shadow result=%u (0=occluded)\n",
                shadowVisible);
    EXPECT_EQ(shadowVisible, 0u)
        << "camera-visibility cull bits must never suppress a shadow-casting instance";

    // --- Pass B: cull-produced mask, depth image NOT re-cleared (already holds real
    // distances the cull step just consumed; production leaves it alone across A->B
    // this same frame). ---
    std::vector<uint32_t> iterB; std::vector<HitRecordCpu> hitRecordsB;
    ASSERT_NO_FATAL_FAILURE(March(nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
                                  maskBuf, maskBufSize, depthImg, depthView, /*clearDepth=*/false,
                                  pc, kW, kH, maxInstances, iterB, hitRecordsB));
    ASSERT_EQ(iterB.size(), 5u);
    // Race-free re-measurement (see measureSinglePixelIterCounts's own comment above) —
    // pass B's maskBuf now holds the real cull-produced camera bits, so the primary
    // isInstanceSkippedForPrimary check still correctly zeroes occ1-3 in each run.
    iterB.assign(maxInstances, 0u);
    mergeIterCounts(iterB, measureSinglePixelIterCounts(lineDir, maskBuf, maskBufSize));
    mergeIterCounts(iterB, measureSinglePixelIterCounts(controlRayDir, maskBuf, maskBufSize));
    uint32_t sumB = 0; for (uint32_t v : iterB) sumB += v;
    std::printf("[b1-ab] pass B iterCounts: occluder=%u occ1=%u occ2=%u occ3=%u control=%u\n",
                iterB[idxOccluder], iterB[idxOcc1], iterB[idxOcc2], iterB[idxOcc3], iterB[idxControl]);

    // (b) skipped instances must run ZERO traversal iterations in pass B.
    EXPECT_EQ(iterB[idxOcc1], 0u) << "occluded #1 ran " << iterB[idxOcc1] << " iterations in pass B; expected 0";
    EXPECT_EQ(iterB[idxOcc2], 0u) << "occluded #2 ran " << iterB[idxOcc2] << " iterations in pass B; expected 0";
    EXPECT_EQ(iterB[idxOcc3], 0u) << "occluded #3 ran " << iterB[idxOcc3] << " iterations in pass B; expected 0";
    EXPECT_GT(iterB[idxControl], 0u) << "control must still traverse in pass B";

    // (c) the measured win: >=40% reduction in total traversal iterations.
    const double reduction = sumA > 0 ? (1.0 - static_cast<double>(sumB) / static_cast<double>(sumA)) : 0.0;
    std::cout << "[b1-ab] iters A=" << sumA << " B=" << sumB
              << " reduction=" << (reduction * 100.0) << "%\n";
    EXPECT_LE(sumB, static_cast<uint32_t>(0.6 * static_cast<double>(sumA)))
        << "expected >=40% total-iteration reduction; sumA=" << sumA << " sumB=" << sumB;

    // (d) byte-identical pixels: a fully static scene where occluded instances
    // contribute no visible pixel (each is entirely behind the opaque occluder from
    // the camera's view) must render identically with or without the cull. Judged
    // from HitRecordBuffer (binding 18, the buffer this shader actually still writes
    // per M2c -- see HitRecordCpu's own comment), not the dead colorImg/binding 0.
    ASSERT_EQ(hitRecordsA.size(), hitRecordsB.size());
    EXPECT_EQ(std::memcmp(hitRecordsA.data(), hitRecordsB.data(),
                          hitRecordsA.size() * sizeof(HitRecordCpu)), 0)
        << "pass A and pass B HitRecords must be byte-identical (occlusion cull must not "
           "change visible output in this static scene)";

    // --- B1+B2: same cull mask and scene, adding only the proxy interval/mask
    // input. The two records are the conservative intervals for this fixture's
    // two target rays: pixel 0 admits the collinear stack; pixel 1 admits only
    // the off-axis control. The pre-pass device parity test independently proves
    // both GPU writers produce this exact 32-byte representation.
    using Vixen::RenderGraph::Mirror::AccumulateProxyInterval;
    using Vixen::RenderGraph::Mirror::ClearProxyIntervalPixel;
    using Vixen::RenderGraph::Mirror::ProxyIntervalPixel;
    std::array<ProxyIntervalPixel, 2> proxyPixels = {
        ClearProxyIntervalPixel(), ClearProxyIntervalPixel()};
    for (uint32_t index : {idxOccluder, idxOcc1, idxOcc2, idxOcc3}) {
        ASSERT_TRUE(AccumulateProxyInterval(proxyPixels[0], index, 0.0f, 1000.0f));
    }
    ASSERT_TRUE(AccumulateProxyInterval(proxyPixels[1], idxControl, 0.0f, 1000.0f));

    VkBuffer proxyPixelsBuf = VK_NULL_HANDLE;
    VkDeviceMemory proxyPixelsMem = VK_NULL_HANDLE;
    const VkDeviceSize proxyPixelsSize = sizeof(proxyPixels);
    CreateHostBuffer(proxyPixelsSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     proxyPixelsBuf, proxyPixelsMem, /*zero=*/false);
    UploadBuffer(proxyPixelsMem, proxyPixels.data(), sizeof(proxyPixels));

    VkBuffer proxyLineBuf = VK_NULL_HANDLE, proxyControlBuf = VK_NULL_HANDLE;
    VkDeviceMemory proxyLineMem = VK_NULL_HANDLE, proxyControlMem = VK_NULL_HANDLE;
    CreateHostBuffer(sizeof(ProxyIntervalPixel), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     proxyLineBuf, proxyLineMem, /*zero=*/false);
    CreateHostBuffer(sizeof(ProxyIntervalPixel), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     proxyControlBuf, proxyControlMem, /*zero=*/false);
    UploadBuffer(proxyLineMem, &proxyPixels[0], sizeof(ProxyIntervalPixel));
    UploadBuffer(proxyControlMem, &proxyPixels[1], sizeof(ProxyIntervalPixel));

    std::vector<uint32_t> iterB2;
    std::vector<HitRecordCpu> hitRecordsB2;
    ASSERT_NO_FATAL_FAILURE(March(
        nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
        maskBuf, maskBufSize, depthImg, depthView, /*clearDepth=*/false,
        pc, kW, kH, maxInstances, iterB2, hitRecordsB2,
        GLSL_RAYMARCH_B2_SPV, proxyPixelsBuf, /*proxyAabbCount=*/1u));
    iterB2.assign(maxInstances, 0u);
    mergeIterCounts(iterB2, measureSinglePixelIterCounts(
        lineDir, maskBuf, maskBufSize, GLSL_RAYMARCH_B2_SPV,
        proxyLineBuf, /*proxyAabbCount=*/1u));
    mergeIterCounts(iterB2, measureSinglePixelIterCounts(
        controlRayDir, maskBuf, maskBufSize, GLSL_RAYMARCH_B2_SPV,
        proxyControlBuf, /*proxyAabbCount=*/1u));
    uint32_t sumB2 = 0u;
    for (uint32_t value : iterB2) sumB2 += value;
    std::printf("[b2-ab] B1+B2 iterCounts: occluder=%u occ1=%u occ2=%u occ3=%u control=%u\n",
                iterB2[idxOccluder], iterB2[idxOcc1], iterB2[idxOcc2],
                iterB2[idxOcc3], iterB2[idxControl]);
    std::cout << "[b2-ab] effective iters B1=" << sumB
              << " B1+B2=" << sumB2 << "\n";
    EXPECT_LE(sumB2, sumB)
        << "B2 must beat or match B1 effective traversal iterations";
    ASSERT_EQ(hitRecordsB.size(), hitRecordsB2.size());
    EXPECT_EQ(std::memcmp(hitRecordsB.data(), hitRecordsB2.data(),
                          hitRecordsB.size() * sizeof(HitRecordCpu)), 0)
        << "B1 and B1+B2 HitRecords must be byte-identical";

    // (e) cross-check the produced mask against the CPU mirror fed from the REAL
    // config buffer read back off the device -- proves GPU chain == CPU mirror
    // end-to-end, not merely a hand-typed expectation.
    Vixen::Gpu::OctreeConfig realConfig{};
    ASSERT_NO_FATAL_FAILURE(ReadBackBuffer(configBuf, sizeof(realConfig), &realConfig));
    static_assert(sizeof(Vixen::Gpu::OctreeConfig) == sizeof(realConfig), "sanity");

    std::vector<float> tileMaxReadback(static_cast<size_t>(tilesX) * tilesY, 0.0f);
    {
        VkBuffer tileRbBuf = VK_NULL_HANDLE; VkDeviceMemory tileRbMem = VK_NULL_HANDLE;
        const VkDeviceSize tileRbSize =
            static_cast<VkDeviceSize>(tilesX) * tilesY * sizeof(float);
        CreateHostBuffer(tileRbSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, tileRbBuf, tileRbMem, false);
        VkCommandBuffer cmd = BeginOneShot();
        VkImageMemoryBarrier toSrc{};
        toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = tileImg; toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {tilesX, tilesY, 1};
        vkCmdCopyImageToBuffer(cmd, tileImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tileRbBuf, 1, &copy);
        VkBufferMemoryBarrier toHost{};
        toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer = tileRbBuf; toHost.offset = 0; toHost.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, 1, &toHost, 0, nullptr);
        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);
        ASSERT_NO_FATAL_FAILURE(SubmitAndWait(cmd));
        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, tileRbMem, 0, tileRbSize, 0, &mapped), VK_SUCCESS);
        std::memcpy(tileMaxReadback.data(), mapped, static_cast<size_t>(tileRbSize));
        vkUnmapMemory(logicalDevice_, tileRbMem);
        vkDestroyBuffer(logicalDevice_, tileRbBuf, nullptr);
        vkFreeMemory(logicalDevice_, tileRbMem, nullptr);
    }

    // Every instance shares octreeIndex=0, so the mirror's per-octree configs array
    // is a single real entry, repeated by CullInstance::octreeIndex.
    std::vector<CullInstance> mirrorInstances(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        mirrorInstances[i].worldPos = glm::vec3(instances[i].worldPos[0], instances[i].worldPos[1],
                                                instances[i].worldPos[2]);
        mirrorInstances[i].renderScale = instances[i].renderScale;
        mirrorInstances[i].octreeIndex = instances[i].octreeIndex;
        mirrorInstances[i].providerKind = instances[i].providerKind;
    }
    CullOctreeConfig mirrorConfig{};
    mirrorConfig.traceBoundsMin = glm::vec3(realConfig.traceBoundsMinX, realConfig.traceBoundsMinY, realConfig.traceBoundsMinZ);
    mirrorConfig.traceBoundsMax = glm::vec3(realConfig.traceBoundsMaxX, realConfig.traceBoundsMaxY, realConfig.traceBoundsMaxZ);
    mirrorConfig.localToWorld = realConfig.localToWorld;
    std::vector<CullOctreeConfig> mirrorConfigs = {mirrorConfig};

    CullParams mirrorParams{};
    mirrorParams.prevViewProj = cullPush.prevViewProj;
    mirrorParams.prevCamPos = glm::vec3(cullPush.prevCamPos);
    mirrorParams.srcWidth = kW; mirrorParams.srcHeight = kH;
    mirrorParams.instanceCount = maxInstances;
    const uint32_t mirrorCameraWord0 = CullMaskWord(0u, mirrorInstances.data(), mirrorConfigs.data(),
                                                    tileMaxReadback.data(), mirrorParams, 0u);
    std::printf("[b1-ab] mirror camera0=0x%08x gpu camera0=0x%08x\n",
                mirrorCameraWord0, producedMask[kCameraMaskWordBase]);
    EXPECT_EQ(mirrorCameraWord0, producedMask[kCameraMaskWordBase])
        << "GPU-produced skip mask must match the CPU mirror fed the identical real "
           "config/instances/tile-max inputs (end-to-end parity)";

    vkDestroyBuffer(logicalDevice_, proxyControlBuf, nullptr);
    vkFreeMemory(logicalDevice_, proxyControlMem, nullptr);
    vkDestroyBuffer(logicalDevice_, proxyLineBuf, nullptr);
    vkFreeMemory(logicalDevice_, proxyLineMem, nullptr);
    vkDestroyBuffer(logicalDevice_, proxyPixelsBuf, nullptr);
    vkFreeMemory(logicalDevice_, proxyPixelsMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maskBuf, nullptr); vkFreeMemory(logicalDevice_, maskMem, nullptr);
    vkDestroyImageView(logicalDevice_, tileView, nullptr);
    vkDestroyImage(logicalDevice_, tileImg, nullptr); vkFreeMemory(logicalDevice_, tileMem, nullptr);
    vkDestroyImageView(logicalDevice_, depthView, nullptr);
    vkDestroyImage(logicalDevice_, depthImg, nullptr); vkFreeMemory(logicalDevice_, depthMem, nullptr);

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}
