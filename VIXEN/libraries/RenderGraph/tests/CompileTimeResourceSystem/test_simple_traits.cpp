// Minimal test to verify ResourceTypeTraits works
#include "RenderGraph/include/Data/Core/ResourceTypeTraits.h"
#include "RenderGraph/include/Data/Core/CompileTimeResourceSystem.h"
#include <vector>

using namespace Vixen::RenderGraph;

// Simple compile-time tests
static_assert(IsRegisteredType<VkSwapchainKHR>::value, "VkSwapchainKHR should be registered");
static_assert(IsRegisteredType<VkImageView>::value, "VkImageView should be registered");

// Test ResourceTypeTraits
static_assert(ResourceTypeTraits<VkSwapchainKHR>::isValid, "VkSwapchainKHR should be valid");
static_assert(ResourceTypeTraits<std::vector<VkImageView>>::isValid, "vector<VkImageView> should be valid");

int main() {
    return 0;
}