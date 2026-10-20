#pragma once
#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>
namespace Vixen::Gpu {
inline constexpr uint32_t ExposureTonemapSystemAccessSlotCount = 4u;
struct ScheduleBarrier { const char* before; const char* after; const char* resource; bool image; VkImageLayout oldLayout; VkImageLayout newLayout; VkPipelineStageFlags2 srcStage; VkPipelineStageFlags2 dstStage; VkAccessFlags2 srcAccess; VkAccessFlags2 dstAccess; };
inline constexpr std::array<ScheduleBarrier, 0> ScheduleBarriers = {{
}};
struct TextureAllocation { const char* owner; const char* member; uint32_t dimensions; VkFormat format; uint32_t texelStride; uint32_t slots; VkImageUsageFlags imageUsage; VkBufferUsageFlags bufferUsage; const char* extentSource; const char* follows; uint32_t mips; bool fullMipChain; bool storageView; bool sampledView; };
inline constexpr std::array<TextureAllocation, 2> TextureAllocations = {{
    {"Vixen.Codegen.Gpu.StateFrameTargetsRef", "OutputImage", 2u, VK_FORMAT_R8G8B8A8_UNORM, 0u, 2u, VK_IMAGE_USAGE_STORAGE_BIT, 0u, "presentation", "", 1u, false, true, false},
    {"Vixen.Codegen.Gpu.StateFrameTargetsRef", "SceneRadianceHistoryOutput", 2u, VK_FORMAT_R16G16B16A16_SFLOAT, 0u, 1u, VK_IMAGE_USAGE_STORAGE_BIT, 0u, "presentation", "", 1u, false, true, false},
}};
} // namespace Vixen::Gpu
