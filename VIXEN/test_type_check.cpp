// Test to debug why VkSwapchainKHR is not recognized
#include "Headers.h"
#include "RenderGraph/include/Data/Core/ResourceTypeTraits.h"
#include "RenderGraph/include/Data/Core/CompileTimeResourceSystem.h"
#include <iostream>

using namespace Vixen::RenderGraph;

int main() {
    // Test 1: Check if VkSwapchainKHR is defined
    std::cout << "sizeof(VkSwapchainKHR) = " << sizeof(VkSwapchainKHR) << "\n";

    // Test 2: Check if it's registered
    std::cout << "IsRegisteredType<VkSwapchainKHR>::value = "
              << IsRegisteredType<VkSwapchainKHR>::value << "\n";

    // Test 3: Check ResourceTypeTraits
    std::cout << "ResourceTypeTraits<VkSwapchainKHR>::isValid = "
              << ResourceTypeTraits<VkSwapchainKHR>::isValid << "\n";

    // Test 4: Check StripContainer
    using BaseType = typename StripContainer<VkSwapchainKHR>::Type;
    std::cout << "StripContainer<VkSwapchainKHR>::isContainer = "
              << StripContainer<VkSwapchainKHR>::isContainer << "\n";

    return 0;
}