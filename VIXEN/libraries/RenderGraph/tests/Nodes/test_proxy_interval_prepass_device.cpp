/**
 * @file test_proxy_interval_prepass_device.cpp
 * @brief B2 device parity: run fragment-store and compute writers for two
 * proxy AABBs, then byte-compare both 32-byte records with the CPU mirror.
 */

#include <gtest/gtest.h>

#include "Headers.h"  // GLM_FORCE_DEPTH_ZERO_TO_ONE must precede GLM headers.
#include "Generated/OctreeConfig.g.h"
#include "Nodes/ProxyIntervalPrepassMirror.h"
#include "ShellDerive.h"
#include "ShellOctreeGpu.h"
#include "TestVkValidation.h"
#include "VulkanGlobalNames.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Vixen::RenderGraph::Mirror::AccumulateProxyAabb;
using Vixen::RenderGraph::Mirror::ClearProxyIntervalPixel;
using Vixen::RenderGraph::Mirror::ProxyIntervalPixel;
using Vixen::RenderGraph::Mirror::ProxyRay;

struct alignas(16) ProxyRasterPush {
    glm::mat4 viewProj;
    glm::vec4 cameraPosFov;
    glm::vec4 cameraDirAspect;
    glm::vec4 cameraUpWidth;
    glm::vec4 cameraRightHeight;
};
static_assert(sizeof(ProxyRasterPush) == 128);

std::vector<uint32_t> ReadSpirv(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const std::streamsize size = stream.tellg();
    if (size <= 0 || size % 4 != 0) return {};
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4u);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}

struct BufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

class ProxyIntervalPrepassDeviceTest : public ::testing::Test {
protected:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0u;
    std::string deviceName_;

    std::vector<BufferAllocation> buffers_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule vertexModule_ = VK_NULL_HANDLE;
    VkShaderModule fragmentModule_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool computeDescriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline computePipeline_ = VK_NULL_HANDLE;
    VkShaderModule computeModule_ = VK_NULL_HANDLE;

    static bool IsUsableDevice(const VkPhysicalDeviceProperties& properties) {
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            return true;
        }
        std::string name(properties.deviceName);
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return name.find("lavapipe") != std::string::npos ||
               name.find("llvmpipe") != std::string::npos ||
               name.find("direct3d12") != std::string::npos;
    }

    void SetUp() override {
        VixenSelectWslGpuIcd();

        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "test_proxy_interval_prepass_device";
        application.apiVersion = VK_API_VERSION_1_3;
        const auto layers = EnabledValidationLayers();
        const char* extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &application;
        createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        createInfo.enabledExtensionCount = 1u;
        createInfo.ppEnabledExtensionNames = extensions;
        ASSERT_EQ(vkCreateInstance(&createInfo, nullptr, &instance_), VK_SUCCESS);

        uint32_t deviceCount = 0u;
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), VK_SUCCESS);
        ASSERT_GT(deviceCount, 0u);
        std::vector<VkPhysicalDevice> devices(deviceCount);
        ASSERT_EQ(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()), VK_SUCCESS);
        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (!IsUsableDevice(properties)) continue;
            uint32_t familyCount = 0u;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (uint32_t family = 0u; family < familyCount; ++family) {
                if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
                    physicalDevice_ = candidate;
                    queueFamily_ = family;
                    deviceName_ = properties.deviceName;
                    break;
                }
            }
            if (physicalDevice_ != VK_NULL_HANDLE) break;
        }
        ASSERT_NE(physicalDevice_, VK_NULL_HANDLE) << "No usable graphics Vulkan device";

        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
        ASSERT_EQ(supported.fragmentStoresAndAtomics, VK_TRUE);
        VkPhysicalDeviceFeatures enabled{};
        enabled.fragmentStoresAndAtomics = VK_TRUE;
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = queueFamily_;
        queueInfo.queueCount = 1u;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.queueCreateInfoCount = 1u;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.pEnabledFeatures = &enabled;
        ASSERT_EQ(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_), VK_SUCCESS);
        vkGetDeviceQueue(device_, queueFamily_, 0u, &queue_);

        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily_;
        ASSERT_EQ(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), VK_SUCCESS);
    }

    void TearDown() override {
        if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (computePipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, computePipeline_, nullptr);
        if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (computePipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);
        if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (computeDescriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, computeDescriptorPool_, nullptr);
        if (descriptorSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        if (computeDescriptorSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, computeDescriptorSetLayout_, nullptr);
        if (vertexModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertexModule_, nullptr);
        if (fragmentModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, fragmentModule_, nullptr);
        if (computeModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, computeModule_, nullptr);
        if (framebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);
        if (imageView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, imageView_, nullptr);
        if (image_ != VK_NULL_HANDLE) vkDestroyImage(device_, image_, nullptr);
        if (imageMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, imageMemory_, nullptr);
        for (const BufferAllocation& allocation : buffers_) {
            if (allocation.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, allocation.buffer, nullptr);
            if (allocation.memory != VK_NULL_HANDLE) vkFreeMemory(device_, allocation.memory, nullptr);
        }
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);
        if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
    }

    uint32_t FindMemoryType(uint32_t bits, VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &properties);
        for (uint32_t index = 0u; index < properties.memoryTypeCount; ++index) {
            if ((bits & (1u << index)) != 0u &&
                (properties.memoryTypes[index].propertyFlags & required) == required) {
                return index;
            }
        }
        return UINT32_MAX;
    }

    BufferAllocation CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                      const void* initialData) {
        BufferAllocation allocation{};
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        EXPECT_EQ(vkCreateBuffer(device_, &bufferInfo, nullptr, &allocation.buffer), VK_SUCCESS);
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, allocation.buffer, &requirements);
        VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memoryInfo.allocationSize = requirements.size;
        memoryInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        EXPECT_NE(memoryInfo.memoryTypeIndex, UINT32_MAX);
        EXPECT_EQ(vkAllocateMemory(device_, &memoryInfo, nullptr, &allocation.memory), VK_SUCCESS);
        EXPECT_EQ(vkBindBufferMemory(device_, allocation.buffer, allocation.memory, 0u), VK_SUCCESS);
        void* mapped = nullptr;
        EXPECT_EQ(vkMapMemory(device_, allocation.memory, 0u, size, 0u, &mapped), VK_SUCCESS);
        if (initialData) std::memcpy(mapped, initialData, static_cast<size_t>(size));
        else std::memset(mapped, 0, static_cast<size_t>(size));
        vkUnmapMemory(device_, allocation.memory);
        buffers_.push_back(allocation);
        return allocation;
    }

    VkShaderModule CreateShaderModule(const char* path) {
        const std::vector<uint32_t> code = ReadSpirv(path);
        EXPECT_FALSE(code.empty()) << "Missing SPIR-V: " << path;
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size() * sizeof(uint32_t);
        info.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        EXPECT_EQ(vkCreateShaderModule(device_, &info, nullptr, &module), VK_SUCCESS);
        return module;
    }
};

TEST_F(ProxyIntervalPrepassDeviceTest, RasterAndComputeRecordsMatchMirrorByteForByte) {
    std::cout << "[ proxy interval ] selected physical device: '" << deviceName_ << "'\n";

    std::array<Vixen::SVO::ShellProxyAabb, 2> proxies{};
    proxies[0].minLocal[0] = -1.0f; proxies[0].minLocal[1] = -1.0f; proxies[0].minLocal[2] = 0.0f;
    proxies[0].maxLocal[0] = 1.0f; proxies[0].maxLocal[1] = 1.0f; proxies[0].maxLocal[2] = 1.0f;
    proxies[0].octreeIndex = 0u;
    proxies[1] = proxies[0];
    proxies[1].minLocal[2] = 2.0f; proxies[1].maxLocal[2] = 3.0f;
    proxies[1].octreeIndex = 1u;

    std::array<Vixen::SVO::BodyInstanceGpu, 2> instances{};
    for (uint32_t index = 0u; index < static_cast<uint32_t>(instances.size()); ++index) {
        instances[index].renderScale = 1.0f;
        instances[index].octreeIndex = index;
        instances[index].providerKind = 0u;
    }
    std::array<Vixen::Gpu::OctreeConfig, 2> configs{};
    for (auto& config : configs) {
        config.localToWorld = glm::mat4(1.0f);
        config.worldToLocal = glm::mat4(1.0f);
    }

    const BufferAllocation proxyBuffer = CreateHostBuffer(
        sizeof(proxies), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, proxies.data());
    const BufferAllocation instanceBuffer = CreateHostBuffer(
        sizeof(instances), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instances.data());
    const BufferAllocation configBuffer = CreateHostBuffer(
        sizeof(configs), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, configs.data());
    const BufferAllocation rasterResultBuffer = CreateHostBuffer(
        sizeof(ProxyIntervalPixel),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);
    const BufferAllocation computeResultBuffer = CreateHostBuffer(
        sizeof(ProxyIntervalPixel),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, nullptr);

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {1u, 1u, 1u};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ASSERT_EQ(vkCreateImage(device_, &imageInfo, nullptr, &image_), VK_SUCCESS);
    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(device_, image_, &imageRequirements);
    VkMemoryAllocateInfo imageAllocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    imageAllocation.allocationSize = imageRequirements.size;
    imageAllocation.memoryTypeIndex = FindMemoryType(
        imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ASSERT_NE(imageAllocation.memoryTypeIndex, UINT32_MAX);
    ASSERT_EQ(vkAllocateMemory(device_, &imageAllocation, nullptr, &imageMemory_), VK_SUCCESS);
    ASSERT_EQ(vkBindImageMemory(device_, image_, imageMemory_, 0u), VK_SUCCESS);
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    ASSERT_EQ(vkCreateImageView(device_, &viewInfo, nullptr, &imageView_), VK_SUCCESS);

    VkAttachmentDescription attachment{};
    attachment.format = imageInfo.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference colorReference{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &colorReference;
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = 1u;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1u;
    renderPassInfo.pSubpasses = &subpass;
    ASSERT_EQ(vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_), VK_SUCCESS);
    VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 1u;
    framebufferInfo.pAttachments = &imageView_;
    framebufferInfo.width = 1u; framebufferInfo.height = 1u; framebufferInfo.layers = 1u;
    ASSERT_EQ(vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer_), VK_SUCCESS);

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (uint32_t index = 0u; index < static_cast<uint32_t>(bindings.size()); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[index].descriptorCount = 1u;
        bindings[index].stageFlags = index == 3u ? VK_SHADER_STAGE_FRAGMENT_BIT
                                                 : VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setLayoutInfo.pBindings = bindings.data();
    ASSERT_EQ(vkCreateDescriptorSetLayout(device_, &setLayoutInfo, nullptr,
                                          &descriptorSetLayout_), VK_SUCCESS);
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(ProxyRasterPush);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1u;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1u;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    ASSERT_EQ(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
                                     &pipelineLayout_), VK_SUCCESS);

    vertexModule_ = CreateShaderModule(PROXY_INTERVAL_VERTEX_SPV);
    fragmentModule_ = CreateShaderModule(PROXY_INTERVAL_FRAGMENT_SPV);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u,
                 VK_SHADER_STAGE_VERTEX_BIT, vertexModule_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule_, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1u; viewportState.scissorCount = 1u;
    VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = 0u;
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1u;
    colorBlend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2u; dynamic.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    ASSERT_EQ(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1u, &pipelineInfo,
                                        nullptr, &pipeline_), VK_SUCCESS);

    std::array<VkDescriptorSetLayoutBinding, 4> computeBindings{};
    for (uint32_t index = 0u; index < static_cast<uint32_t>(computeBindings.size()); ++index) {
        computeBindings[index].binding = index;
        computeBindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeBindings[index].descriptorCount = 1u;
        computeBindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo computeSetLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    computeSetLayoutInfo.bindingCount = static_cast<uint32_t>(computeBindings.size());
    computeSetLayoutInfo.pBindings = computeBindings.data();
    ASSERT_EQ(vkCreateDescriptorSetLayout(device_, &computeSetLayoutInfo, nullptr,
                                          &computeDescriptorSetLayout_), VK_SUCCESS);
    VkPushConstantRange computePushRange{};
    computePushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    computePushRange.size = sizeof(ProxyRasterPush);
    VkPipelineLayoutCreateInfo computePipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    computePipelineLayoutInfo.setLayoutCount = 1u;
    computePipelineLayoutInfo.pSetLayouts = &computeDescriptorSetLayout_;
    computePipelineLayoutInfo.pushConstantRangeCount = 1u;
    computePipelineLayoutInfo.pPushConstantRanges = &computePushRange;
    ASSERT_EQ(vkCreatePipelineLayout(device_, &computePipelineLayoutInfo, nullptr,
                                     &computePipelineLayout_), VK_SUCCESS);
    computeModule_ = CreateShaderModule(PROXY_INTERVAL_COMPUTE_SPV);
    VkPipelineShaderStageCreateInfo computeStage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    computeStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeStage.module = computeModule_;
    computeStage.pName = "main";
    VkComputePipelineCreateInfo computePipelineInfo{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    computePipelineInfo.stage = computeStage;
    computePipelineInfo.layout = computePipelineLayout_;
    ASSERT_EQ(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1u,
                                       &computePipelineInfo, nullptr,
                                       &computePipeline_), VK_SUCCESS);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4u};
    VkDescriptorPoolCreateInfo descriptorPoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPoolInfo.maxSets = 1u;
    descriptorPoolInfo.poolSizeCount = 1u;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    ASSERT_EQ(vkCreateDescriptorPool(device_, &descriptorPoolInfo, nullptr,
                                     &descriptorPool_), VK_SUCCESS);
    VkDescriptorSetAllocateInfo setAllocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setAllocate.descriptorPool = descriptorPool_;
    setAllocate.descriptorSetCount = 1u;
    setAllocate.pSetLayouts = &descriptorSetLayout_;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateDescriptorSets(device_, &setAllocate, &descriptorSet), VK_SUCCESS);
    const std::array<VkDescriptorBufferInfo, 4> bufferInfos = {{
        {proxyBuffer.buffer, 0u, VK_WHOLE_SIZE},
        {instanceBuffer.buffer, 0u, VK_WHOLE_SIZE},
        {configBuffer.buffer, 0u, VK_WHOLE_SIZE},
        {rasterResultBuffer.buffer, 0u, VK_WHOLE_SIZE},
    }};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (uint32_t index = 0u; index < static_cast<uint32_t>(writes.size()); ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = descriptorSet;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1u;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &bufferInfos[index];
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0u, nullptr);

    VkDescriptorPoolCreateInfo computeDescriptorPoolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    computeDescriptorPoolInfo.maxSets = 1u;
    computeDescriptorPoolInfo.poolSizeCount = 1u;
    computeDescriptorPoolInfo.pPoolSizes = &poolSize;
    ASSERT_EQ(vkCreateDescriptorPool(device_, &computeDescriptorPoolInfo, nullptr,
                                     &computeDescriptorPool_), VK_SUCCESS);
    VkDescriptorSetAllocateInfo computeSetAllocate{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    computeSetAllocate.descriptorPool = computeDescriptorPool_;
    computeSetAllocate.descriptorSetCount = 1u;
    computeSetAllocate.pSetLayouts = &computeDescriptorSetLayout_;
    VkDescriptorSet computeDescriptorSet = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateDescriptorSets(device_, &computeSetAllocate,
                                       &computeDescriptorSet), VK_SUCCESS);
    const std::array<VkDescriptorBufferInfo, 4> computeBufferInfos = {{
        {proxyBuffer.buffer, 0u, VK_WHOLE_SIZE},
        {instanceBuffer.buffer, 0u, VK_WHOLE_SIZE},
        {configBuffer.buffer, 0u, VK_WHOLE_SIZE},
        {computeResultBuffer.buffer, 0u, VK_WHOLE_SIZE},
    }};
    std::array<VkWriteDescriptorSet, 4> computeWrites{};
    for (uint32_t index = 0u;
         index < static_cast<uint32_t>(computeWrites.size()); ++index) {
        computeWrites[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[index].dstSet = computeDescriptorSet;
        computeWrites[index].dstBinding = index;
        computeWrites[index].descriptorCount = 1u;
        computeWrites[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[index].pBufferInfo = &computeBufferInfos[index];
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(computeWrites.size()),
                           computeWrites.data(), 0u, nullptr);

    ProxyRasterPush push{};
    const glm::vec3 cameraPosition(0.0f, 0.0f, -3.0f);
    const glm::vec3 cameraDirection(0.0f, 0.0f, 1.0f);
    const glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 cameraRight(1.0f, 0.0f, 0.0f);
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    push.viewProj = projection * glm::lookAt(cameraPosition,
                                             cameraPosition + cameraDirection, cameraUp);
    push.cameraPosFov = glm::vec4(cameraPosition, 90.0f);
    push.cameraDirAspect = glm::vec4(cameraDirection, 1.0f);
    push.cameraUpWidth = glm::vec4(cameraUp, static_cast<float>(proxies.size()));
    push.cameraRightHeight = glm::vec4(cameraRight, std::bit_cast<float>(0x00010001u));

    VkCommandBufferAllocateInfo commandAllocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandAllocate.commandPool = commandPool_;
    commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocate.commandBufferCount = 1u;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    ASSERT_EQ(vkAllocateCommandBuffers(device_, &commandAllocate, &commandBuffer), VK_SUCCESS);
    VkCommandBufferBeginInfo commandBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    ASSERT_EQ(vkBeginCommandBuffer(commandBuffer, &commandBegin), VK_SUCCESS);
    vkCmdFillBuffer(commandBuffer, rasterResultBuffer.buffer, 0u, VK_WHOLE_SIZE, 0u);
    vkCmdFillBuffer(commandBuffer, computeResultBuffer.buffer, 0u, VK_WHOLE_SIZE, 0u);
    std::array<VkBufferMemoryBarrier, 2> fillBarriers{};
    for (VkBufferMemoryBarrier& barrier : fillBarriers) {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0u;
        barrier.size = VK_WHOLE_SIZE;
    }
    fillBarriers[0].buffer = rasterResultBuffer.buffer;
    fillBarriers[1].buffer = computeResultBuffer.buffer;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0u, 0u, nullptr,
                         static_cast<uint32_t>(fillBarriers.size()),
                         fillBarriers.data(), 0u, nullptr);
    VkRenderPassBeginInfo renderBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderBegin.renderPass = renderPass_;
    renderBegin.framebuffer = framebuffer_;
    renderBegin.renderArea.extent = {1u, 1u};
    vkCmdBeginRenderPass(commandBuffer, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {1u, 1u}};
    vkCmdSetViewport(commandBuffer, 0u, 1u, &viewport);
    vkCmdSetScissor(commandBuffer, 0u, 1u, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0u, 1u, &descriptorSet, 0u, nullptr);
    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u, sizeof(push), &push);
    vkCmdDraw(commandBuffer, 36u,
              static_cast<uint32_t>(proxies.size() * instances.size()), 0u, 0u);
    vkCmdEndRenderPass(commandBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipelineLayout_, 0u, 1u,
                            &computeDescriptorSet, 0u, nullptr);
    vkCmdPushConstants(commandBuffer, computePipelineLayout_,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(push), &push);
    vkCmdDispatch(commandBuffer, 1u, 1u,
                  static_cast<uint32_t>(instances.size()));

    std::array<VkBufferMemoryBarrier, 2> readBarriers{};
    for (VkBufferMemoryBarrier& barrier : readBarriers) {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0u;
        barrier.size = VK_WHOLE_SIZE;
    }
    readBarriers[0].buffer = rasterResultBuffer.buffer;
    readBarriers[1].buffer = computeResultBuffer.buffer;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0u,
                         0u, nullptr,
                         static_cast<uint32_t>(readBarriers.size()),
                         readBarriers.data(), 0u, nullptr);
    ASSERT_EQ(vkEndCommandBuffer(commandBuffer), VK_SUCCESS);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1u;
    submit.pCommandBuffers = &commandBuffer;
    ASSERT_EQ(vkQueueSubmit(queue_, 1u, &submit, VK_NULL_HANDLE), VK_SUCCESS);
    ASSERT_EQ(vkQueueWaitIdle(queue_), VK_SUCCESS);

    ProxyIntervalPixel rasterActual{};
    void* mapped = nullptr;
    ASSERT_EQ(vkMapMemory(device_, rasterResultBuffer.memory, 0u,
                          sizeof(rasterActual), 0u, &mapped), VK_SUCCESS);
    std::memcpy(&rasterActual, mapped, sizeof(rasterActual));
    vkUnmapMemory(device_, rasterResultBuffer.memory);
    ProxyIntervalPixel computeActual{};
    ASSERT_EQ(vkMapMemory(device_, computeResultBuffer.memory, 0u,
                          sizeof(computeActual), 0u, &mapped), VK_SUCCESS);
    std::memcpy(&computeActual, mapped, sizeof(computeActual));
    vkUnmapMemory(device_, computeResultBuffer.memory);

    ProxyIntervalPixel expected = ClearProxyIntervalPixel();
    const ProxyRay ray{cameraPosition, cameraDirection};
    ASSERT_TRUE(AccumulateProxyAabb(expected, 0u, ray,
                                    glm::vec3(-1.0f, -1.0f, 0.0f),
                                    glm::vec3(1.0f, 1.0f, 1.0f)));
    ASSERT_TRUE(AccumulateProxyAabb(expected, 1u, ray,
                                    glm::vec3(-1.0f, -1.0f, 2.0f),
                                    glm::vec3(1.0f, 1.0f, 3.0f)));
    EXPECT_EQ(std::memcmp(&rasterActual, &expected, sizeof(expected)), 0);
    EXPECT_EQ(std::memcmp(&computeActual, &expected, sizeof(expected)), 0);
    EXPECT_EQ(std::memcmp(&rasterActual, &computeActual, sizeof(rasterActual)), 0)
        << "fragment-store and compute-writer records differ on " << deviceName_;
}

}  // namespace
