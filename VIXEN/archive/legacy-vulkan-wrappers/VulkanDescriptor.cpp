#include "VulkanDescriptor.h"
#include "VulkanResources/VulkanDevice.h"

VulkanStatus VulkanDescriptor::DestroyPipelineLayout()
{
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(deviceObj->device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    return {};
}

VulkanStatus VulkanDescriptor::DestroyDescriptorPool()
{
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(deviceObj->device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    return {};
}

VulkanStatus VulkanDescriptor::DestroyDescriptorSet()
{
    if (descriptorSet.empty()) {
        return {};
    }
    
    vkFreeDescriptorSets(
        deviceObj->device,
        descriptorPool,
        static_cast<uint32_t>(descriptorSet.size()),
        descriptorSet.data()
    );
    descriptorSet.clear();
    return {};
}

VulkanStatus VulkanDescriptor::DestroyDescriptorLayout()
{
    for (size_t i = 0; i < descLayout.size(); i++) {
        if (descLayout[i] != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(deviceObj->device, descLayout[i], nullptr);
        }
    }
    descLayout.clear();
    return {};
}

VulkanDescriptor::VulkanDescriptor()
    : pipelineLayout(VK_NULL_HANDLE),
      descriptorPool(VK_NULL_HANDLE),
      deviceObj(nullptr)
{
}

VulkanDescriptor::~VulkanDescriptor()
{
    (void)DestroyDescriptorSet();
    (void)DestroyDescriptorPool();
    (void)DestroyDescriptorLayout();
    (void)DestroyPipelineLayout();
}

VulkanStatus VulkanDescriptor::CreateDescriptor(bool useTexture)
{
    VK_PROPAGATE_ERROR(CreateDescriptorResources());
    VK_PROPAGATE_ERROR(CreateDescriptorPool(useTexture));
    VK_PROPAGATE_ERROR(CreateDescriptorSet(useTexture));
    return {};
}

