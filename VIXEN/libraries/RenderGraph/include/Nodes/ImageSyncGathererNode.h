#pragma once

#include "Core/VariadicTypedNode.h"
#include "Core/NodeType.h"
#include "Data/Nodes/ImageSyncGathererNodeConfig.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for gathering a variable-count set of IRenderTarget* handles
 * into one array-typed output (Sampled Lighting Inc4 M1).
 *
 * See ImageSyncGathererNodeConfig.h for the full "why this exists" — the image-typed
 * sibling of BufferSyncGathererNode, mirroring its variadic-gatherer shape exactly.
 */
class ImageSyncGathererNodeType : public TypedNodeType<ImageSyncGathererNodeConfig> {
public:
    ImageSyncGathererNodeType(const std::string& typeName = "ImageSyncGatherer")
        : TypedNodeType<ImageSyncGathererNodeConfig>(typeName)
        , defaultMinVariadicInputs(0)
        , defaultMaxVariadicInputs(SIZE_MAX)
    {}
    virtual ~ImageSyncGathererNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;

    size_t GetDefaultMinVariadicInputs() const { return defaultMinVariadicInputs; }
    size_t GetDefaultMaxVariadicInputs() const { return defaultMaxVariadicInputs; }

private:
    size_t defaultMinVariadicInputs;
    size_t defaultMaxVariadicInputs;
};

/**
 * @brief Variadic node instance gathering N IRenderTarget* handles into one
 * std::vector<IRenderTarget*> output.
 *
 * Mirrors BufferSyncGathererNode exactly (see that node's own file header): no shader
 * to reflect against, the image COUNT is known at graph-build time, so slots are
 * pre-registered manually via PreRegisterImageSlots(count) rather than discovered from
 * shader metadata. Each slot accepts any Image-typed Resource*; hazard AccessKind is
 * declared on the CONSUMING ComputeStageNodeConfig slot (IMAGE_WRITE_ARRAY), not here.
 */
class ImageSyncGathererNode : public VariadicTypedNode<ImageSyncGathererNodeConfig> {
public:
    ImageSyncGathererNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~ImageSyncGathererNode() override = default;

    /**
     * @brief Pre-register `count` variadic image slots.
     *
     * Call during graph construction, before Setup, so batch.Connect(...,
     * gatherer, /*index*\/ i) has a slot to land on. Mirrors
     * BufferSyncGathererNode::PreRegisterBufferSlots exactly.
     */
    void PreRegisterImageSlots(size_t count);

protected:
    void SetupImpl(VariadicSetupContext& ctx) override;
    void CompileImpl(VariadicCompileContext& ctx) override;
    void ExecuteImpl(VariadicExecuteContext& ctx) override;
    void CleanupImpl(VariadicCleanupContext& ctx) override;
};

} // namespace Vixen::RenderGraph
