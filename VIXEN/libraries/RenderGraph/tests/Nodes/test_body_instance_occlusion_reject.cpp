/**
 * @file test_body_instance_occlusion_reject.cpp
 * @brief Proves the GPU per-ray occlusion reject (Sparse-Mip ESVO LOD Inc1 M4b):
 *        a synthetic camera -> occluder -> occluded-target line-up, front-to-back
 *        sorted CPU-side, asserts the occluded instance's ESVO traversal ran ZERO
 *        iterations once the occluder's hit was recorded on the same ray — not just
 *        "no visible pixel difference."
 *
 * Reuses the same lavapipe/Mesa-Dozen device bring-up and real-shader dispatch
 * pattern as test_body_instance_raymarch_render.cpp, extended with a NEW readback
 * buffer (binding 14, InstanceIterDebugBuffer) that BodyInstanceRayMarch.comp writes
 * each instance's per-ray iteration count into (Inc1 M4b addition — see the shader's
 * "PER-INSTANCE ITERATION DEBUG BUFFER" comment block).
 *
 * Scene: three equal-size Stored/binary shell bodies on the same octreeIndex,
 * placed collinear along the camera's view ray at increasing distance (occluder
 * closest, target farthest, with a third "control" body even farther still to prove
 * the reject is about occlusion, not merely "last in the array"). A single-pixel
 * (1x1) dispatch aimed exactly down that line means every ray in the (trivial)
 * dispatch passes through all three instances' AABBs.
 *
 * Run: ./test_body_instance_occlusion_reject
 */

#include <gtest/gtest.h>

#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Core/NodeContext.h"
#include "VulkanDevice.h"

#include "ShellOctreeGpu.h"   // Vixen::SVO::BodyInstanceGpu
#include "InstanceSort.h"     // Vixen::SVO::SortInstancesFrontToBack
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"  // VixenSelectWslGpuIcd

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;
using Vixen::Vulkan::Resources::VulkanDevice;

#ifndef GLSL_RAYMARCH_SPV
#error "GLSL_RAYMARCH_SPV (path to compiled BodyInstanceRayMarch.spv) must be defined by CMake"
#endif

namespace {

// Byte-identical to BodyInstanceRayMarch.comp's PushConstants block (see
// test_body_instance_raymarch_render.cpp's own copy for the layout derivation).
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

class BodyInstanceOcclusionRejectTest : public ::testing::Test {
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

    // Real discrete/integrated GPUs are now PREFERRED; software/Dozen is only a
    // fallback when no real GPU is visible.
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
        appInfo.pApplicationName = "test_body_instance_occlusion_reject";
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
        // Prefer a real discrete/integrated GPU; fall back to software/Dozen only
        // when no real GPU is visible.
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

    // Dispatches the real shader at w*h and reads back BOTH the colour image and the
    // per-instance iteration-count debug buffer (binding 14). maxInstances sizes the
    // debug buffer (one uint per slot); instanceIterCounts is resized to maxInstances.
    void RenderAndReadIterCounts(VkBuffer nodesBuf, VkBuffer bricksBuf, VkBuffer materialsBuf,
                                 VkBuffer configBuf, VkBuffer instanceBuf,
                                 const PushConstants& pc, uint32_t w, uint32_t h,
                                 uint32_t maxInstances,
                                 std::vector<uint32_t>& instanceIterCounts) {
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

        // Inc1 M4b: the new per-instance iteration debug buffer (binding 14).
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
        const std::array<VkDescriptorSetLayoutBinding, 13> bindings = {
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
            bind(14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // Inc1 M4b: per-instance iteration debug
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
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11},
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
        const std::array<VkWriteDescriptorSet, 13> writes = {
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

        // Barrier the iteration debug SSBO (shader write -> host read) before copying anything;
        // it's already HOST_VISIBLE|HOST_COHERENT so no copy-to-buffer stage is needed, just a
        // pipeline barrier ordering the shader write ahead of the host map/read below.
        VkBufferMemoryBarrier iterBarrier{};
        iterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        iterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        iterBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        iterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        iterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        iterBarrier.buffer = iterBuf; iterBarrier.offset = 0; iterBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, nullptr, 1, &iterBarrier, 0, nullptr);

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

        vkDeviceWaitIdle(logicalDevice_);
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

constexpr float kBaseRadiusAu  = 0.05f;
constexpr float kWorldGridSize = 10.0f;

Vixen::SVO::BodyInstanceGpu MakeInstance(float x, float y, float z, float scale,
                                         uint32_t octreeIndex, float r, float g, float b) {
    Vixen::SVO::BodyInstanceGpu i{};
    i.worldPos[0] = x; i.worldPos[1] = y; i.worldPos[2] = z;
    i.renderScale = scale; i.octreeIndex = octreeIndex;
    i.color[0] = r; i.color[1] = g; i.color[2] = b;
    return i;
}

glm::vec3 ShaderBodyCentre(const Vixen::SVO::BodyInstanceGpu& inst) {
    const glm::vec3 wp(inst.worldPos[0], inst.worldPos[1], inst.worldPos[2]);
    return wp + glm::vec3(0.5f * kWorldGridSize * inst.renderScale);
}
float ShaderBodyRadius(const Vixen::SVO::BodyInstanceGpu& inst) {
    return 0.5f * kWorldGridSize * inst.renderScale;
}

}  // namespace

// ---------------------------------------------------------------------------
// The decisive test: camera -> occluder -> occluded target, collinear along the
// view ray. Front-to-back sort puts the occluder first in the instance array;
// the shader's gridT.x>bestT reject should skip the occluded target's ESVO
// traversal entirely once the occluder's hit lands in bestT.
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceOcclusionRejectTest, OccludedInstanceHasZeroTraversalIterations) {
    std::cout << "[ lavapipe ] selected physical device: '" << selectedDeviceName_
              << "' (software rasterizer confirmed)\n";
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_occlusion_reject");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // Three same-kind (octree 0 star shell) bodies collinear along an OFF-AXIS view
    // direction (never a pure cardinal axis -- an axis-aligned ray risks grazing an
    // ESVO node/child boundary exactly at dead-center, the same edge case the proven
    // reference render test (test_body_instance_raymarch_render.cpp) avoids by using
    // eye = focus + normalize(0.3, 0.25, 1.0) * distance rather than a pure +Z look).
    // Bodies are placed at increasing multiples of `lineDir` from the origin, spaced
    // far enough apart that the occluder's shell fully covers the ray before it could
    // reach the target/control bodies' shells.
    const glm::vec3 lineDir = glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f));
    const float scale = kBaseRadiusAu * 4.0f;   // larger radius, guarantees full occlusion on-axis
    const float R = 0.5f * kWorldGridSize * scale;  // ShaderBodyRadius: the ACTUAL rendered radius
    // The shader offsets the rendered sphere centre to worldPos + R*(1,1,1) (NOT along the
    // instance's own facing direction -- see ShaderBodyCentre) -- subtract that fixed offset from
    // worldPos so the ACTUAL rendered centre lands exactly on the eye->lineDir ray at distance t.
    auto placeOnLine = [&](float t) {
        const glm::vec3 p = lineDir * t - glm::vec3(R);
        return MakeInstance(p.x, p.y, p.z, scale, 0, 1.0f, 1.0f, 1.0f);
    };
    const std::vector<Vixen::SVO::BodyInstanceGpu> unsorted = {
        // Deliberately NOT pre-sorted here -- control (farthest) listed FIRST in the raw
        // array, to prove the fix depends on the front-to-back SORT, not array order.
        placeOnLine(40.0f),  // control: farthest
        placeOnLine(20.0f),  // target: occluded (middle)
        placeOnLine(0.0f),   // occluder: closest
    };

    // Camera sits behind the occluder along -lineDir, looking down +lineDir --
    // SortInstancesFrontToBack is the CPU mechanism under test here exactly as much
    // as the shader reject is.
    const glm::vec3 eye = -lineDir * 10.0f;
    std::vector<Vixen::SVO::BodyInstanceGpu> instances = unsorted;
    Vixen::SVO::SortInstancesFrontToBack(instances, eye);

    // occluder (t=0) must now be first, target (t=20) second, control (t=40) last.
    ASSERT_NEAR(instances[0].worldPos[0], placeOnLine(0.0f).worldPos[0], 0.01f)
        << "sort did not put the occluder first";
    ASSERT_NEAR(instances[1].worldPos[0], placeOnLine(20.0f).worldPos[0], 0.01f)
        << "sort did not put the target second";
    ASSERT_NEAR(instances[2].worldPos[0], placeOnLine(40.0f).worldPos[0], 0.01f)
        << "sort did not put the control last";

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

    // Single-pixel dispatch (a narrow FOV pinhole aimed exactly down `lineDir` from
    // `eye`) so the one ray traced passes through all three instances' AABBs.
    // getRayDir() ignores fov entirely at dead-center UV (ndc=(0,0) exactly cancels
    // the tanHalfFov term), so pc.fov's value doesn't matter here -- it only needs a
    // valid (non-degenerate) camera basis, built the same way MakeCamera() does in
    // the reference render test (right/up derived from worldUp x dir).
    constexpr uint32_t kW = 1, kH = 1;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(lineDir, worldUp));
    const glm::vec3 up    = glm::normalize(glm::cross(right, lineDir));
    PushConstants pc{};
    pc.cameraPos = eye; pc.time = 0.0f;
    pc.cameraDir = lineDir; pc.fov = 45.0f;
    pc.cameraUp = up;       pc.aspect = 1.0f;
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;
    pc.instanceCount = static_cast<int32_t>(instances.size());

    std::vector<uint32_t> iterCounts;
    ASSERT_NO_FATAL_FAILURE(RenderAndReadIterCounts(
        nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
        pc, kW, kH, static_cast<uint32_t>(instances.size()), iterCounts));

    ASSERT_EQ(iterCounts.size(), 3u);
    std::printf("[OCCLUSION] iteration counts: occluder(slot0)=%u target(slot1)=%u control(slot2)=%u\n",
                iterCounts[0], iterCounts[1], iterCounts[2]);

    // The occluder (nearest, slot 0) always runs its full traversal -- nothing is in
    // front of it yet, bestT is still 1e30 when its AABB check runs.
    EXPECT_GT(iterCounts[0], 0u) << "occluder itself should still traverse normally";

    // THE decisive assertion: once the occluder's hit is recorded in bestT, both the
    // target (slot 1) AND the farther control (slot 2) must be rejected before their
    // ESVO traversal runs at all -- zero iterations, not merely a discarded hit.
    EXPECT_EQ(iterCounts[1], 0u)
        << "occluded target ran " << iterCounts[1] << " traversal iterations; "
           "expected 0 (gridT.x > bestT reject should skip it entirely)";
    EXPECT_EQ(iterCounts[2], 0u)
        << "occluded control ran " << iterCounts[2] << " traversal iterations; expected 0";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}

// ---------------------------------------------------------------------------
// Control: WITHOUT occlusion (bodies spread apart, none in front of another
// along the ray), all instances should still run non-zero iterations -- proves
// the reject is occlusion-specific, not an over-broad skip.
// ---------------------------------------------------------------------------
TEST_F(BodyInstanceOcclusionRejectTest, NonOccludedInstancesStillTraverse) {
    ASSERT_TRUE(softwareConfirmed_);

    using C = BodyOctreeSceneNodeConfig;
    BodyOctreeSceneNodeType nodeType("BodyOctreeScene");
    auto nodeBase = nodeType.CreateInstance("body_no_occlusion");
    auto* node = dynamic_cast<BodyOctreeSceneNode*>(nodeBase.get());
    ASSERT_NE(node, nullptr);

    Resource deviceRes; SetHandleVal<VulkanDevice*>(deviceRes, deviceShell_.get());
    Resource poolRes;   SetHandleVal<VkCommandPool>(poolRes, commandPool_);
    Resource frameRes;  uint32_t frameIndex = 0; SetHandleVal<uint32_t>(frameRes, frameIndex);
    node->SetInput(C::VULKAN_DEVICE_IN_Slot::index,    0, &deviceRes);
    node->SetInput(C::COMMAND_POOL_Slot::index,        0, &poolRes);
    node->SetInput(C::CURRENT_FRAME_INDEX_Slot::index, 0, &frameRes);

    // A single body, alone on the ray -- no possible occluder, so its slot-0 traversal
    // must be non-zero (sanity check that the harness itself renders correctly and the
    // reject does not fire spuriously with only one instance). Off-axis line direction
    // for the same reason as the decisive test above (avoid an axis-aligned ESVO
    // node-boundary edge case at dead-center).
    const glm::vec3 lineDir = glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f));
    const float scale = kBaseRadiusAu * 4.0f;
    const float R = 0.5f * kWorldGridSize * scale;
    const glm::vec3 bodyPos = lineDir * 20.0f - glm::vec3(R);   // see decisive test's comment
    std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
        MakeInstance(bodyPos.x, bodyPos.y, bodyPos.z, scale, 0, 1.0f, 1.0f, 1.0f),
    };
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
    ASSERT_NE(instanceBuf, VK_NULL_HANDLE);

    constexpr uint32_t kW = 1, kH = 1;
    const glm::vec3 eye = -lineDir * 10.0f;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(lineDir, worldUp));
    const glm::vec3 up    = glm::normalize(glm::cross(right, lineDir));
    PushConstants pc{};
    pc.cameraPos = eye; pc.time = 0.0f;
    pc.cameraDir = lineDir; pc.fov = 45.0f;
    pc.cameraUp = up;       pc.aspect = 1.0f;
    pc.cameraRight = right; pc.debugMode = 0;
    pc.raySizeCoef = 0.0f; pc.raySizeBias = 0.0f;
    pc.instanceCount = static_cast<int32_t>(instances.size());

    std::vector<uint32_t> iterCounts;
    ASSERT_NO_FATAL_FAILURE(RenderAndReadIterCounts(
        nodesBuf, bricksBuf, materialsBuf, configBuf, instanceBuf,
        pc, kW, kH, static_cast<uint32_t>(instances.size()), iterCounts));

    ASSERT_EQ(iterCounts.size(), 1u);
    std::printf("[NO-OCCLUSION] single-instance iteration count=%u\n", iterCounts[0]);
    EXPECT_GT(iterCounts[0], 0u) << "sole instance should traverse normally (no occluder present)";

    vkDeviceWaitIdle(logicalDevice_);
    node->Cleanup(CleanupReason::FinalTeardown);
    nodeBase.reset();
}
