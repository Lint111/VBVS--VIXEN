// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.

#include "Nodes/PhotonCellTableNode.h"

#include "Nodes/PhotonCellParamsConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Logger.h"
#include "VulkanDevice.h"

#include "PhotonCells.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;
using namespace Vixen::SVO::PhotonCells;

std::unique_ptr<NodeInstance> PhotonCellTableNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<PhotonCellTableNode>(
        instanceName, const_cast<PhotonCellTableNodeType*>(this));
}

PhotonCellTableNode::PhotonCellTableNode(const std::string& instanceName, NodeType* nodeType)
    : StorageBufferNode(instanceName, nodeType) {
    SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kTableBytes);
}

void PhotonCellTableNode::RunDiagnostic(StorageBufferNode& hitRecords,
                                        const PhotonCellParamsConfigNode& params,
                                        VulkanDevice* device,
                                        Logger* logger,
                                        uint64_t sampleFrame) const {
    if (!device || !logger) return;
    vkDeviceWaitIdle(device->device);

    const uint32_t generation = params.GenerationForDiagnostic(sampleFrame);
    std::unordered_map<uint64_t, uint32_t> gpuCounts;
    std::unordered_map<uint64_t, std::array<int32_t, 3>> gpuSums;
    uint32_t gpuOccupied = 0;
    uint32_t gpuTotal = 0;
    if (void* mapped = MapForReadback(device)) {
        const auto* words = reinterpret_cast<const uint32_t*>(mapped);
        for (uint32_t slot = 0; slot < kCapacity; ++slot) {
            const size_t base = static_cast<size_t>(slot) * kEntryWords;
            const uint32_t keyLo = words[base + 0];
            const uint32_t keyHi = words[base + 1];
            const uint32_t storedGeneration = keyHi & kGenerationMask;
            if ((keyLo & kKeyTagBit) == 0u || storedGeneration != generation ||
                storedGeneration == kTransientGeneration) continue;
            const uint64_t key = (static_cast<uint64_t>(keyHi & ~kGenerationMask) << 32u) | keyLo;
            ++gpuOccupied;
            gpuCounts[key] += words[base + 2];
            gpuTotal += words[base + 2];
            auto& sums = gpuSums[key];
            for (uint32_t channel = 0; channel < 3; ++channel) {
                int32_t value = 0;
                std::memcpy(&value, &words[base + 3 + channel], sizeof(value));
                sums[channel] += value;
            }
        }
        UnmapReadback(device);
    }

    std::unordered_map<uint64_t, uint32_t> cpuCounts;
    uint32_t cpuTotal = 0;
    constexpr size_t kHitRecordBytes = 64u;
    const float primaryCoef = params.PrimaryCoef();
    if (void* mapped = hitRecords.MapForReadback(device)) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(mapped);
        const size_t recordCount = static_cast<size_t>(hitRecords.GetSizeBytes()) /
                                   kHitRecordBytes;
        for (size_t index = 0; index < recordCount; ++index) {
            const uint8_t* record = bytes + index * kHitRecordBytes;
            uint32_t flags = 0;
            float hitT = 0.0f;
            glm::vec3 worldPos{0.0f};
            std::memcpy(&flags, record + 44, sizeof(flags));
            if ((flags & 0x1u) == 0u || (flags & 0x4u) != 0u) continue;
            std::memcpy(&hitT, record + 28, sizeof(hitT));
            std::memcpy(&worldPos, record + 32, sizeof(worldPos));
            const CellKey cell = MakeCellKey(worldPos, hitT * primaryCoef +
                                                       params.PrimaryBias(),
                                              params.CellSize0());
            const uint32_t keyLo = PackCellKeyLo(cell.cell);
            const uint32_t keyHiBase = PackCellKeyHiBase(kAnchorId, cell.level);
            if (keyLo == 0u) continue;
            const uint64_t key = (static_cast<uint64_t>(keyHiBase) << 32u) | keyLo;
            ++cpuCounts[key];
            ++cpuTotal;
        }
        hitRecords.UnmapReadback(device);
    }

    uint32_t mismatchedKeys = 0;
    for (const auto& [key, expected] : cpuCounts) {
        const auto it = gpuCounts.find(key);
        if (it == gpuCounts.end() || it->second != expected) ++mismatchedKeys;
    }
    for (const auto& [key, actual] : gpuCounts) {
        if (!cpuCounts.contains(key) && actual != 0u) ++mismatchedKeys;
    }
    logger->Info("[PhotonCellDiag]" +
                 std::string(sampleFrame == 0 ? " frame=shutdown" :
                             " frame=" + std::to_string(sampleFrame)) +
                 " generation=" + std::to_string(generation) +
                 " GPU: occupied=" + std::to_string(gpuOccupied) +
                 " deposits=" + std::to_string(gpuTotal) +
                 " CPU: keys=" + std::to_string(cpuCounts.size()) +
                 " deposits=" + std::to_string(cpuTotal) +
                 " mismatchedKeys=" + std::to_string(mismatchedKeys));
    for (const auto& [key, sums] : gpuSums) {
        const uint32_t count = gpuCounts[key];
        if (count == 0u) continue;
        char keyText[9]{};
        std::snprintf(keyText, sizeof(keyText), "%08X", static_cast<uint32_t>(key));
        logger->Info("[PhotonCellDiag] keyLo=0x" + std::string(keyText) +
                     " count=" + std::to_string(count) +
                     " sumFlux=" + std::to_string(sums[0]) + "," +
                     std::to_string(sums[1]) + "," + std::to_string(sums[2]));
    }
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::PhotonCellTableNodeType);
