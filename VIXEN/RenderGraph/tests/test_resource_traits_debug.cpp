// Debug test to verify ResourceTypeTraits works with ResourceV3
#include "Data/Core/ResourceTypeTraits.h"
#include "Data/Core/ResourceV3.h"
#include <vector>
#include <iostream>

using namespace Vixen::RenderGraph;

int main() {
    // Test basic type registration
    std::cout << "VkSwapchainKHR registered: "
              << IsRegisteredType<VkSwapchainKHR>::value << "\n";

    std::cout << "VkImageView registered: "
              << IsRegisteredType<VkImageView>::value << "\n";

    // Test ResourceTypeTraits
    std::cout << "ResourceTypeTraits<VkSwapchainKHR>::isValid: "
              << ResourceTypeTraits<VkSwapchainKHR>::isValid << "\n";

    std::cout << "ResourceTypeTraits<std::vector<VkImageView>>::isValid: "
              << ResourceTypeTraits<std::vector<VkImageView>>::isValid << "\n";

    // Test StripContainer
    using VectorType = std::vector<VkImageView>;
    using BaseType = typename StripContainer<VectorType>::Type;
    std::cout << "StripContainer<vector<VkImageView>>::isContainer: "
              << StripContainer<VectorType>::isContainer << "\n";
    std::cout << "Base type of vector<VkImageView> registered: "
              << IsRegisteredType<BaseType>::value << "\n";

    return 0;
}