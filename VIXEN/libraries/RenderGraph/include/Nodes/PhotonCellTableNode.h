// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include "Nodes/StorageBufferNode.h"

#include <cstdint>

namespace Vixen::RenderGraph {

class PhotonCellParamsConfigNode;

/**
 * @brief Node type for the fixed-size PhotonCell SSBO.
 *
 * This is intentionally a distinct resource node rather than a generic
 * StorageBufferNode configured by the application.  The table's capacity and
 * entry layout are part of the photon node contract and therefore travel with
 * the reusable node into any graph.
 */
class PhotonCellTableNodeType : public TypedNodeType<StorageBufferNodeConfig> {
public:
    explicit PhotonCellTableNodeType(const std::string& typeName = "PhotonCellTable")
        : TypedNodeType<StorageBufferNodeConfig>(typeName) {}
    ~PhotonCellTableNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName) const override;
};

/**
 * @brief Zero-initialised, 131072-entry photon/irradiance world-cell cache.
 *
 * The node owns allocation and the debug-only readback hook.  It has no scene
 * or simulation ownership; all writes are GPU render-graph SSBO writes.
 */
class PhotonCellTableNode : public StorageBufferNode {
public:
    using Base = StorageBufferNode;

    static constexpr uint32_t kCapacity = 131072u;
    static constexpr uint32_t kEntryBytes = 64u;
    // StorageBufferNode's explicit size parameter is a uint32_t; the fixed
    // 8 MiB table is safely within that contract.
    static constexpr uint32_t kTableBytes = kCapacity * kEntryBytes;

    PhotonCellTableNode(const std::string& instanceName, NodeType* nodeType);
    ~PhotonCellTableNode() override = default;

    /**
     * @brief Run the opt-in headless diagnostic without forwarding state.
     *
     * The method is kept on the table resource node so the application only
     * composes the readback hook.  It waits for the device, compares current
     * generation occupancy with the march HitRecord buffer, and logs results.
     */
    void RunDiagnostic(StorageBufferNode& hitRecords,
                       const PhotonCellParamsConfigNode& params,
                       Vixen::Vulkan::Resources::VulkanDevice* device,
                       Logger* logger,
                       uint64_t sampleFrame) const;
};

} // namespace Vixen::RenderGraph
