#pragma once

#include "Headers.h"

class VulkanDevice;

class VulkanDescriptor {
public:
    VulkanDescriptor();
    ~VulkanDescriptor();

    // Create descriptor pool and allocate descriptor set from it
    VulkanStatus CreateDescriptor(bool useTexture);
    // Destroy the created Descriptor set object
    VulkanStatus DestroyDescriptor();

    // Define the descriptor sets layout binding and
    // create the descriptor set layout object
    virtual VulkanStatus CreateDescriptorSetLayout(bool useTexture) = 0;
    // Destroy the created descriptor layout object
    VulkanStatus DestroyDescriptorLayout();

    // Create the descriptor pool that is used to allocate
    // descriptor sets
    virtual VulkanStatus CreateDescriptorPool(bool useTexture) = 0;
    // Destroy the created descriptor pool object
    VulkanStatus DestroyDescriptorPool();

    // Create Descriptor set associated resources before creating the descriptor set.
    virtual VulkanStatus CreateDescriptorResources() = 0;

    // Create the descriptor set from the descriptor pool allocated
    // memory and update the descriptor set information into it.
    virtual VulkanStatus CreateDescriptorSet(bool useTexture) = 0;
    // Destroy the created descriptor set object
    VulkanStatus DestroyDescriptorSet();

    virtual VulkanStatus CreatePipelineLayout() = 0;
    VulkanStatus DestroyPipelineLayout();

    // Pipeline layout object
    VkPipelineLayout pipelineLayout;

    // List of all the VkDescriptorSetLayouts
    std::vector<VkDescriptorSetLayout> descLayout;

    // Descriptor pool object that will be used for allocating
    // VkDescriptorSet objects
    VkDescriptorPool descriptorPool;

    // List of all created VkDescriptorSet
    std::vector<VkDescriptorSet> descriptorSet;

    // Logical device handle used for creating the descriptor pool
    // and descriptor sets
    VulkanDevice* deviceObj;

};