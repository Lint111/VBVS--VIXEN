// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/LightingConfigNodeConfig.h"
#include "Generated/LightingConfig.g.h"
#include <iterator>
#include <memory>
#include <vector>

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
 * The default content is a single directional light matching Lighting.glsl's
 * previously-hardcoded default. Hosts can replace the shared light set through
 * SetLights() without graph rewiring; the fixed generated capacity remains four.
 *
 * Lifecycle: the ring buffers persist across graph recompile; released on
 * FinalTeardown (see CleanupImpl).
 */
class LightingConfigNode : public TypedNode<LightingConfigNodeConfig> {
public:
    using Base = TypedNode<LightingConfigNodeConfig>;

    LightingConfigNode(const std::string& instanceName, NodeType* nodeType);
    ~LightingConfigNode() override = default;

    // Replace the shared light set. Excess entries are clipped to the generated
    // four-light capacity; an empty list is valid and leaves ambient lighting.
    // Missing purpose scales default to 1.0; excess scales are ignored.
    void SetLights(const std::vector<Vixen::Gpu::Light>& lights, float ambientIntensity = 0.3f,
                   const std::vector<float>& celSpillPurposeScales = {});

protected:
    void TypedSetupImpl(TypedSetupContext&    ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    friend class LightingConfigNodeTestAccess;

    static const uint32_t kRingSize;  // = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT

    PerFrameResources perFrame_;
    Vixen::Gpu::LightingConfig customLighting_{};
    bool hasCustomLighting_ = false;
};

} // namespace Vixen::RenderGraph
