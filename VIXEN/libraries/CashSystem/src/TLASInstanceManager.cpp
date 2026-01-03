#include "pch.h"
#include "TLASInstanceManager.h"

#include <cstring>
#include <stdexcept>

namespace CashSystem {

// ============================================================================
// INSTANCE LIFECYCLE
// ============================================================================

TLASInstanceManager::InstanceId TLASInstanceManager::AddInstance(const Instance& instance) {
    InstanceId id;

    if (!freeList_.empty()) {
        // Reuse a recycled slot
        id = freeList_.back();
        freeList_.pop_back();
        instances_[id] = instance;
        instances_[id].active = true;
    } else {
        // Allocate new slot
        id = static_cast<InstanceId>(instances_.size());
        instances_.push_back(instance);
        instances_.back().active = true;
    }

    ++activeCount_;
    SetDirtyLevel(DirtyLevel::StructuralChange);

    return id;
}

bool TLASInstanceManager::UpdateTransform(InstanceId id, const glm::mat3x4& transform) {
    if (id >= instances_.size() || !instances_[id].active) {
        return false;
    }

    instances_[id].transform = transform;
    SetDirtyLevel(DirtyLevel::TransformsOnly);

    return true;
}

bool TLASInstanceManager::UpdateBLASAddress(InstanceId id, VkDeviceAddress blasAddress) {
    if (id >= instances_.size() || !instances_[id].active) {
        return false;
    }

    instances_[id].blasAddress = blasAddress;
    SetDirtyLevel(DirtyLevel::StructuralChange);  // BLAS change requires full rebuild

    return true;
}

bool TLASInstanceManager::RemoveInstance(InstanceId id) {
    if (id >= instances_.size() || !instances_[id].active) {
        return false;
    }

    instances_[id].active = false;
    freeList_.push_back(id);
    --activeCount_;
    SetDirtyLevel(DirtyLevel::StructuralChange);

    return true;
}

void TLASInstanceManager::Clear() {
    if (activeCount_ > 0) {
        SetDirtyLevel(DirtyLevel::StructuralChange);
    }

    instances_.clear();
    freeList_.clear();
    activeCount_ = 0;
}

// ============================================================================
// QUERY
// ============================================================================

const TLASInstanceManager::Instance* TLASInstanceManager::GetInstance(InstanceId id) const {
    if (id >= instances_.size() || !instances_[id].active) {
        return nullptr;
    }
    return &instances_[id];
}

// ============================================================================
// DIRTY TRACKING
// ============================================================================

void TLASInstanceManager::SetDirtyLevel(DirtyLevel level) {
    // Never demote dirty level (StructuralChange > TransformsOnly > Clean)
    if (static_cast<uint8_t>(level) > static_cast<uint8_t>(dirtyLevel_)) {
        dirtyLevel_ = level;
    }
}

// ============================================================================
// VULKAN INSTANCE GENERATION
// ============================================================================

void TLASInstanceManager::GenerateVulkanInstances(
    std::vector<VkAccelerationStructureInstanceKHR>& out) const {

    out.reserve(out.size() + activeCount_);

    for (const auto& inst : instances_) {
        if (!inst.active) {
            continue;
        }

        VkAccelerationStructureInstanceKHR vkInst{};

        // Copy 3x4 transform (row-major, glm stores column-major so we transpose)
        // VkTransformMatrixKHR expects row-major float[3][4]
        const float* src = glm::value_ptr(inst.transform);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                // glm::mat3x4 is column-major: column * 3 + row
                // VkTransformMatrixKHR is row-major: row * 4 + col
                vkInst.transform.matrix[row][col] = src[col * 3 + row];
            }
        }

        vkInst.instanceCustomIndex = inst.customIndex & 0x00FFFFFFu;  // 24 bits
        vkInst.mask = inst.mask;
        vkInst.instanceShaderBindingTableRecordOffset = 0;
        vkInst.flags = inst.flags;
        vkInst.accelerationStructureReference = inst.blasAddress;

        out.push_back(vkInst);
    }
}

std::vector<VkAccelerationStructureInstanceKHR> TLASInstanceManager::GenerateVulkanInstances() const {
    std::vector<VkAccelerationStructureInstanceKHR> result;
    GenerateVulkanInstances(result);
    return result;
}

} // namespace CashSystem
