#include "ErrorCatalogVkAdapter.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace
{
using namespace yeroket::error_catalog;
using namespace vixen::error_adapter;

bool Contains(StringView text, const char* needle) noexcept
{
    std::size_t needle_size = 0;
    while (needle[needle_size] != '\0') ++needle_size;
    if (needle_size > text.size) return false;
    for (std::uint32_t start = 0; start + needle_size <= text.size; ++start)
    {
        bool equal = true;
        for (std::size_t offset = 0; offset < needle_size; ++offset)
            if (text.data[start + offset] != needle[offset]) { equal = false; break; }
        if (equal) return true;
    }
    return false;
}
}

int main() noexcept
{
    static_assert(std::is_standard_layout<StringView>::value);
    static_assert(std::is_trivially_copyable<DescriptorView>::value);
    static_assert(std::is_standard_layout<Location>::value);
    static_assert(std::is_trivially_copyable<Occurrence>::value);
    static_assert(std::is_standard_layout<MappedError>::value);
    static_assert(std::is_trivially_copyable<MappedError>::value);
    static_assert(noexcept(FindByCanonicalKey(Literal("kernel.legacy.pipe001"))));
    static_assert(noexcept(FindByDisplayCode(Literal("VIXEN001"))));
    static_assert(noexcept(FormatOccurrence(Occurrence{}, nullptr, 0)));
    static_assert(noexcept(FromVkResult(VK_ERROR_DEVICE_LOST)));
    static_assert(noexcept(ShaderCompileFailure()));

    const DescriptorView* kernel = FindByDisplayCode(Literal("PIPE001"));
    const DescriptorView* vixen = FindByDisplayCode(Literal("VIXEN001"));
    assert(kernel != nullptr);
    assert(vixen != nullptr);
    assert(std::strcmp(vixen->namespace_name.data, "vixen") == 0);

    const MappedError device_lost = FromVkResult(VK_ERROR_DEVICE_LOST);
    assert(device_lost.definition == vixen);
    assert(device_lost.recovery == RecoveryClass::Recreate);

    const MappedError swapchain = FromVkResult(VK_ERROR_OUT_OF_DATE_KHR);
    assert(swapchain.definition == FindByDisplayCode(Literal("VIXEN002")));
    assert(swapchain.recovery == RecoveryClass::Recreate);

    const MappedError out_of_memory = FromVkResult(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    assert(out_of_memory.definition == FindByDisplayCode(Literal("VIXEN003")));
    assert(out_of_memory.recovery == RecoveryClass::Degrade);

    const MappedError shader = ShaderCompileFailure();
    assert(shader.definition == FindByDisplayCode(Literal("VIXEN004")));
    assert(shader.recovery == RecoveryClass::Disable);

    const Location location{
        Literal("vixen/renderer/device"), 42, 7, 42, 7, Literal("device")};
    const Parameter parameters[]{{Literal("result"), Literal("VK_ERROR_DEVICE_LOST")}};
    const Occurrence occurrence{device_lost.definition, location, nullptr, 0, parameters, 1};
    char output[2048]{};
    const FormatResult result = FormatOccurrence(occurrence, output, sizeof(output));
    assert(result.complete);
    const StringView formatted{output, static_cast<std::uint32_t>(result.size)};
    assert(Contains(formatted, "what: The renderer lost its Vulkan device after VK_ERROR_DEVICE_LOST."));
    assert(Contains(formatted, "why: The device or driver stopped accepting work"));
    assert(Contains(formatted, "fix: Recreate the renderer device"));
    assert(Contains(formatted, "doc: vixen-renderer-device-lost"));

    std::printf("VIXEN error catalog smoke: descriptors=%zu kernel=%.*s vixen=%.*s recovery=%s formatted=%zu bytes\n",
        DescriptorCount,
        static_cast<int>(kernel->display_code.size), kernel->display_code.data,
        static_cast<int>(vixen->display_code.size), vixen->display_code.data,
        RecoveryName(device_lost.recovery), result.size);
    return 0;
}
