#include <gtest/gtest.h>

#include "Data/Core/ResourceV3.h"

using namespace Vixen::RenderGraph;

TEST(PassThroughStorage_HandleTypes, CompileTimeRegistrationAndGetSet) {
    // This test ensures the common handle types used by nodes are registered
    // with the PassThroughStorage Resource system. If a type is missing from the
    // type registry, the call to SetHandle/GetHandle will fail to
    // compile (static_assert in PassThroughStorage).

    Resource r;

    // Pointer-like Windows handles (HWND, HINSTANCE)
    HWND hw = reinterpret_cast<HWND>(0x1234);
    r.SetHandle<HWND>(hw);
    EXPECT_EQ(r.GetHandle<HWND>(), hw);

    HINSTANCE hi = reinterpret_cast<HINSTANCE>(0x5678);
    r.SetHandle<HINSTANCE>(hi);
    EXPECT_EQ(r.GetHandle<HINSTANCE>(), hi);

    // Vulkan instance handle
    VkInstance vi = reinterpret_cast<VkInstance>(uintptr_t(0x9));
    r.SetHandle<VkInstance>(vi);
    EXPECT_EQ(r.GetHandle<VkInstance>(), vi);

    // Scalar types registered in the registry
    r.SetHandle<uint32_t>(42u);
    EXPECT_EQ(r.GetHandle<uint32_t>(), 42u);

    r.SetHandle<uint64_t>(123456789ull);
    EXPECT_EQ(r.GetHandle<uint64_t>(), 123456789ull);
}
