#include "Nodes/DescriptorSetNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "ShaderManagement/ShaderProgram.h"
#include <unordered_set>

namespace Vixen::RenderGraph {

DescriptorSetNodeType::DescriptorSetNodeType() {
    typeId = 107;
    typeName = "DescriptorSet";
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0;

    DescriptorSetNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    workloadMetrics.estimatedMemoryFootprint = 4096;
    workloadMetrics.estimatedComputeCost = 0.1f;
    workloadMetrics.estimatedBandwidthCost = 0.1f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> DescriptorSetNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<DescriptorSetNode>(
        instanceName,
        const_cast<DescriptorSetNodeType*>(this),
        device
    );
}

DescriptorSetNode::DescriptorSetNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : TypedNode<DescriptorSetNodeConfig>(instanceName, nodeType, device)
{
}

DescriptorSetNode::~DescriptorSetNode() {
    Cleanup();
}

void DescriptorSetNode::Setup() {
    NODE_LOG_INFO("Setup: Descriptor set node ready");
}

void DescriptorSetNode::Compile() {
    // PRIORITY 1: Check for shader program input (auto-reflection)
    // Get shader program resource, then extract CompiledProgram from description
    IResource* shaderResource = NodeInstance::GetInput(DescriptorSetNodeConfig::SHADER_PROGRAM_Slot::index);
    ShaderManagement::CompiledProgram* shaderProgram = nullptr;
    if (shaderProgram)
        shaderProgram = shaderResource->


    if (shaderProgram && shaderProgram->descriptorLayout) {
        layoutSpec = shaderProgram->descriptorLayout;
        NODE_LOG_INFO("DescriptorSetNode: Using descriptor layout from shader program reflection");
    } else {
        // PRIORITY 2: Manual parameter specification (fallback)
        layoutSpec = GetParameterValue<const ShaderManagement::DescriptorLayoutSpec*>(
            DescriptorSetNodeConfig::PARAM_LAYOUT_SPEC, nullptr);
        if (layoutSpec) {
            NODE_LOG_INFO("DescriptorSetNode: Using manually specified descriptor layout");
        } else {
            NODE_LOG_ERROR("DescriptorSetNode: No descriptor layout provided (neither shader input nor parameter)");
            return;
        }
    }

    // Validate and create Vulkan objects (may throw on error)
    ValidateLayoutSpec();
    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    AllocateDescriptorSets();

    // Publish outputs
    Out(DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT) = descriptorSetLayout;
    Out(DescriptorSetNodeConfig::DESCRIPTOR_POOL) = descriptorPool;
    for (uint32_t i = 0; i < layoutSpec->maxSets; ++i) {
        SetOutput(DescriptorSetNodeConfig::DESCRIPTOR_SETS, i, descriptorSets[i]);
    }
}

void DescriptorSetNode::Execute(VkCommandBuffer commandBuffer) {
}

void DescriptorSetNode::Cleanup() {
    VkDevice vkDevice = device->device;
    descriptorSets.clear();

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    layoutSpec = nullptr;
}

void DescriptorSetNode::UpdateDescriptorSet(
    uint32_t setIndex,
    const std::vector<DescriptorUpdate>& updates
) {
    if (setIndex >= descriptorSets.size()) {
        NODE_LOG_ERROR("Invalid set index: " + std::to_string(setIndex));
        return;
    }

    std::vector<VkWriteDescriptorSet> writes;
    for (const auto& update : updates) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets[setIndex];
        write.dstBinding = update.binding;
        write.dstArrayElement = 0;
        write.descriptorType = update.type;
        write.descriptorCount = 1;

        switch (update.type) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                if (!update.bufferInfos.empty()) {
                    write.pBufferInfo = update.bufferInfos.data();
                    write.descriptorCount = static_cast<uint32_t>(update.bufferInfos.size());
                }
                break;

            case VK_DESCRIPTOR_TYPE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                if (!update.imageInfos.empty()) {
                    write.pImageInfo = update.imageInfos.data();
                    write.descriptorCount = static_cast<uint32_t>(update.imageInfos.size());
                }
                break;

            default:
                NODE_LOG_WARNING("Unsupported descriptor type");
                continue;
        }

        writes.push_back(write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(device->device,
            static_cast<uint32_t>(writes.size()),
            writes.data(), 0, nullptr);
    }
}

void DescriptorSetNode::UpdateBinding(
    uint32_t setIndex,
    uint32_t binding,
    const VkDescriptorBufferInfo& bufferInfo
) {
    DescriptorUpdate update;
    update.binding = binding;
    update.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    update.bufferInfos.push_back(bufferInfo);
    UpdateDescriptorSet(setIndex, {update});
}

void DescriptorSetNode::UpdateBinding(
    uint32_t setIndex,
    uint32_t binding,
    const VkDescriptorImageInfo& imageInfo
) {
    DescriptorUpdate update;
    update.binding = binding;
    update.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    update.imageInfos.push_back(imageInfo);
    UpdateDescriptorSet(setIndex, {update});
}

void DescriptorSetNode::ValidateLayoutSpec() {
    if (!layoutSpec->IsValid()) {
        throw std::runtime_error("DescriptorLayoutSpec is invalid");
    }

    if (layoutSpec->maxSets == 0) {
        throw std::runtime_error("maxSets must be > 0");
    }

    std::unordered_set<uint32_t> seenBindings;
    for (const auto& binding : layoutSpec->bindings) {
        if (seenBindings.count(binding.binding)) {
            throw std::runtime_error("Duplicate binding: " + std::to_string(binding.binding));
        }
        seenBindings.insert(binding.binding);
    }
}

void DescriptorSetNode::CreateDescriptorSetLayout() {
    auto vkBindings = layoutSpec->ToVulkanBindings();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.data();

    VkResult result = vkCreateDescriptorSetLayout(
        device->device, &layoutInfo, nullptr, &descriptorSetLayout);

    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to create descriptor set layout"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }
}

void DescriptorSetNode::CreateDescriptorPool() {
    auto poolSizes = layoutSpec->ToPoolSizes();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = layoutSpec->maxSets;

    VkResult result = vkCreateDescriptorPool(
        device->device, &poolInfo, nullptr, &descriptorPool);

    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to create descriptor pool"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }
}

void DescriptorSetNode::AllocateDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(layoutSpec->maxSets, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = layoutSpec->maxSets;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(layoutSpec->maxSets);
    VkResult result = vkAllocateDescriptorSets(
        device->device, &allocInfo, descriptorSets.data());

    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to allocate descriptor sets"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }
}

}
