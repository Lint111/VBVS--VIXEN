#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

#include "YeroketErrorCatalog.g.h"

namespace vixen::error_adapter
{
using yeroket::error_catalog::DescriptorView;
using yeroket::error_catalog::FindByCanonicalKey;
using yeroket::error_catalog::Literal;
using yeroket::error_catalog::StringView;

enum class RecoveryClass : std::uint8_t
{
    Retry,
    Recreate,
    Degrade,
    Disable,
    CleanStop,
};

struct MappedError
{
    const DescriptorView* definition;
    RecoveryClass recovery;
};

inline MappedError Map(StringView key, RecoveryClass recovery) noexcept
{
    return MappedError{FindByCanonicalKey(key), recovery};
}

inline MappedError FromVkResult(VkResult result) noexcept
{
    switch (result)
    {
        case VK_ERROR_DEVICE_LOST:
            return Map(Literal("vixen.renderer.device-lost"), RecoveryClass::Recreate);
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_SUBOPTIMAL_KHR:
            return Map(Literal("vixen.renderer.swapchain-out-of-date"), RecoveryClass::Recreate);
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return Map(Literal("vixen.renderer.out-of-device-memory"), RecoveryClass::Degrade);
        default:
            return MappedError{nullptr, RecoveryClass::CleanStop};
    }
}

inline MappedError ShaderCompileFailure() noexcept
{
    return Map(Literal("vixen.renderer.shader-compile-failed"), RecoveryClass::Disable);
}

inline const char* RecoveryName(RecoveryClass recovery) noexcept
{
    switch (recovery)
    {
        case RecoveryClass::Retry: return "retry";
        case RecoveryClass::Recreate: return "recreate";
        case RecoveryClass::Degrade: return "degrade";
        case RecoveryClass::Disable: return "disable";
        case RecoveryClass::CleanStop: return "clean-stop";
    }
    return "unknown";
}
}
