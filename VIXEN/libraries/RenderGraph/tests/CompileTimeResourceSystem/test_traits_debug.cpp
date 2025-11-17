// Debug test to see if types are properly registered
#include "Headers.h"
#include "Data/Core/ResourceTypeTraits.h"
#include "Data/Core/CompileTimeResourceSystem.h"

using namespace Vixen::RenderGraph;

// Compile-time checks
static_assert(IsRegisteredType<VkSwapchainKHR>::value, "VkSwapchainKHR should be registered");
static_assert(ResourceTypeTraits<VkSwapchainKHR>::isValid, "VkSwapchainKHR should be valid in traits");

int main() {
    // If this compiles, the assertions passed
    return 0;
}