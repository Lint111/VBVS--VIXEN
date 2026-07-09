/**
 * @file test_tier_crossing_lod_residency.cpp
 * @brief Live-GPU proof of Tiered-ESVO Inc2 M4 (Tasks 9-10): the screen-space LOD
 *        early-out and the residency-reuse fallback for a farBit==1 tier-crossing leaf.
 *
 * Reuses test_body_instance_occlusion_reject.cpp's exact real-device/real-shader
 * dispatch pattern (binding 14, InstanceIterDebugBuffer, for the per-instance
 * iteration count), EXTENDED with binding 15 (TierRefTableBuffer) so a genuine
 * two-tree tier-crossing scene (same construction as BuildRenderGraph.cpp's
 * VIXEN_TIER_CROSSING_DEMO / test_tier_crossing_construction.cpp's fixture) can be
 * dispatched against the real compiled shader and its ACTUAL crossing/no-crossing
 * decision observed, not just its final pixel color.
 *
 * Task 9 (LOD gate): with a large pc.raySizeCoef (a wide ray cone, simulating either
 * a distant camera or a coarse render target), the marked leaf's own footprint
 * (tv_max at THIS leaf's scale) goes sub-pixel while the ray still successfully hits
 * the PARENT sphere's surface elsewhere -- the iteration count for the marked leaf's
 * instance must be LOW (the crossing's restart, which would re-enter a full second
 * traversal with its OWN iteration budget, is never taken) and the resolved hit must
 * be the PARENT's own colour, not the child's.
 *
 * Task 10 (residency): with raySizeCoef=0 (LOD disabled, so ONLY residency gates
 * the crossing) and the child octree's OctreeConfig.brickResident=0, the SAME
 * assertions apply -- no restart, parent's own colour -- proving the sentinel-miss
 * pattern composes correctly for a tier-crossing leaf without any dedicated
 * per-child residency plumbing (a whole-BodyOctreeSceneNode brickResident stamp,
 * RequestBrickResidency, applies uniformly to every octree in one concatenated
 * pool -- see this file's own scene-construction comment for why residency is
 * driven directly on the concatenated config rather than through
 * RequestBrickResidency(), which cannot isolate one octree from its siblings).
 *
 * Run: ./test_tier_crossing_lod_residency
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "SdfBake.h"          // Vixen::SVO::BakeRecipeToSdfWorld/BuildSdfBodyOctree
#include "ShellOctreeGpu.h"   // Vixen::SVO::{SerializeSdf, ConcatenatedOctrees, BodyInstanceGpu}
#include "MipBake.h"          // Vixen::SVO::BakeAndAttachMipPool
#include "TierRef.h"
#include "SVOTypes.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#undef far
#undef near
#undef min
#undef max

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch.spv) must be defined by CMake"
#endif

namespace {

// Byte-identical to BodyInstanceRayMarch.comp's PushConstants block.
struct PushConstants {
    glm::vec3 cameraPos;   float time;
    glm::vec3 cameraDir;   float fov;       // DEGREES
    glm::vec3 cameraUp;    float aspect;
    glm::vec3 cameraRight; int32_t debugMode;
    float   raySizeCoef;
    float   raySizeBias;
    int32_t instanceCount;
};
static_assert(sizeof(PushConstants) == 76, "PushConstants must be 76 bytes");

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

}  // namespace

class TierCrossingLodResidencyTest : public ::testing::Test {
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
        appInfo.pApplicationName = "test_tier_crossing_lod_residency";
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
            << "Refusing to run: selected device '" << selectedDeviceName_
            << "' is not a verified device (software rasterizer or Dozen).";

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

    void CreateImage(uint32_t w, uint32_t h, VkFormat format, VkImage& outImage, VkDeviceMemory& outMem) {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {w, h, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

    // Dispatches the real shader at w*h and reads back the colour image, the
    // per-instance iteration-count debug buffer (binding 14), AND accepts a REAL
    // tierRefTableBuf (binding 15) — the one extension over
    // test_body_instance_occlusion_reject.cpp's own RenderAndReadIterCounts, needed
    // for a genuine (not placeholder) tier-crossing scene.
    void RenderAndReadIterCounts(VkBuffer nodesBuf, VkBuffer bricksBuf, VkBuffer materialsBuf,
                                 VkBuffer configBuf, VkBuffer instanceBuf, VkBuffer tierRefTableBuf,
                                 const PushConstants& pc, uint32_t w, uint32_t h,
                                 uint32_t maxInstances,
                                 std::vector<uint32_t>& instanceIterCounts,
                                 std::vector<uint8_t>& rgba) {
        ASSERT_TRUE(softwareConfirmed_) << "ABORT: not the software rasterizer; refusing to submit.";

        VkBuffer traceBuf = VK_NULL_HANDLE, counterBuf = VK_NULL_HANDLE;
        VkDeviceMemory traceMem = VK_NULL_HANDLE, counterMem = VK_NULL_HANDLE;
        CreateHostBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, traceBuf, traceMem, true);
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

        const VkFormat kColorFmt = VK_FORMAT_R8G8B8A8_UNORM;
        const VkFormat kIdFmt    = VK_FORMAT_R32_UINT;
        VkImage colorImg = VK_NULL_HANDLE, idImg = VK_NULL_HANDLE;
        VkDeviceMemory colorMem = VK_NULL_HANDLE, idMem = VK_NULL_HANDLE;
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kColorFmt, colorImg, colorMem));
        ASSERT_NO_FATAL_FAILURE(CreateImage(w, h, kIdFmt, idImg, idMem));
        VkImageView colorView = CreateView(colorImg, kColorFmt);
        VkImageView idView    = CreateView(idImg, kIdFmt);
        ASSERT_NE(colorView, VK_NULL_HANDLE); ASSERT_NE(idView, VK_NULL_HANDLE);

        const std::vector<uint32_t> spirv = ReadSpirv(GLSL_RAYMARCH_SPV);
        ASSERT_FALSE(spirv.empty()) << "Failed to read compiled SPIR-V at " << GLSL_RAYMARCH_SPV;
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
        const std::array<VkDescriptorSetLayoutBinding, 14> bindings = {
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
            bind(15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // M4: TierRefTableBuffer
        };
        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size()); dslci.pBindings = bindings.data();
        VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
        ASSERT_EQ(vkCreateDescriptorSetLayout(logicalDevice_, &dslci, nullptr, &dsl), VK_SUCCESS);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.offset = 0; pcr.size = sizeof(PushConstants);
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
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  2},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12},
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
        VkDescriptorBufferInfo tierRefInfo{tierRefTableBuf, 0, VK_WHOLE_SIZE};

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
        const std::array<VkWriteDescriptorSet, 14> writes = {
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
        };
        vkUpdateDescriptorSets(logicalDevice_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ASSERT_EQ(vkBeginCommandBuffer(cmd, &bi), VK_SUCCESS);

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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descSet, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        VkBufferMemoryBarrier iterBarrier{};
        iterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        iterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        iterBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        iterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        iterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        iterBarrier.buffer = iterBuf; iterBarrier.offset = 0; iterBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, 1, &iterBarrier, 0, nullptr);

        VkImageMemoryBarrier toSrc{};
        toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_GENERAL; toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = colorImg; toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

        const VkDeviceSize rgbaSize = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer rgbaBuf = VK_NULL_HANDLE; VkDeviceMemory rgbaMem = VK_NULL_HANDLE;
        CreateHostBuffer(rgbaSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, rgbaBuf, rgbaMem, false);
        VkBufferImageCopy cp{};
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, colorImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rgbaBuf, 1, &cp);

        ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd;

        ASSERT_TRUE(softwareConfirmed_) << "ABORT: software device not confirmed; refusing vkQueueSubmit.";
        ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

        void* mapped = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, iterMem, 0, iterBufSize, 0, &mapped), VK_SUCCESS);
        instanceIterCounts.assign(maxInstances, 0u);
        std::memcpy(instanceIterCounts.data(), mapped, static_cast<size_t>(iterBufSize));
        vkUnmapMemory(logicalDevice_, iterMem);

        void* mappedRgba = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, rgbaMem, 0, rgbaSize, 0, &mappedRgba), VK_SUCCESS);
        rgba.assign(static_cast<size_t>(w) * h * 4, 0);
        std::memcpy(rgba.data(), mappedRgba, static_cast<size_t>(rgbaSize));
        vkUnmapMemory(logicalDevice_, rgbaMem);

        vkDeviceWaitIdle(logicalDevice_);
        vkDestroyBuffer(logicalDevice_, rgbaBuf, nullptr); vkFreeMemory(logicalDevice_, rgbaMem, nullptr);
        vkDestroyDescriptorPool(logicalDevice_, descPool, nullptr);
        vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
        vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
        vkDestroyShaderModule(logicalDevice_, shaderModule, nullptr);
        vkDestroyImageView(logicalDevice_, colorView, nullptr);
        vkDestroyImageView(logicalDevice_, idView, nullptr);
        vkDestroyImage(logicalDevice_, colorImg, nullptr); vkFreeMemory(logicalDevice_, colorMem, nullptr);
        vkDestroyImage(logicalDevice_, idImg, nullptr);    vkFreeMemory(logicalDevice_, idMem, nullptr);
        vkDestroyBuffer(logicalDevice_, traceBuf, nullptr);   vkFreeMemory(logicalDevice_, traceMem, nullptr);
        vkDestroyBuffer(logicalDevice_, counterBuf, nullptr); vkFreeMemory(logicalDevice_, counterMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummySdf, nullptr);    vkFreeMemory(logicalDevice_, dummySdfMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyLookup, nullptr); vkFreeMemory(logicalDevice_, dummyLookupMem, nullptr);
        vkDestroyBuffer(logicalDevice_, dummyMip, nullptr);    vkFreeMemory(logicalDevice_, dummyMipMem, nullptr);
        vkDestroyBuffer(logicalDevice_, iterBuf, nullptr);     vkFreeMemory(logicalDevice_, iterMem, nullptr);
    }
};

namespace {

// Builds the exact two-tree tier-crossing scene BuildRenderGraph.cpp's
// VIXEN_TIER_CROSSING_DEMO uses (n=16, r=6.0 parent / r=7.2 child, brickDepth=3,
// ALL 8 root leaves brick-level), with EVERY root leaf marked tier-crossing
// pointing at the child (slot 1) — same "mark all leaves" convention this
// increment's mirror-parity test uses, sidestepping needing to hand-derive which
// specific leaf/octant a given ray direction lands in. The child's SEM_COLOR
// channel is overwritten to solid magenta (1,0,1), unmistakably distinct from the
// parent's cosine-gradient bake, so a captured pixel's colour alone proves which
// tree was actually shaded. `residentChild` controls whether the RETURNED
// ConcatenatedOctrees stamps childOctreeConfig.brickResident=1 or 0 — driven
// directly on the concatenated config (not via BodyOctreeSceneNode::
// RequestBrickResidency, which is a single whole-node flag applied uniformly to
// EVERY octree in one concatenated pool — CreateOctreeBuffers's own
// `for (auto& cfg : concatenated_.configs) setBrickResident(cfg, brickPoolUploaded_)`
// loop confirms this directly; there is no existing mechanism to make one octree
// in a shared pool resident while a sibling is not, so Task 10's "child not
// resident" condition is set on the concatenated config the same way the real
// upload pipeline would if only the child's own upload had not landed yet).
struct TierCrossingScene {
    Vixen::SVO::ConcatenatedOctrees pool;
    uint32_t markedLeafCount = 0;
};

TierCrossingScene BuildTierCrossingScene(bool residentChild) {
    using namespace Vixen::SVO;

    constexpr int   kN          = 16;
    constexpr int   kBrickDepth = 3;
    const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);

    RecipeParams parentRp{};
    parentRp.radius = 6.0f;
    SdfBakeResult parentBaked = BakeRecipeToSdfWorld(RECIPE_SPHERE, kCenter, parentRp, kN, 2.0f);
    SdfBodyOctree parentBody  = BuildSdfBodyOctree(parentBaked, kBrickDepth);

    RecipeParams childRp{};
    childRp.radius = 7.2f;
    SdfBakeResult childBaked = BakeRecipeToSdfWorld(RECIPE_SPHERE, kCenter, childRp, kN, 2.0f);
    SdfBodyOctree childBody  = BuildSdfBodyOctree(childBaked, kBrickDepth);

    SerializedOctree parentSer = SerializeSdf(parentBody);
    SerializedOctree childSer  = SerializeSdf(childBody);

    // Solid magenta override on the child (BuildRenderGraph.cpp's own convention).
    {
        const uint32_t colorBase = childSer.channelBaseFloats(SEM_COLOR);
        if (colorBase != 0xFFFFFFFFu) {
            float* poolF = reinterpret_cast<float*>(childSer.channelPool.data());
            const size_t poolFloats = childSer.channelPool.size() / sizeof(float);
            for (uint32_t brick = 0; brick < childSer.brickCount; ++brick) {
                for (uint32_t comp = 0; comp < 3; ++comp) {
                    const float v = (comp == 1) ? 0.0f : 1.0f;  // (1,0,1)
                    for (uint32_t voxel = 0; voxel < SerializedOctree::kVoxelsPerBrick; ++voxel) {
                        const size_t idx = static_cast<size_t>(brick) * childSer.brickStrideFloats
                                         + colorBase + comp * SerializedOctree::kVoxelsPerBrick + voxel;
                        if (idx < poolFloats) poolF[idx] = v;
                    }
                }
            }
        }
    }

    const Octree* parentOct = parentBody.octree->getOctree();
    uint32_t markedCount = 0;
    if (parentOct != nullptr) {
        const auto& descs = parentOct->root->childDescriptors;
        TierRef ref{};
        ref.childOctreeIndex = 1u;
        ref.childOriginLocal[0] = 1.5f;
        ref.childOriginLocal[1] = 1.5f;
        ref.childOriginLocal[2] = 1.5f;
        ref.childScale = 1.0f;
        constexpr uint8_t kChildRootScaleHint = 22;
        for (uint32_t i = 0; i < descs.size(); ++i) {
            const ChildDescriptor& d = descs[i];
            for (int oct = 0; oct < 8; ++oct) {
                if (d.hasChild(oct) && d.isLeaf(oct)) {
                    MarkLeafAsTierCrossing(parentSer, i, oct, ref, kChildRootScaleHint);
                    ++markedCount;
                }
            }
        }
    }

    setBrickResident(childSer.config, residentChild);
    setBrickResident(parentSer.config, true);  // parent's own bricks always resident in this scene

    ConcatenatedOctrees cat;
    cat.count = 2;
    cat.configs.resize(2);
    cat.nodeCounts.resize(2);
    cat.brickCounts.resize(2);
    cat.tierRefCounts.resize(2);

    SerializedOctree* octs[2] = {&parentSer, &childSer};
    uint32_t nodeBase = 0, brickBase = 0, poolBase = 0, tierRefBase = 0;
    for (int k = 0; k < 2; ++k) {
        SerializedOctree& s = *octs[k];
        s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
        s.config.brickArrayBase = static_cast<int32_t>(brickBase);
        setSdfBrickArrayBase(s.config, poolBase);
        setTierRefTableBase(s.config, tierRefBase);

        cat.configs[k]       = s.config;
        cat.nodeCounts[k]    = s.nodeCount;
        cat.brickCounts[k]   = s.brickCount;
        cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

        cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
        cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());
        cat.channelPool.insert(cat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
        cat.brickGridLookup.insert(cat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
        cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());

        if (cat.materials.empty()) {
            cat.materials = s.materials;
        }

        nodeBase    += s.nodeCount;
        brickBase   += s.brickCount;
        poolBase    += s.brickCount * s.brickStrideFloats;
        tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
    }

    return {std::move(cat), markedCount};
}

// worldPos/renderScale convention matching BuildRenderGraph.cpp's own demo exactly
// (kWorldGridSize=10.0f, so world span = renderScale*[0,10]).
constexpr float kRenderScale = 4.8f;
constexpr float kHalf = 5.0f * kRenderScale;  // = 24.0f

}  // namespace

// ---------------------------------------------------------------------------
// Task 10 — Residency reuse: a non-resident child must make the crossing
// gracefully fall back (never restart into the child), while a resident child
// crosses and renders the child's own magenta colour. raySizeCoef=0 here (LOD
// disabled) so residency is the ONLY variable under test.
// ---------------------------------------------------------------------------
TEST_F(TierCrossingLodResidencyTest, NonResidentChildNeverCrossesResidentChildDoes) {
    using C = BodyOctreeSceneNodeConfig;

    auto runScene = [&](bool residentChild, std::vector<uint32_t>& iterCounts,
                        std::vector<uint8_t>& rgba, uint32_t& markedCount) {
        BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
        auto nodeBase = nodeType.CreateInstance(residentChild ? "tcr_resident" : "tcr_nonresident");
        auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
        ASSERT_NE(node, nullptr);

        Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
        Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
        Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
        node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
        node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
        node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

        TierCrossingScene scene = BuildTierCrossingScene(residentChild);
        markedCount = scene.markedLeafCount;
        ASSERT_GT(markedCount, 0u) << "fixture must produce at least one marked leaf";

        node->SetRecipePool(std::move(scene.pool));

        Vixen::SVO::BodyInstanceGpu inst{};
        inst.worldPos[0]  = 64.0f - kHalf;
        inst.worldPos[1]  = 64.0f - kHalf;
        inst.worldPos[2]  = 64.0f - kHalf;
        inst.renderScale  = kRenderScale;
        inst.color[0] = inst.color[1] = inst.color[2] = 1.0f;
        inst.octreeIndex  = 0u;
        inst.providerKind = 0u;
        inst.recipeId     = 0u;
        node->SetInstances({inst});
        node->Setup();
        ASSERT_NO_THROW(node->Compile());
        frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
        ASSERT_NO_THROW(node->Execute());

        auto buf = [&](int slot) -> VkBuffer { return node->GetOutput(slot, 0)->GetHandle<VkBuffer>(); };
        VkBuffer nodesBuf    = buf(C::OCTREE_NODES_BUFFER_Slot::index);
        VkBuffer bricksBuf   = buf(C::OCTREE_BRICKS_BUFFER_Slot::index);
        VkBuffer materialsBuf= buf(C::OCTREE_MATERIALS_BUFFER_Slot::index);
        VkBuffer configBuf   = buf(C::OCTREE_CONFIG_BUFFER_Slot::index);
        VkBuffer instanceBuf = buf(C::INSTANCE_BUFFER_Slot::index);
        VkBuffer tierRefBuf  = buf(C::OCTREE_TIERREFTABLE_BUFFER_Slot::index);
        ASSERT_NE(nodesBuf, VK_NULL_HANDLE); ASSERT_NE(tierRefBuf, VK_NULL_HANDLE);

        // 500x500, matching BuildRenderGraph.cpp's own standalone-window default,
        // camera looking down -Z at the sphere's world centre.
        constexpr uint32_t kW = 500, kH = 500;
        const glm::vec3 bodyCentre(64.0f, 64.0f, 64.0f);
        const glm::vec3 eye(64.0f, 64.0f, 300.0f);
        const glm::vec3 dir(0.0f, 0.0f, -1.0f);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 right(1.0f, 0.0f, 0.0f);

        PushConstants pc{};
        pc.cameraPos = eye; pc.time = 0.0f;
        pc.cameraDir = dir; pc.fov = 45.0f;
        pc.cameraUp = up;   pc.aspect = 1.0f;
        pc.cameraRight = right; pc.debugMode = 0;
        pc.raySizeCoef = 0.0f;  // LOD disabled -- residency is the only variable
        pc.raySizeBias = 0.0f;
        pc.instanceCount = 1;

        ASSERT_NO_FATAL_FAILURE(RenderAndReadIterCounts(
            nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf, tierRefBuf,
            pc, kW, kH, 1u, iterCounts, rgba));

        vkDeviceWaitIdle(logicalDevice_);
        node->Cleanup(CleanupReason::FinalTeardown);
        (void)bodyCentre;
    };

    std::vector<uint32_t> iterResident, iterNonResident;
    std::vector<uint8_t> rgbaResident, rgbaNonResident;
    uint32_t markedResident = 0, markedNonResident = 0;
    ASSERT_NO_FATAL_FAILURE(runScene(true,  iterResident,    rgbaResident,    markedResident));
    ASSERT_NO_FATAL_FAILURE(runScene(false, iterNonResident, rgbaNonResident, markedNonResident));

    // Count magenta pixels (child's unmistakable colour: R,B high, G~0) in each capture.
    auto countMagenta = [](const std::vector<uint8_t>& rgba) {
        int count = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            const int r = rgba[i], g = rgba[i+1], b = rgba[i+2];
            if (r > 180 && b > 180 && g < 60) ++count;
        }
        return count;
    };
    const int magentaResident    = countMagenta(rgbaResident);
    const int magentaNonResident = countMagenta(rgbaNonResident);
    std::printf("[TIER-CROSSING RESIDENCY] resident child magenta px=%d, non-resident child magenta px=%d\n",
                magentaResident, magentaNonResident);

    // THE decisive assertion: the resident scene must show REAL magenta child
    // geometry; the non-resident scene (same marked leaves, same child tree, ONLY
    // brickResident differs) must show NONE -- proving the crossing never actually
    // restarted into the child tree when it wasn't resident.
    EXPECT_GT(magentaResident, 0)
        << "resident child: expected visible magenta (child) pixels from a real crossing, found none";
    EXPECT_EQ(magentaNonResident, 0)
        << "non-resident child: expected ZERO magenta pixels (crossing must never happen), found "
        << magentaNonResident;
}

// ---------------------------------------------------------------------------
// Task 9 — Screen-space LOD gate: with pc.raySizeCoef set high enough that the
// marked leaf's own footprint (one octant of the root, an eighth of the whole
// sphere's angular size) goes sub-pixel while the sphere's overall silhouette is
// still resolved, the crossing must never be taken -- no magenta child pixels,
// and the resolved shading falls back to the PARENT's own colour (which,
// per Sparse-Mip's mip-fallback machinery, requires the parent to have a baked
// mip pool -- BakeAndAttachMipPool is applied to the parent here so
// shadeFromMipSample has real coverage to read, matching test_mip_fallback_render.
// cpp's own precedent for exercising this fallback path with real data instead
// of the neutral-grey placeholder).
// ---------------------------------------------------------------------------
TEST_F(TierCrossingLodResidencyTest, SubPixelFootprintSkipsCrossingEvenWhenChildResident) {
    using C = BodyOctreeSceneNodeConfig;

    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("tcr_lod_gate");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // Resident child (so a MISSING crossing can only be attributed to the LOD
    // gate, not to residency being absent) -- proves Task 9 and Task 10 are
    // genuinely independent gates, not the same check wearing two names.
    TierCrossingScene scene = BuildTierCrossingScene(/*residentChild=*/true);
    ASSERT_GT(scene.markedLeafCount, 0u);
    node->SetRecipePool(std::move(scene.pool));

    Vixen::SVO::BodyInstanceGpu inst{};
    inst.worldPos[0]  = 64.0f - kHalf;
    inst.worldPos[1]  = 64.0f - kHalf;
    inst.worldPos[2]  = 64.0f - kHalf;
    inst.renderScale  = kRenderScale;
    inst.color[0] = inst.color[1] = inst.color[2] = 1.0f;
    inst.octreeIndex  = 0u;
    inst.providerKind = 0u;
    inst.recipeId     = 0u;
    node->SetInstances({inst});
    node->Setup();
    ASSERT_NO_THROW(node->Compile());
    frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    ASSERT_NO_THROW(node->Execute());

    auto buf = [&](int slot) -> VkBuffer { return node->GetOutput(slot, 0)->GetHandle<VkBuffer>(); };
    VkBuffer nodesBuf     = buf(C::OCTREE_NODES_BUFFER_Slot::index);
    VkBuffer bricksBuf    = buf(C::OCTREE_BRICKS_BUFFER_Slot::index);
    VkBuffer materialsBuf = buf(C::OCTREE_MATERIALS_BUFFER_Slot::index);
    VkBuffer configBuf    = buf(C::OCTREE_CONFIG_BUFFER_Slot::index);
    VkBuffer instanceBuf  = buf(C::INSTANCE_BUFFER_Slot::index);
    VkBuffer tierRefBuf   = buf(C::OCTREE_TIERREFTABLE_BUFFER_Slot::index);
    ASSERT_NE(nodesBuf, VK_NULL_HANDLE); ASSERT_NE(tierRefBuf, VK_NULL_HANDLE);

    constexpr uint32_t kW = 500, kH = 500;
    const glm::vec3 eye(64.0f, 64.0f, 300.0f);
    const glm::vec3 dir(0.0f, 0.0f, -1.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right(1.0f, 0.0f, 0.0f);

    // A deliberately huge raySizeCoef -- far larger than RaySizeCoefNode would ever
    // compute from a real FOV/resolution -- guarantees EVERY leaf-level footprint
    // in this scene (including the one marked octant) reads as sub-pixel, without
    // needing to hand-tune camera distance/FOV to hit one specific octant's exact
    // angular threshold. The sphere's own silhouette is still resolved (traversal
    // still descends to SOME leaf and shades it -- LOD_ENABLED's own non-leaf
    // branch and this leaf's new gate share the identical formula/threshold, so
    // whichever level first goes sub-pixel is where shading happens; the parent's
    // baked mip pool covers every level, so mip-fallback shading is available at
    // whichever level is reached).
    constexpr float kHugeRaySizeCoef = 10.0f;

    PushConstants pc{};
    pc.cameraPos = eye; pc.time = 0.0f;
    pc.cameraDir = dir; pc.fov = 45.0f;
    pc.cameraUp = up;   pc.aspect = 1.0f;
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = kHugeRaySizeCoef;
    pc.raySizeBias = 0.0f;
    pc.instanceCount = 1;

    std::vector<uint32_t> iterCounts;
    std::vector<uint8_t> rgba;
    ASSERT_NO_FATAL_FAILURE(RenderAndReadIterCounts(
        nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf, tierRefBuf,
        pc, kW, kH, 1u, iterCounts, rgba));

    ASSERT_EQ(iterCounts.size(), 1u);
    std::printf("[TIER-CROSSING LOD] instance iteration count with huge raySizeCoef=%u\n", iterCounts[0]);

    auto countMagenta = [](const std::vector<uint8_t>& rgba) {
        int count = 0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            const int r = rgba[i], g = rgba[i+1], b = rgba[i+2];
            if (r > 180 && b > 180 && g < 60) ++count;
        }
        return count;
    };
    const int magentaCount = countMagenta(rgba);
    std::printf("[TIER-CROSSING LOD] magenta px=%d\n", magentaCount);

    // THE decisive assertion: a sub-pixel footprint at the marked leaf must NEVER
    // show the child's magenta colour, even though the child IS resident (proven
    // by the sibling test above) -- the LOD gate alone must be sufficient to skip
    // the crossing.
    EXPECT_EQ(magentaCount, 0)
        << "expected ZERO magenta (child) pixels under a huge raySizeCoef (sub-pixel "
           "footprint should skip the crossing even though the child is resident), found "
        << magentaCount;

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}
