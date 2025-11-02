#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "ShaderLibraryNodeConfig.h"
#include <ShaderManagement/ShaderDataBundle.h>
#include "VulkanShader.h"
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
class ShaderLibraryNodeType : public TypedNodeType<ShaderLibraryNodeConfig> {
public:
    ShaderLibraryNodeType(const std::string& typeName = "ShaderLibrary")
        : TypedNodeType<ShaderLibraryNodeConfig>(typeName) {}
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
    ~ShaderLibraryNode() override = default;

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(Context& ctx) override;
    void CompileImpl(Context& ctx) override;
    void ExecuteImpl(Context& ctx) override;
    void CleanupImpl() override;

    // ===== Program Management API (MVP STUBS - NOT IMPLEMENTED) =====

    // MVP: These methods are stubs - shader management not integrated yet
    // In MVP, load shaders directly via VulkanShader class in application code

private:
    // Note: device member is inherited from base class NodeInstance
    // Set in Setup() via SetDevice(), used throughout Compile()

    // CashSystem integration - cached during Compile()
    CashSystem::ShaderModuleCacher* shaderModuleCacher = nullptr;

    // ShaderManagement integration - Phase 1
    std::shared_ptr<ShaderManagement::ShaderDataBundle> shaderBundle_;

    // Loaded shader modules (cached from ShaderModuleCacher)
    std::shared_ptr<CashSystem::ShaderModuleWrapper> vertexShader;
    std::shared_ptr<CashSystem::ShaderModuleWrapper> fragmentShader;

    // VulkanShader wrapper for compatibility with GraphicsPipelineNode (Phase 1)
    VulkanShader* vulkanShader = nullptr;

    // Device metadata (received via EventBus)
    int deviceVulkanVersion = 130;  // Default: Vulkan 1.3
    int deviceSpirvVersion = 160;   // Default: SPIR-V 1.6
    bool hasReceivedDeviceMetadata = false;

    // Event handlers
    void OnDeviceMetadata(const Vixen::EventBus::BaseEventMessage& message);
};

} // namespace Vixen::RenderGraph