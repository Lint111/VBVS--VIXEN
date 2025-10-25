#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "ShaderLibraryNodeConfig.h"
#include <ShaderManagement/ShaderLibrary.h>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for shader library management
 *
 * Manages multiple shader programs with compile-time type safety.
 * Minimal implementation: synchronous compilation only.
 *
 * Type ID: 110
 */
class ShaderLibraryNodeType : public NodeType {
public:
    ShaderLibraryNodeType(const std::string& typeName = "ShaderLibrary");
    virtual ~ShaderLibraryNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Typed node instance for shader library (minimal synchronous version)
 *
 * Uses ShaderLibraryNodeConfig for compile-time type safety.
 *
 * Inputs: None (programs registered via API)
 *
 * Outputs:
 * - SHADER_PROGRAMS (ShaderProgramDescriptor*[]) - Array of program descriptors
 *
 * API:
 * - RegisterProgram() - Add shader program definition
 * - GetProgram() - Get compiled program descriptor by ID
 */
class ShaderLibraryNode : public TypedNode<ShaderLibraryNodeConfig> {
public:
    ShaderLibraryNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    virtual ~ShaderLibraryNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // ===== Program Management API =====

    /**
     * @brief Register a shader program definition
     * @param definition Program definition with shader stages
     * @return Program ID for future reference
     */
    uint32_t RegisterProgram(const ShaderManagement::ShaderProgramDefinition& definition);

    /**
     * @brief Get compiled program descriptor by ID
     * @return nullptr if not found or not yet compiled
     */
    const ShaderProgramDescriptor* GetProgram(uint32_t programId) const;

    /**
     * @brief Get compiled program descriptor by name
     * @return nullptr if not found
     */
    const ShaderProgramDescriptor* GetProgramByName(const std::string& name) const;

    /**
     * @brief Get all compiled programs
     */
    const std::vector<ShaderProgramDescriptor*>& GetAllPrograms() const {
        return programPointers;
    }

    /**
     * @brief Get number of registered programs
     */
    size_t GetProgramCount() const;

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    
    // Vulkan helpers
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirvCode);
    void DestroyShaderModule(VkShaderModule module);

    // Device-agnostic shader library (no VkDevice)
    std::unique_ptr<ShaderManagement::ShaderLibrary> shaderLib;

    // Vulkan shader program descriptors (has VkShaderModule)
    std::unordered_map<uint32_t, ShaderProgramDescriptor> programs;
    std::unordered_map<std::string, uint32_t> nameToId;
    std::vector<ShaderProgramDescriptor*> programPointers;  // For array output
};

} // namespace Vixen::RenderGraph