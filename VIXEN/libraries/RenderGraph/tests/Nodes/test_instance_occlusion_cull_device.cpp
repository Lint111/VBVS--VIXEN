/**
 * @file test_instance_occlusion_cull_device.cpp
 * @brief Raster-proxy B1 M3 device gate: dispatch the SHIPPED
 * shaders/InstanceOcclusionCull.comp against synthetic buffers and assert the
 * skip-mask words — both against hand-expected bits AND against the CPU
 * mirror's CullMaskWord on the identical inputs (the gpu-shader-debug parity
 * proof; the mirror's own 15 unit tests carry the math coverage).
 *
 * Scene = the mirror suite's canonical trio on a uniform distance-4 wall
 * (16x16 tile image cleared to 4.0):
 *   inst0: box at z in [5, 5.5], nearest 5.0  -> occluded (bit 0)
 *   inst1: same box at z in [2, 2.5], nearest 2.0 -> in FRONT of the wall (clear)
 *   inst2: inst0 shifted +x one tile           -> occluded (bit 2)
 * skipMask word 0 pre-seeded with bit 5 (the bucketed-dispatch CPU writer's
 * bit). B1 writes its camera-visibility result to the separate high region;
 * the low ownership bit must survive untouched.
 *
 * DEVICE SELECTION: same contract as test_recipe_instance_bucketing.cpp — real
 * GPU preferred, lavapipe/Dozen fallback, some usable device hard-asserted.
 */

#include <gtest/gtest.h>

#include "Headers.h"  // MUST be first to define GLM_FORCE_DEPTH_ZERO_TO_ONE (mirrors CameraNode.cpp)
#include "Generated/OctreeConfig.g.h"            // Vixen::Gpu::OctreeConfig (432 B, static_asserted)
#include "Nodes/InstanceOcclusionCullMirror.h"   // the CPU mirror under parity test
#include "ShellOctreeGpu.h"                      // Vixen::SVO::BodyInstanceGpu
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"                   // VixenSelectWslGpuIcd
#include "VulkanDevice.h"

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using Vixen::RenderGraph::Mirror::CullInstance;
using Vixen::RenderGraph::Mirror::CullMaskWord;
using Vixen::RenderGraph::Mirror::CullOctreeConfig;
using Vixen::RenderGraph::Mirror::CullParams;

namespace {

// Byte-identical to the shader's push_constant block.
struct CullPush {
    glm::mat4 prevViewProj;
    glm::vec4 prevCamPos;
    uint32_t dims[4];  // srcWidth, srcHeight, instanceCount, pad
};
static_assert(sizeof(CullPush) == 96, "CullPush must match the shader's 96-byte block");

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

constexpr uint32_t kSrcW = 256, kSrcH = 256;  // depth extent the tile grid derives from
constexpr uint32_t kTiles = 16;               // HiZTileCount(256)
constexpr uint32_t kWords = 2 * Vixen::RenderGraph::Mirror::kInstanceMaskWordCount;
constexpr uint32_t kCameraWordBase = Vixen::RenderGraph::Mirror::kCameraVisibilityMaskWordBase;

}  // namespace

class InstanceOcclusionCullDeviceTest : public ::testing::Test {
protected:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         logicalDevice_  = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    std::string      selectedDeviceName_;
    bool             deviceConfirmed_ = false;

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
        appInfo.pApplicationName = "test_instance_occlusion_cull_device";
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

        ASSERT_NO_FATAL_FAILURE(PickPhysicalDevice());
        ASSERT_TRUE(deviceConfirmed_)
            << "Refusing to run: no usable Vulkan device found (real GPU, software "
               "rasterizer, or Dozen); nearest was '" << selectedDeviceName_ << "'.";

        ASSERT_NO_FATAL_FAILURE(CreateLogicalDevice());
        ASSERT_NO_FATAL_FAILURE(CreateCommandPool());
    }

    void TearDown() override {
        if (commandPool_ != VK_NULL_HANDLE && logicalDevice_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (logicalDevice_ != VK_NULL_HANDLE) { vkDestroyDevice(logicalDevice_, nullptr); logicalDevice_ = VK_NULL_HANDLE; }
        if (instance_ != VK_NULL_HANDLE) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
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
            if (IsRealGpu(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; return;
            }
        }
        for (VkPhysicalDevice dev : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);
            if (LooksLikeSoftware(props)) {
                physicalDevice_ = dev; selectedDeviceName_ = props.deviceName;
                deviceConfirmed_ = true; return;
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
            if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily_ = i; found = true; break; }
        }
        ASSERT_TRUE(found) << "No compute queue family on the selected device";

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily_; qInfo.queueCount = 1; qInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo dInfo{};
        dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dInfo.queueCreateInfoCount = 1; dInfo.pQueueCreateInfos = &qInfo;
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

    void UploadBuffer(VkDeviceMemory mem, const void* data, VkDeviceSize size) {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, mem, 0, size, 0, &m), VK_SUCCESS);
        std::memcpy(m, data, static_cast<size_t>(size));
        vkUnmapMemory(logicalDevice_, mem);
    }
};

TEST_F(InstanceOcclusionCullDeviceTest, MaskWordsMatchMirrorOnSyntheticScene) {
    std::cout << "[ cull ] selected physical device: '" << selectedDeviceName_ << "'\n";

    // --- Synthetic scene (the mirror suite's canonical trio) ---------------
    const glm::mat4 prevViewProj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 100.0f);

    Vixen::SVO::BodyInstanceGpu instances[3] = {};
    for (auto& inst : instances) {
        inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 5.0f;
        inst.renderScale = 1.0f;
        inst.octreeIndex = 0u;
        inst.providerKind = 0u;
    }
    instances[1].worldPos[2] = 2.0f;   // in front of the wall -> visible
    instances[2].worldPos[0] = 0.125f; // one tile right -> still occluded

    Vixen::Gpu::OctreeConfig config{};
    config.traceBoundsMinX = 0.0f;   config.traceBoundsMinY = 0.0f;  config.traceBoundsMinZ = 0.0f;
    config.traceBoundsMaxX = 0.125f; config.traceBoundsMaxY = 0.125f; config.traceBoundsMaxZ = 0.5f;
    config.localToWorld = glm::mat4(1.0f);
    config.worldToLocal = glm::mat4(1.0f);

    uint32_t seededMask[kWords] = {};
    seededMask[0] = 1u << 5;  // the bucketed-dispatch CPU writer's bit — must survive

    CullPush push{};
    push.prevViewProj = prevViewProj;
    push.prevCamPos   = glm::vec4(0.0f);
    push.dims[0] = kSrcW; push.dims[1] = kSrcH; push.dims[2] = 3u; push.dims[3] = 0u;

    // --- Mirror expectation on the IDENTICAL inputs ------------------------
    CullInstance mInsts[3] = {};
    for (int i = 0; i < 3; ++i) {
        mInsts[i].worldPos = glm::vec3(instances[i].worldPos[0], instances[i].worldPos[1],
                                       instances[i].worldPos[2]);
        mInsts[i].renderScale = instances[i].renderScale;
        mInsts[i].octreeIndex = instances[i].octreeIndex;
        mInsts[i].providerKind = instances[i].providerKind;
    }
    CullOctreeConfig mCfg{};
    mCfg.traceBoundsMin = glm::vec3(0.0f);
    mCfg.traceBoundsMax = glm::vec3(0.125f, 0.125f, 0.5f);
    mCfg.localToWorld = glm::mat4(1.0f);
    CullParams mParams{};
    mParams.prevViewProj = prevViewProj;
    mParams.prevCamPos = glm::vec3(0.0f);
    mParams.srcWidth = kSrcW; mParams.srcHeight = kSrcH;
    mParams.instanceCount = 3u;
    std::vector<float> mTiles(kTiles * kTiles, 4.0f);
    const uint32_t mirrorCameraWord0 =
        CullMaskWord(0u, mInsts, &mCfg, mTiles.data(), mParams, 0u);
    ASSERT_EQ(mirrorCameraWord0, (1u << 0) | (1u << 2))
        << "mirror precondition drifted — fix the mirror suite first";

    // --- Device resources --------------------------------------------------
    VkBuffer instBuf{}, cfgBuf{}, maskBuf{};
    VkDeviceMemory instMem{}, cfgMem{}, maskMem{};
    CreateHostBuffer(sizeof(instances), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instBuf, instMem, false);
    CreateHostBuffer(sizeof(config), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, cfgBuf, cfgMem, false);
    CreateHostBuffer(sizeof(seededMask), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maskBuf, maskMem, false);
    UploadBuffer(instMem, instances, sizeof(instances));
    UploadBuffer(cfgMem, &config, sizeof(config));
    UploadBuffer(maskMem, seededMask, sizeof(seededMask));

    // Tile-max image: 16x16 R32_SFLOAT storage, cleared to the 4.0 wall.
    VkImage tileImage{}; VkDeviceMemory tileMem{}; VkImageView tileView{};
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R32_SFLOAT;
        ici.extent = {kTiles, kTiles, 1};
        ici.mipLevels = 1; ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ASSERT_EQ(vkCreateImage(logicalDevice_, &ici, nullptr, &tileImage), VK_SUCCESS);
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(logicalDevice_, tileImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        ASSERT_NE(ai.memoryTypeIndex, UINT32_MAX);
        ASSERT_EQ(vkAllocateMemory(logicalDevice_, &ai, nullptr, &tileMem), VK_SUCCESS);
        ASSERT_EQ(vkBindImageMemory(logicalDevice_, tileImage, tileMem, 0), VK_SUCCESS);

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = tileImage;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R32_SFLOAT;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ASSERT_EQ(vkCreateImageView(logicalDevice_, &vci, nullptr, &tileView), VK_SUCCESS);
    }

    // --- Pipeline ----------------------------------------------------------
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

    VkDescriptorBufferInfo bufInfos[3] = {
        {instBuf, 0, VK_WHOLE_SIZE}, {cfgBuf, 0, VK_WHOLE_SIZE}, {maskBuf, 0, VK_WHOLE_SIZE}};
    VkDescriptorImageInfo imgInfo{VK_NULL_HANDLE, tileView, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[4] = {};
    const uint32_t bufBindings[3] = {0u, 1u, 3u};
    for (int i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set; writes[i].dstBinding = bufBindings[i];
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufInfos[i];
    }
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = set; writes[3].dstBinding = 2u;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(logicalDevice_, 4, writes, 0, nullptr);

    // --- Record: clear tile image to the wall, dispatch, read back ---------
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool_; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    ASSERT_EQ(vkAllocateCommandBuffers(logicalDevice_, &cbai, &cmd), VK_SUCCESS);

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    ASSERT_EQ(vkBeginCommandBuffer(cmd, &cbbi), VK_SUCCESS);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = tileImage;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkClearColorValue wall{};
    wall.float32[0] = 4.0f;  // uniform distance-4 wall in every tile
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, tileImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &wall, 1, &range);

    VkImageMemoryBarrier toGeneral = toDst;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toGeneral);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                            0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPush), &push);
    vkCmdDispatch(cmd, 1, 1, 1);  // 64 threads ≥ 6 words

    VkMemoryBarrier toHost{};
    toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    toHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &toHost, 0, nullptr, 0, nullptr);
    ASSERT_EQ(vkEndCommandBuffer(cmd), VK_SUCCESS);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    ASSERT_EQ(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    uint32_t result[kWords] = {};
    {
        void* m = nullptr;
        ASSERT_EQ(vkMapMemory(logicalDevice_, maskMem, 0, sizeof(result), 0, &m), VK_SUCCESS);
        std::memcpy(result, m, sizeof(result));
        vkUnmapMemory(logicalDevice_, maskMem);
    }

    // The parity proof: shader == mirror on identical inputs in the camera region;
    // the independent low ownership region is preserved byte-for-byte.
    EXPECT_EQ(result[0], seededMask[0]);
    EXPECT_EQ(result[kCameraWordBase], mirrorCameraWord0);
    for (uint32_t w = 1; w < kWords; ++w) {
        if (w == kCameraWordBase) continue;
        EXPECT_EQ(result[w], 0u) << "word " << w << " must stay untouched";
    }

    // --- Cleanup -----------------------------------------------------------
    vkDestroyPipeline(logicalDevice_, pipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice_, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(logicalDevice_, pool, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice_, dsl, nullptr);
    vkDestroyShaderModule(logicalDevice_, shader, nullptr);
    vkDestroyImageView(logicalDevice_, tileView, nullptr);
    vkDestroyImage(logicalDevice_, tileImage, nullptr);
    vkFreeMemory(logicalDevice_, tileMem, nullptr);
    vkDestroyBuffer(logicalDevice_, instBuf, nullptr);  vkFreeMemory(logicalDevice_, instMem, nullptr);
    vkDestroyBuffer(logicalDevice_, cfgBuf, nullptr);   vkFreeMemory(logicalDevice_, cfgMem, nullptr);
    vkDestroyBuffer(logicalDevice_, maskBuf, nullptr);  vkFreeMemory(logicalDevice_, maskMem, nullptr);
}
