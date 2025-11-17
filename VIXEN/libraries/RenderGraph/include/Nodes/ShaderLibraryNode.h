#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/ShaderLibraryNodeConfig.h"
#include <ShaderDataBundle.h>
#include <ShaderBundleBuilder.h>
#include <memory>
#include <functional>

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

    // ===== Program Management API =====

    /**
     * @brief Register shader program via builder function
     *
     * Accepts a function that configures and returns a ShaderBundleBuilder.
     * This decouples the node from builder implementation details.
     * Can be called multiple times to register multiple shader programs.
     *
     * @param builderFunc Function that returns configured ShaderBundleBuilder
     *
     * Example:
     * @code
     * shaderLibNode->RegisterShaderBuilder([](int vulkanVer, int spirvVer) {
     *     ShaderManagement::ShaderBundleBuilder builder;
     *     builder.SetProgramName("ComputeTest")
     *            .SetTargetVulkanVersion(vulkanVer)
     *            .SetTargetSpirvVersion(spirvVer)
     *            .AddStageFromFile(ShaderManagement::ShaderStage::Compute, "ComputeTest.comp", "main");
     *     return builder;
     * });
     * @endcode
     */
    void RegisterShaderBuilder(
        std::function<::ShaderManagement::ShaderBundleBuilder(int vulkanVersion, int spirvVersion)> builderFunc
    );

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Note: device member is inherited from base class NodeInstance
    // Set in Setup() via SetDevice(), used throughout Compile()

    // CashSystem integration - cached during Compile()
    CashSystem::ShaderModuleCacher* shaderModuleCacher = nullptr;

    // ShaderManagement integration - Phase 1
    std::shared_ptr<::ShaderManagement::ShaderDataBundle> shaderBundle_;

    // Loaded shader modules (cached from ShaderModuleCacher)
    std::shared_ptr<CashSystem::ShaderModuleWrapper> vertexShader;
    std::shared_ptr<CashSystem::ShaderModuleWrapper> fragmentShader;

    // Device metadata (received via EventBus)
    int deviceVulkanVersion = 130;  // Default: Vulkan 1.3
    int deviceSpirvVersion = 160;   // Default: SPIR-V 1.6
    bool hasReceivedDeviceMetadata = false;

    // Event handlers
    void OnDeviceMetadata(const Vixen::EventBus::BaseEventMessage& message);

    // Shader builder functions (registered via RegisterShaderBuilder)
    std::vector<std::function<::ShaderManagement::ShaderBundleBuilder(int, int)>> shaderBuilderFuncs;

    // Helper methods
    void RegisterShaderModuleCacher();
    void InitializeShaderModuleCacher();
    void CompileShaderBundle(int targetVulkan, int targetSpirv);
    void CreateShaderModules();
};

} // namespace Vixen::RenderGraph