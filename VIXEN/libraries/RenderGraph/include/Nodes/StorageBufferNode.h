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
