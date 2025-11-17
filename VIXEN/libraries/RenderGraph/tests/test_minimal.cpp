// Minimal test to verify ResourceV3 type registration works
#include "Headers.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include <iostream>

using namespace Vixen::RenderGraph;

int main() {
    // Test 1: VkSwapchainKHR is registered directly in ResourceV3
    constexpr bool swapchain_registered = IsRegisteredType<VkSwapchainKHR>::value;
    std::cout << "IsRegisteredType<VkSwapchainKHR> = " << swapchain_registered << "\n";

    // Test 2: VkImageView is registered
    constexpr bool imageview_registered = IsRegisteredType<VkImageView>::value;
    std::cout << "IsRegisteredType<VkImageView> = " << imageview_registered << "\n";

    // Test 3: Check if the base type extraction works
    using VectorBaseType = typename StripContainer<std::vector<VkImageView>>::Type;
    constexpr bool is_vector = StripContainer<std::vector<VkImageView>>::isContainer;
    std::cout << "std::vector<VkImageView> is container = " << is_vector << "\n";
    std::cout << "Base type of vector<VkImageView> is registered = " << IsRegisteredType<VectorBaseType>::value << "\n";

    // Test 4: ResourceTypeTraits - let's see what's happening
    std::cout << "\nResourceTypeTraits checks:\n";
    std::cout << "ResourceTypeTraits<VkSwapchainKHR>::isValid = " << ResourceTypeTraits<VkSwapchainKHR>::isValid << "\n";

    // Debug the BaseType extraction
    using SwapchainBaseType = typename ResourceTypeTraits<VkSwapchainKHR>::BaseType;
    std::cout << "ResourceTypeTraits<VkSwapchainKHR>::BaseType is same as VkSwapchainKHR = " << std::is_same_v<SwapchainBaseType, VkSwapchainKHR> << "\n";
    std::cout << "ResourceTypeTraits<VkSwapchainKHR>::BaseType is registered = " << IsRegisteredType<SwapchainBaseType>::value << "\n";

    // Check StripContainer directly
    using SwapchainStripped = typename StripContainer<VkSwapchainKHR>::Type;
    std::cout << "StripContainer<VkSwapchainKHR>::Type is same as VkSwapchainKHR = " << std::is_same_v<SwapchainStripped, VkSwapchainKHR> << "\n";
    std::cout << "Direct check: IsRegisteredType<" << typeid(SwapchainStripped).name() << "> = " << IsRegisteredType<SwapchainStripped>::value << "\n";

    // Test 5: Vector traits
    std::cout << "ResourceTypeTraits<std::vector<VkImageView>>::isValid = " << ResourceTypeTraits<std::vector<VkImageView>>::isValid << "\n";
    std::cout << "ResourceTypeTraits<std::vector<VkImageView>>::isContainer = " << ResourceTypeTraits<std::vector<VkImageView>>::isContainer << "\n";

    // Compile-time assertions - only test what we know should work
    static_assert(swapchain_registered, "VkSwapchainKHR should be registered");
    static_assert(imageview_registered, "VkImageView should be registered");
    static_assert(is_vector, "vector should be detected as container");

    // Now this should work!
    static_assert(ResourceTypeTraits<VkSwapchainKHR>::isValid, "VkSwapchainKHR should be valid in traits");
    static_assert(ResourceTypeTraits<std::vector<VkImageView>>::isValid, "vector<VkImageView> should be valid in traits");

    std::cout << "\nDone - check the output above to debug the issue\n";
    return 0;
}