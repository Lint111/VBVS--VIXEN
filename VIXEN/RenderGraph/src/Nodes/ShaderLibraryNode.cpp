#include "Nodes/ShaderLibraryNode.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"

namespace Vixen::RenderGraph {

// ====== ShaderLibraryNodeType ======

ShaderLibraryNodeType::ShaderLibraryNodeType() {
    typeId = 110; // Unique ID
    typeName = "ShaderLibrary";
    pipelineType = PipelineType::None;  // Not a pipeline node
    requiredCapabilities = DeviceCapability::None;
    supportsInstancing = false;
    maxInstances = 1;  // Only one shader library per graph

    // Initialize config and extract schema
    ShaderLibraryNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 0;  // Minimal metadata
    workloadMetrics.estimatedComputeCost = 0.0f;
    workloadMetrics.estimatedBandwidthCost = 0.0f;
    workloadMetrics.canRunInParallel = false;  // Sequential compilation
}

std::unique_ptr<NodeInstance> ShaderLibraryNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<ShaderLibraryNode>(
        instanceName,
        const_cast<ShaderLibraryNodeType*>(this),
        device
    );
}

// ====== ShaderLibraryNode ======

ShaderLibraryNode::ShaderLibraryNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : TypedNode<ShaderLibraryNodeConfig>(instanceName, nodeType, device)
    , shaderLib(std::make_unique<ShaderManagement::ShaderLibrary>())
{
}

ShaderLibraryNode::~ShaderLibraryNode() {
    Cleanup();
}

void ShaderLibraryNode::Setup() {
    NODE_LOG_INFO("Setup: Shader library node ready");
}

void ShaderLibraryNode::Compile() {
    NODE_LOG_INFO("Compile: Compiling shader programs");

    // Get all registered programs from device-agnostic library
    size_t programCount = shaderLib->GetProgramCount();
    if (programCount == 0) {
        NODE_LOG_WARNING("No shader programs registered");
        return;
    }

    NODE_LOG_DEBUG("Compiling " + std::to_string(programCount) + " shader programs");

    // Compile all programs synchronously
    uint32_t successCount = shaderLib->CompileAllPrograms();

    NODE_LOG_INFO("Compiled " + std::to_string(successCount) + "/" +
                  std::to_string(programCount) + " programs successfully");

    // Convert compiled SPIRV to VkShaderModule
    auto compiledPrograms = shaderLib->GetAllCompiledPrograms();
    programPointers.clear();

    for (const auto* compiledProgram : compiledPrograms) {
        if (!compiledProgram) continue;

        ShaderProgramDescriptor descriptor;
        descriptor.programId = compiledProgram->programId;
        descriptor.name = compiledProgram->name;
        descriptor.pipelineType = compiledProgram->pipelineType;
        descriptor.generation = compiledProgram->generation;

        NODE_LOG_DEBUG("Creating VkShaderModules for program: " + descriptor.name);

        // Create VkShaderModule for each stage
        for (const auto& compiledStage : compiledProgram->stages) {
            ShaderProgramDescriptor::CompiledStage stage;
            stage.stage = compiledStage.stage;
            stage.entryPoint = compiledStage.entryPoint;
            stage.generation = compiledStage.generation;

            // Create Vulkan shader module from SPIRV bytecode
            stage.module = CreateShaderModule(compiledStage.spirvCode);

            descriptor.stages.push_back(stage);
        }

        // Build cached Vulkan stage infos
        descriptor.RebuildVkStageInfos();

        // Store descriptor
        programs[descriptor.programId] = descriptor;
        if (!descriptor.name.empty()) {
            nameToId[descriptor.name] = descriptor.programId;
        }

        NODE_LOG_DEBUG("Program " + descriptor.name + " ready (" +
                      std::to_string(descriptor.stages.size()) + " stages)");
    }

    // Build output array
    for (auto& [id, program] : programs) {
        programPointers.push_back(&program);
    }

    NODE_LOG_INFO("Compile complete: " + std::to_string(programs.size()) +
                  " programs available");
}

void ShaderLibraryNode::Execute(VkCommandBuffer commandBuffer) {
    // No-op - shader compilation happens in Compile phase
}

void ShaderLibraryNode::Cleanup() {
    NODE_LOG_DEBUG("Cleanup: Destroying shader modules");

    // Destroy all VkShaderModule objects
    for (auto& [id, program] : programs) {
        for (auto& stage : program.stages) {
            if (stage.module != VK_NULL_HANDLE) {
                DestroyShaderModule(stage.module);
                stage.module = VK_NULL_HANDLE;
            }
        }
    }

    programs.clear();
    programPointers.clear();
    nameToId.clear();
}

uint32_t ShaderLibraryNode::RegisterProgram(
    const ShaderManagement::ShaderProgramDefinition& definition
) {
    // Register with device-agnostic library
    uint32_t programId = shaderLib->RegisterProgram(definition);

    NODE_LOG_INFO("Registered shader program: " + definition.name +
                  " (ID: " + std::to_string(programId) + ")");

    return programId;
}

const ShaderProgramDescriptor* ShaderLibraryNode::GetProgram(uint32_t programId) const {
    auto it = programs.find(programId);
    return (it != programs.end()) ? &it->second : nullptr;
}

const ShaderProgramDescriptor* ShaderLibraryNode::GetProgramByName(const std::string& name) const {
    auto idIt = nameToId.find(name);
    if (idIt == nameToId.end()) {
        return nullptr;
    }
    return GetProgram(idIt->second);
}

size_t ShaderLibraryNode::GetProgramCount() const {
    return shaderLib->GetProgramCount();
}

VkShaderModule ShaderLibraryNode::CreateShaderModule(const std::vector<uint32_t>& spirvCode) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
    createInfo.pCode = spirvCode.data();
    createInfo.flags = 0;

    VkShaderModule shaderModule;
    VkResult result = vkCreateShaderModule(device->device, &createInfo, nullptr, &shaderModule);

    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to create shader module"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    return shaderModule;
}

void ShaderLibraryNode::DestroyShaderModule(VkShaderModule module) {
    if (module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device->device, module, nullptr);
    }
}

} // namespace Vixen::RenderGraph