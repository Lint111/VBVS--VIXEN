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

    /// Recipe-Live-App-Bucketed-Dispatch Inc4 M3: read the real device's resolved GLSL compile
    /// target versions (Vulkan/SPIR-V shorthand, e.g. 130/160), for a caller OUTSIDE the graph
    /// that needs to compile GLSL directly via ShaderManagement::ShaderCompiler (bypassing this
    /// node's own RegisterShaderBuilder callback path entirely) but must still target the SAME
    /// environment every other shader in the graph targets -- ShaderCompiler's own
    /// CompilationOptions default (Vulkan 1.2/SPIR-V 1.5) is a safe FLOOR, not necessarily what
    /// the actual selected device supports, and a #version 460 GLSL source can fail to compile
    /// ("Unable to parse built-ins") against too-low a target environment. 120/150 (this node's
    /// own pre-device-metadata defaults) until OnDeviceMetadata has actually fired once.
    int GetDeviceVulkanVersion() const { return deviceVulkanVersion; }
    int GetDeviceSpirvVersion() const { return deviceSpirvVersion; }

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

    // Device metadata (received via EventBus).
    // Defaults are a conservative safe floor (Vulkan 1.2 / SPIR-V 1.5) so that any shader compiled
    // before DeviceMetadataEvent is received never targets a higher SPIR-V version than a Vulkan-1.2
    // device can accept (e.g. Mesa Dozen on WSL2 reports apiVersion = 1.2 even though the instance
    // requests 1.3).  Once OnDeviceMetadata fires these are replaced with the device's real caps.
    int deviceVulkanVersion = 120;  // Safe floor: Vulkan 1.2
    int deviceSpirvVersion = 150;   // Safe floor: SPIR-V 1.5 (matches Vulkan 1.2 per spec §46.1)
    bool hasReceivedDeviceMetadata = false;

    // Event handlers
    void OnDeviceMetadata(const Vixen::EventBus::BaseEventMessage& message);

    // Shader builder functions (registered via RegisterShaderBuilder)
    std::vector<std::function<::ShaderManagement::ShaderBundleBuilder(int, int)>> shaderBuilderFuncs;

    // Task profile for compile-time cost estimation (Sprint 6.5)
    ITaskProfile* compileProfile_ = nullptr;

    // Helper methods
    void RegisterShaderModuleCacher();
    void InitializeShaderModuleCacher();
    void CompileShaderBundle(int targetVulkan, int targetSpirv);
    void CreateShaderModules();
};

} // namespace Vixen::RenderGraph