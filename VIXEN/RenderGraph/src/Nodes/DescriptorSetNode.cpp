#include "Nodes/DescriptorSetNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/DescriptorCacher.h"
#include "CashSystem/DescriptorSetLayoutCacher.h"  // Phase 5: Helper functions
#include <ShaderManagement/ShaderDataBundle.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring> // for memcpy
#include <unordered_map>

// Phase 5: Type-safe UBO updates with generated SDI headers
#include "generated/sdi/Draw_ShaderNames.h"

namespace Vixen::RenderGraph {

// ===== NODE TYPE =====

DescriptorSetNodeType::DescriptorSetNodeType(const std::string& typeName)
    : NodeType(typeName)
{
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
    const std::string& instanceName
) const {
    return std::make_unique<DescriptorSetNode>(
        instanceName,
        const_cast<DescriptorSetNodeType*>(this)
    );
}

// ===== NODE INSTANCE (MVP STUB) =====

DescriptorSetNode::DescriptorSetNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<DescriptorSetNodeConfig>(instanceName, nodeType)
{
    // MVP STUB: No descriptor set initialization
}

DescriptorSetNode::~DescriptorSetNode() {
    Cleanup();
}

void DescriptorSetNode::Setup() {
    NODE_LOG_DEBUG("Setup: DescriptorSetNode (MVP stub)");
    VulkanDevicePtr devicePtr = In(DescriptorSetNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("DescriptorSetNode: VulkanDevice input is null");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    NODE_LOG_INFO("Setup: Descriptor set node ready (MVP stub - no descriptors)");
}

void DescriptorSetNode::Compile() {
    NODE_LOG_INFO("Compile: DescriptorSetNode (Phase 2: using reflection data from ShaderDataBundle)");

    // Phase 2: Read ShaderDataBundle from input
    auto shaderBundle = In(DescriptorSetNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle) {
        throw std::runtime_error("DescriptorSetNode: ShaderDataBundle input is null");
    }

    std::cout << "[DescriptorSetNode::Compile] Received ShaderDataBundle: " << shaderBundle->GetProgramName() << std::endl;

    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register DescriptorCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::DescriptorWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::DescriptorCacher,
            CashSystem::DescriptorWrapper,
            CashSystem::DescriptorCreateParams
        >(
            typeid(CashSystem::DescriptorWrapper),
            "Descriptor",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("DescriptorSetNode: Registered DescriptorCacher");
    }

    // Cache the cacher reference for use throughout node lifetime
    descriptorCacher = mainCacher.GetCacher<
        CashSystem::DescriptorCacher,
        CashSystem::DescriptorWrapper,
        CashSystem::DescriptorCreateParams
    >(typeid(CashSystem::DescriptorWrapper), device);

    bool useCache = false;
    if (descriptorCacher) {
        NODE_LOG_INFO("DescriptorSetNode: Descriptor cache ready");
        // TODO: Implement descriptor caching
        // auto cachedDescriptor = descriptorCacher->GetOrCreate(params);
        useCache = false;  // Not yet implemented
    }

    // Phase 2: Extract descriptor sets from reflection (we'll use set 0 for now)
    auto descriptorBindings = shaderBundle->GetDescriptorSet(0);

    if (descriptorBindings.empty()) {
        throw std::runtime_error("DescriptorSetNode: No descriptor bindings found in ShaderDataBundle set 0");
    }

    std::cout << "[DescriptorSetNode::Compile] Found " << descriptorBindings.size()
              << " descriptor bindings in set 0" << std::endl;

    if (!useCache) {
        // Phase 2: Generate descriptor set layout from shader reflection data
        // Convert SpirvDescriptorBinding to VkDescriptorSetLayoutBinding
        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        vkBindings.reserve(descriptorBindings.size());

        for (const auto& spirvBinding : descriptorBindings) {
            VkDescriptorSetLayoutBinding vkBinding{};
            vkBinding.binding = spirvBinding.binding;
            vkBinding.descriptorType = spirvBinding.descriptorType;
            vkBinding.descriptorCount = spirvBinding.descriptorCount;
            vkBinding.stageFlags = spirvBinding.stageFlags;
            vkBinding.pImmutableSamplers = nullptr;

            vkBindings.push_back(vkBinding);

            std::cout << "[DescriptorSetNode::Compile] Binding " << vkBinding.binding
                      << ": type=" << vkBinding.descriptorType
                      << ", count=" << vkBinding.descriptorCount
                      << ", stages=" << std::hex << vkBinding.stageFlags << std::dec
                      << ", name=" << spirvBinding.name << std::endl;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
        layoutInfo.pBindings = vkBindings.data();

        VkResult result = vkCreateDescriptorSetLayout(
            device->device,
            &layoutInfo,
            nullptr,
            &descriptorSetLayout
        );

        if (result != VK_SUCCESS) {
            throw std::runtime_error("DescriptorSetNode: Failed to create descriptor set layout from reflection");
        }

        std::cout << "[DescriptorSetNode::Compile] Successfully created descriptor set layout from reflection" << std::endl;
    }

    // Continue with pool creation and descriptor allocation...

    NODE_LOG_INFO("Compile: Descriptor set layout created from reflection");
    std::cout << "[DescriptorSetNode::Compile] Created layout: " << descriptorSetLayout << std::endl;

    // Phase 5: Use helper function to calculate descriptor pool sizes from reflection
    std::vector<VkDescriptorPoolSize> poolSizes = CashSystem::CalculateDescriptorPoolSizes(
        *shaderBundle,
        0,  // setIndex
        1   // maxSets
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1; // MVP: Just one descriptor set

    VkResult result = vkCreateDescriptorPool(
        device->device,
        &poolInfo,
        nullptr,
        &descriptorPool
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetNode: Failed to create descriptor pool");
    }

    std::cout << "[DescriptorSetNode::Compile] Created descriptor pool: " << descriptorPool << std::endl;

    // Allocate descriptor sets
    descriptorSets.resize(1); // MVP: Just one descriptor set

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    result = vkAllocateDescriptorSets(
        device->device,
        &allocInfo,
        descriptorSets.data()
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetNode: Failed to allocate descriptor sets");
    }

    std::cout << "[DescriptorSetNode::Compile] Allocated descriptor set: " << descriptorSets[0] << std::endl;

    // MVP: Create UBO buffer for animated rotation (Learning Vulkan Chapter 10 feature parity)
    uboBuffer = VK_NULL_HANDLE;
    uboMemory = VK_NULL_HANDLE;
    uboMappedData = nullptr;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(glm::mat4); // Single MVP matrix
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    result = vkCreateBuffer(device->device, &bufferInfo, nullptr, &uboBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetNode: Failed to create UBO");
    }
    
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, uboBuffer, &memReqs);
    
    VkMemoryAllocateInfo allocMemInfo{};
    allocMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocMemInfo.allocationSize = memReqs.size;
    allocMemInfo.memoryTypeIndex = 0;
    
    // Find HOST_VISIBLE | HOST_COHERENT memory for persistent mapping
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(*device->gpu, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            allocMemInfo.memoryTypeIndex = i;
            break;
        }
    }
    
    result = vkAllocateMemory(device->device, &allocMemInfo, nullptr, &uboMemory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device->device, uboBuffer, nullptr);
        throw std::runtime_error("DescriptorSetNode: Failed to allocate UBO memory");
    }
    
    vkBindBufferMemory(device->device, uboBuffer, uboMemory, 0);
    
    // Map memory persistently (HOST_COHERENT means no need to flush/invalidate)
    result = vkMapMemory(device->device, uboMemory, 0, sizeof(glm::mat4), 0, &uboMappedData);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetNode: Failed to map UBO memory");
    }
    
    // Write initial MVP transformation using type-safe SDI struct
    glm::mat4 Projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 View = glm::lookAt(
        glm::vec3(10.0f, 3.0f, 10.0f),  // Camera in World Space
        glm::vec3(0.0f, 0.0f, 0.0f),     // Look at origin
        glm::vec3(0.0f, -1.0f, 0.0f)     // Up vector (Y-axis down)
    );
    glm::mat4 Model = glm::mat4(1.0f);

    // Phase 5: Type-safe UBO update using generated SDI struct
    Draw_Shader::bufferVals ubo;
    ubo.mvp = Projection * View * Model;
    memcpy(uboMappedData, &ubo, sizeof(Draw_Shader::bufferVals));
    
    std::cout << "[DescriptorSetNode::Compile] Created UBO with persistent mapping: " << uboBuffer << std::endl;
    
    // Update descriptor set with UBO (binding 0)
    VkDescriptorBufferInfo bufferDescInfo{};
    bufferDescInfo.buffer = uboBuffer;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(glm::mat4);
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSets[0];
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferDescInfo;
    
    vkUpdateDescriptorSets(device->device, 1, &descriptorWrite, 0, nullptr);
    
    std::cout << "[DescriptorSetNode::Compile] Updated descriptor set with UBO (binding 0)" << std::endl;
    
    // Check if texture inputs are provided
    VkImageView textureView = In(DescriptorSetNodeConfig::TEXTURE_VIEW);
    VkSampler textureSampler = In(DescriptorSetNodeConfig::TEXTURE_SAMPLER);
    
    if (textureView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE) {
        // Update descriptor set with real texture (binding 1)
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = textureView;
        imageInfo.sampler = textureSampler;
        
        VkWriteDescriptorSet textureWrite{};
        textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureWrite.dstSet = descriptorSets[0];
        textureWrite.dstBinding = 1;
        textureWrite.dstArrayElement = 0;
        textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureWrite.descriptorCount = 1;
        textureWrite.pImageInfo = &imageInfo;
        
        vkUpdateDescriptorSets(device->device, 1, &textureWrite, 0, nullptr);
        
        std::cout << "[DescriptorSetNode::Compile] Updated descriptor set with texture (view=" 
                  << textureView << ", sampler=" << textureSampler << ")" << std::endl;
    } else {
        std::cout << "[DescriptorSetNode::Compile] WARNING: No texture inputs provided, descriptor binding 1 will be invalid!" << std::endl;
    }
    
    // Note: We're intentionally NOT storing dummyUBO/dummyUBOMemory for cleanup
    // This is a memory leak but acceptable for MVP testing
    // TODO: Properly manage dummy resources

    // Set outputs
    Out(DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, descriptorSetLayout);
    Out(DescriptorSetNodeConfig::DESCRIPTOR_POOL, descriptorPool);
    Out(DescriptorSetNodeConfig::DESCRIPTOR_SETS, descriptorSets); 
    Out(DescriptorSetNodeConfig::VULKAN_DEVICE_OUT, device);

    std::cout << "[DescriptorSetNode::Compile] Outputs set successfully" << std::endl;

    NodeInstance::RegisterCleanup();
}

void DescriptorSetNode::Execute(VkCommandBuffer commandBuffer) {
    // Update MVP matrix with rotation (Learning Vulkan Chapter 10 feature parity)
    if (!uboMappedData) {
        std::cout << "[DescriptorSetNode::Execute] WARNING: UBO not mapped!" << std::endl;
        return; // UBO not initialized yet
    }
    
    // Get delta time from graph's centralized time system
    RenderGraph* graph = GetOwningGraph();
    if (!graph) {
        std::cout << "[DescriptorSetNode::Execute] WARNING: No graph context!" << std::endl;
        return; // No graph context
    }
    
    float deltaTime = graph->GetTime().GetDeltaTime();
    
    // Increment rotation angle (0.0005 radians per frame at 60fps ≈ 0.03 rad/sec)
    // Using deltaTime: 0.03 rad/sec for frame-rate independence
    rotationAngle += 0.03f * deltaTime;
    
    // Recalculate MVP with rotation
    glm::mat4 Projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 View = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),     // Camera position (different from initial - matches update())
        glm::vec3(0.0f, 0.0f, 0.0f),     // Look at origin
        glm::vec3(0.0f, 1.0f, 0.0f)      // Up vector (Y-axis up - matches update())
    );
    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::rotate(Model, rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f))      // Rotate around Y
          * glm::rotate(Model, rotationAngle, glm::vec3(1.0f, 1.0f, 1.0f));    // Rotate around diagonal
    
    // Phase 5: Type-safe per-frame UBO update using generated SDI struct
    Draw_Shader::bufferVals ubo;
    ubo.mvp = Projection * View * Model;
    memcpy(uboMappedData, &ubo, sizeof(Draw_Shader::bufferVals));
}

void DescriptorSetNode::CleanupImpl() {
    NODE_LOG_DEBUG("Cleanup: DescriptorSetNode");

    // Destroy descriptor pool (this also frees descriptor sets)
    if (descriptorPool != VK_NULL_HANDLE && device) {
        vkDestroyDescriptorPool(
            device->device,
            descriptorPool,
            nullptr
        );
        descriptorPool = VK_NULL_HANDLE;
        descriptorSets.clear();
        NODE_LOG_DEBUG("Cleanup: Descriptor pool destroyed");
    }

    // Destroy descriptor set layout
    if (descriptorSetLayout != VK_NULL_HANDLE && device) {
        vkDestroyDescriptorSetLayout(
            device->device,
            descriptorSetLayout,
            nullptr
        );
        descriptorSetLayout = VK_NULL_HANDLE;
        NODE_LOG_DEBUG("Cleanup: Descriptor set layout destroyed");
    }

    // Cleanup UBO resources (buffer, memory, mapping)
    // These were created during Compile() and must be freed before device destruction
    if (device) {
        if (uboMappedData != nullptr) {
            vkUnmapMemory(device->device, uboMemory);
            uboMappedData = nullptr;
            NODE_LOG_DEBUG("Cleanup: UBO memory unmapped");
        }

        if (uboBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device->device, uboBuffer, nullptr);
            uboBuffer = VK_NULL_HANDLE;
            NODE_LOG_DEBUG("Cleanup: UBO buffer destroyed");
        }

        if (uboMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device->device, uboMemory, nullptr);
            uboMemory = VK_NULL_HANDLE;
            NODE_LOG_DEBUG("Cleanup: UBO memory freed");
        }
    }
}

// ===== API METHODS (MVP STUBS) =====

void DescriptorSetNode::UpdateDescriptorSet(
    uint32_t setIndex,
    const std::vector<DescriptorUpdate>& updates
) {
    // MVP STUB: Descriptor sets not implemented yet
    NODE_LOG_WARNING("UpdateDescriptorSet: MVP stub - not implemented");
}

void DescriptorSetNode::UpdateBinding(
    uint32_t setIndex,
    uint32_t binding,
    const VkDescriptorBufferInfo& bufferInfo
) {
    // MVP STUB: Descriptor sets not implemented yet
    NODE_LOG_WARNING("UpdateBinding (buffer): MVP stub - not implemented");
}

void DescriptorSetNode::UpdateBinding(
    uint32_t setIndex,
    uint32_t binding,
    const VkDescriptorImageInfo& imageInfo
) {
    // MVP STUB: Descriptor sets not implemented yet
    NODE_LOG_WARNING("UpdateBinding (image): MVP stub - not implemented");
}

// ===== PRIVATE HELPERS (MVP STUBS) =====

void DescriptorSetNode::ValidateLayoutSpec() {
    // MVP STUB: No validation in MVP
}

void DescriptorSetNode::CreateDescriptorSetLayout() {
    // MVP STUB: No descriptor layout in MVP
}

void DescriptorSetNode::CreateDescriptorPool() {
    // MVP STUB: No descriptor pool in MVP
}

void DescriptorSetNode::AllocateDescriptorSets() {
    // MVP STUB: No descriptor sets in MVP
}

void DescriptorSetNode::CreateDescriptorSetLayoutManually() {
    // This method is no longer used - inlined into Compile()
    // Keeping stub for API compatibility
}

} // namespace Vixen::RenderGraph
