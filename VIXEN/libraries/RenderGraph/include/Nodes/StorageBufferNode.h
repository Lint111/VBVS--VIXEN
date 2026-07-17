// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/StorageBufferNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for a generic, arbitrary-sized storage buffer (SSBO).
 */
class StorageBufferNodeType : public TypedNodeType<StorageBufferNodeConfig> {
public:
    StorageBufferNodeType(const std::string& typeName = "StorageBuffer")
        : TypedNodeType<StorageBufferNodeConfig>(typeName) {}
    virtual ~StorageBufferNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates a generic, zero-initialised storage buffer (SSBO) of an
 * arbitrary byte size. Reusable: the size is a parameter (explicit bytes, or
 * elementCount * elementStride), not tied to any particular payload shape.
 *
 * The buffer is host-visible/host-coherent, zero-filled once at compile time.
 *
 * Lifecycle: the buffer persists across graph recompile; it is recreated when
 * the requested size GROWS (e.g. swapchain resize to a larger extent), and is
 * released on FinalTeardown.
 */
class StorageBufferNode : public TypedNode<StorageBufferNodeConfig> {
public:
    using Base = TypedNode<StorageBufferNodeConfig>;

    StorageBufferNode(const std::string& instanceName, NodeType* nodeType);
    ~StorageBufferNode() override = default;

    /**
     * @brief Map the buffer's host-visible/host-coherent memory for a CPU-side
     * readback (Sampled Lighting Inc3 M4's equal-error gate: reading back
     * reservoirRecordsA/B after a dispatch to numerically compare against the
     * CPU brute-force reference). Caller must have externally waited for the
     * GPU work writing this buffer to complete (fence/vkDeviceWaitIdle) before
     * calling -- this does not synchronize itself, mirroring PerFrameResources'
     * own "no flush needed, HOST_COHERENT" convention.
     * @return mapped pointer, or nullptr if the buffer has not been created yet.
     */
    void* MapForReadback(Vixen::Vulkan::Resources::VulkanDevice* device) const;
    void UnmapReadback(Vixen::Vulkan::Resources::VulkanDevice* device) const;
    VkDeviceSize GetSizeBytes() const { return sizeBytes_; }

    /**
     * @brief Recipe-Live-App-Bucketed-Dispatch Inc4 M3: the raw VkBuffer handle, for a
     * caller OUTSIDE the render graph's node-connection system that needs to reference
     * this buffer directly (e.g. building a VkDescriptorBufferInfo for a descriptor set
     * assembled by hand). This buffer is single-instance (not a per-frame ring, unlike
     * BodyOctreeSceneNode's own instance buffer), so there is no frame-index ambiguity.
     */
    VkBuffer GetBufferHandle() const { return buffer_; }

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateBuffer(Vixen::Vulkan::Resources::VulkanDevice* device, VkDeviceSize sizeBytes);
    void DestroyBuffer();

    VkBuffer       buffer_   = VK_NULL_HANDLE;
    VkDeviceMemory memory_   = VK_NULL_HANDLE;
    VkDeviceSize   sizeBytes_ = 0;   // current allocated size
};

} // namespace Vixen::RenderGraph
