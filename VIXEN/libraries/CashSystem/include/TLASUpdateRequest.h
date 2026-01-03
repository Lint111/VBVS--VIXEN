#pragma once

#include "Updates/UpdateRequest.h"
#include "TLASInstanceManager.h"

#include <vulkan/vulkan.h>

// Forward declarations
namespace Vixen::Vulkan::Resources {
    class VulkanDevice;
}

namespace CashSystem {
    class DynamicTLAS;
}

namespace CashSystem {

/**
 * @brief TLAS rebuild/update request
 *
 * Part of Phase 3.5: Generalized Update API
 *
 * Records acceleration structure build commands for dynamic TLAS.
 * Uses VK_BUILD_MODE_UPDATE when only transforms changed.
 *
 * Responsibilities:
 * - Loading RT function pointers (vkCmdBuildAccelerationStructuresKHR)
 * - Recording build commands to command buffer
 * - DynamicTLAS remains a pure state holder
 */
class TLASUpdateRequest : public ResourceManagement::UpdateRequestBase {
public:
    Vixen::Vulkan::Resources::VulkanDevice* device = nullptr;
    DynamicTLAS* tlas = nullptr;
    const TLASInstanceManager* instanceManager = nullptr;
    TLASInstanceManager::DirtyLevel dirtyLevel = TLASInstanceManager::DirtyLevel::Clean;

    TLASUpdateRequest()
        : ResourceManagement::UpdateRequestBase(ResourceManagement::UpdateType::TLASRebuild)
    {}

    TLASUpdateRequest(
        Vixen::Vulkan::Resources::VulkanDevice* dev,
        DynamicTLAS* t,
        const TLASInstanceManager* mgr,
        TLASInstanceManager::DirtyLevel dirty,
        uint32_t imgIndex)
        : ResourceManagement::UpdateRequestBase(ResourceManagement::UpdateType::TLASRebuild)
        , device(dev)
        , tlas(t)
        , instanceManager(mgr)
        , dirtyLevel(dirty)
    {
        imageIndex = imgIndex;
    }

    ~TLASUpdateRequest() override = default;

    /**
     * @brief Record TLAS build/update commands
     *
     * Delegates to DynamicTLAS::RecordBuild() which handles:
     * - Instance buffer upload
     * - Build mode selection (BUILD vs UPDATE)
     * - Acceleration structure build command
     *
     * @param cmd Active command buffer
     */
    void Record(VkCommandBuffer cmd) override;

    /**
     * @brief TLAS builds are relatively expensive
     */
    uint32_t GetEstimatedCost() const override { return 100; }

    /**
     * @brief TLAS builds require memory barriers
     */
    bool RequiresBarriers() const override { return true; }
};

} // namespace CashSystem
