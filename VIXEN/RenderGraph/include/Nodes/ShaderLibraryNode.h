#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "ShaderLibraryNodeConfig.h"
// TEMPORARILY REMOVED - MVP uses VulkanShader directly
// #include <ShaderManagement/ShaderLibrary.h>
#include <memory>

// Forward declarations
namespace CashSystem {
    class ShaderModuleCacher;
    struct ShaderModuleWrapper;
}

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
    
protected:
    void CleanupImpl() override;

    // ===== Program Management API (MVP STUBS - NOT IMPLEMENTED) =====

    // MVP: These methods are stubs - shader management not integrated yet
    // In MVP, load shaders directly via VulkanShader class in application code

private:
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;

    // CashSystem integration - cached during Compile()
    CashSystem::ShaderModuleCacher* shaderModuleCacher = nullptr;

    // Loaded shader modules (cached from ShaderModuleCacher)
    std::shared_ptr<CashSystem::ShaderModuleWrapper> vertexShader;
    std::shared_ptr<CashSystem::ShaderModuleWrapper> fragmentShader;
};

} // namespace Vixen::RenderGraph