#pragma once

#include "Core/VariadicTypedNode.h"
#include "Core/NodeType.h"
#include "Data/Nodes/BufferSyncGathererNodeConfig.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for gathering a variable-count set of VkBuffer handles
 * into one array-typed output (Sampled Lighting Inc3 M5).
 *
 * See BufferSyncGathererNodeConfig.h for the full "why this exists" — the
 * generic replacement for ComputeStageNodeConfig's old fixed named buffer-
 * sync slots (BUFFER_WRITE/BUFFER_READ_A/BUFFER_READ_B), mirroring
 * DescriptorResourceGathererNode/PushConstantGathererNode's own variadic-
 * gatherer shape.
 */
class BufferSyncGathererNodeType : public TypedNodeType<BufferSyncGathererNodeConfig> {
public:
    BufferSyncGathererNodeType(const std::string& typeName = "BufferSyncGatherer")
        : TypedNodeType<BufferSyncGathererNodeConfig>(typeName)
        , defaultMinVariadicInputs(0)
        , defaultMaxVariadicInputs(SIZE_MAX)
    {}
    virtual ~BufferSyncGathererNodeType() = default;

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
 * @brief Variadic node instance gathering N VkBuffer handles into one
 * std::vector<VkBuffer> output.
 *
 * Unlike DescriptorResourceGathererNode/PushConstantGathererNode, this node
 * has NO shader to reflect against — the buffer COUNT is known at graph-
 * build time (e.g. "2" for a ping-pong pair), so slots are pre-registered
 * manually via PreRegisterBufferSlots(count) rather than discovered from
 * shader metadata. Each slot accepts any Buffer-typed Resource*; there is
 * no descriptor-type/binding validation (this node produces a plain array
 * VALUE, not a descriptor layout — hazard AccessKind is declared on the
 * CONSUMING ComputeStageNodeConfig slot, not here).
 */
class BufferSyncGathererNode : public VariadicTypedNode<BufferSyncGathererNodeConfig> {
public:
    BufferSyncGathererNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~BufferSyncGathererNode() override = default;

    /**
     * @brief Pre-register `count` variadic buffer slots.
     *
     * Call during graph construction, before Setup, so batch.Connect(...,
     * gatherer, /*index*\/ i) has a slot to land on. Mirrors
     * DescriptorResourceGathererNode::PreRegisterVariadicSlots's own
     * "call before Setup" contract, simplified: no binding/descriptor-type
     * metadata needed, just a count.
     */
    void PreRegisterBufferSlots(size_t count);

protected:
    void SetupImpl(VariadicSetupContext& ctx) override;
    void CompileImpl(VariadicCompileContext& ctx) override;
    void ExecuteImpl(VariadicExecuteContext& ctx) override;
    void CleanupImpl(VariadicCleanupContext& ctx) override;
};

} // namespace Vixen::RenderGraph
