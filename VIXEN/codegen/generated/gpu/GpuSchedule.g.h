#pragma once
#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>
namespace Vixen::Gpu {
inline constexpr uint32_t ExposureTonemapSystemAccessSlotCount = 4u;
struct ScheduleBarrier { const char* before; const char* after; const char* resource; bool image; VkImageLayout oldLayout; VkImageLayout newLayout; VkPipelineStageFlags2 srcStage; VkPipelineStageFlags2 dstStage; VkAccessFlags2 srcAccess; VkAccessFlags2 dstAccess; };
inline constexpr std::array<ScheduleBarrier, 6> ScheduleBarriers = {{
    {"vixen:ExposureTonemapSystem", "vixen:exposure-meter", "state:exposure-meter.histogram", false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
    {"vixen:ExposureTonemapSystem", "vixen:exposure-reduce", "state:exposure-meter.log2Exposure", false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
    {"vixen:ExposureTonemapSystem", "vixen:exposure-reduce", "state:exposure-meter.meteredEV100", false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
    {"vixen:ExposureTonemapSystem", "vixen:exposure-reduce", "state:exposure-meter.state", false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
    {"vixen:ExposureTonemapSystem", "vixen:exposure-reduce", "state:exposure-meter.targetLog2Exposure", false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
    {"vixen:exposure-meter", "vixen:exposure-reduce", "state:exposure-meter.histogram", false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT},
}};
struct TextureAllocation { const char* owner; const char* member; uint32_t dimensions; VkFormat format; uint32_t texelStride; uint32_t slots; VkImageUsageFlags imageUsage; VkBufferUsageFlags bufferUsage; const char* extentSource; const char* follows; uint32_t mips; bool fullMipChain; bool storageView; bool sampledView; };
inline constexpr std::array<TextureAllocation, 2> TextureAllocations = {{
    {"Vixen.Codegen.Gpu.StateFrameTargetsRef", "OutputImage", 2u, VK_FORMAT_R8G8B8A8_UNORM, 0u, 2u, VK_IMAGE_USAGE_STORAGE_BIT, 0u, "presentation", "", 1u, false, true, false},
    {"Vixen.Codegen.Gpu.StateFrameTargetsRef", "SceneRadianceHistoryOutput", 2u, VK_FORMAT_R16G16B16A16_SFLOAT, 0u, 1u, VK_IMAGE_USAGE_STORAGE_BIT, 0u, "presentation", "", 1u, false, true, false},
}};
} // namespace Vixen::Gpu
