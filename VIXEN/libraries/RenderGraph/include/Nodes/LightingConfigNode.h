// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/LightingConfigNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for LightingConfigNode.
 */
class LightingConfigNodeType : public TypedNodeType<LightingConfigNodeConfig> {
public:
    LightingConfigNodeType(const std::string& typeName = "LightingConfig")
        : TypedNodeType<LightingConfigNodeConfig>(typeName) {}
    virtual ~LightingConfigNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Uploads a Vixen::Gpu::LightingConfig record into a ring-buffered,
 * host-visible storage buffer — one SSBO per frame-in-flight (mirrors
 * DynamicInstanceBufferNode's PerFrameResources ring pattern).
 *
 * Content is static this increment (Sampled Lighting Inc0 M3): a single
 * directional light matching Lighting.glsl's previously-hardcoded default
 * (direction normalize(1,1,-1), white radiance, ambientIntensity 0.3).
 * Re-uploaded every Execute (144 B, negligible) so a future milestone can
 * mutate the light list via SetLights() with no graph rewiring.
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl).
 */
class LightingConfigNode : public TypedNode<LightingConfigNodeConfig> {
public:
    using Base = TypedNode<LightingConfigNodeConfig>;

    LightingConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~LightingConfigNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;
};

} // namespace Vixen::RenderGraph
