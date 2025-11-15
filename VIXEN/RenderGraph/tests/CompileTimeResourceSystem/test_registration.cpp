// Minimal test to debug registration issue
#include "RenderGraph/include/Data/Core/CompileTimeResourceSystem.h"
#include "RenderGraph/include/Core/FieldExtractor.h"
#include <iostream>

using namespace Vixen::RenderGraph;

int main() {
    // Test 1: Direct registration check
    std::cout << "IsRegisteredType<VkSwapchainKHR>::value = "
              << IsRegisteredType<VkSwapchainKHR>::value << "\n";

    // Test 2: ResourceTypeTraits check
    std::cout << "ResourceTypeTraits<VkSwapchainKHR>::isValid = "
              << ResourceTypeTraits<VkSwapchainKHR>::isValid << "\n";

    // Test 3: StripContainer check
    using TestType = VkSwapchainKHR;
    using BaseType = typename StripContainer<TestType>::Type;
    std::cout << "StripContainer<VkSwapchainKHR>::isContainer = "
              << StripContainer<TestType>::isContainer << "\n";
    std::cout << "Base type is same as original = "
              << std::is_same_v<BaseType, TestType> << "\n";

    return 0;
}