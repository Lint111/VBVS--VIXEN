// ============================================================================
// STANDALONE TEST: Array Type Validation (TRIMMED BUILD COMPATIBLE)
// ============================================================================
// This test validates compile-time type traits for the array-aware type system.
// NO Vulkan runtime needed - only headers!
// Compatible with VULKAN_TRIMMED_BUILD (headers only)

#include "Core/ResourceVariant.h"
#include "Core/ResourceTypeTraits.h"

#include <iostream>
#include <vector>
#include <array>
#include <variant>

// Use the RenderGraph namespace
using namespace Vixen::RenderGraph;

// Forward declare an unknown type for negative tests
struct UnknownType {};

// ============================================================================
// COMPILE-TIME VALIDATION (static_assert)
// ============================================================================
// If any of these fail, compilation fails = early error detection!

namespace CompileTimeTests {

// ----------------------------------------------------------------------------
// Test 1: Scalar types (should be valid - registered in RESOURCE_TYPE_REGISTRY)
// ----------------------------------------------------------------------------
static_assert(ResourceTypeTraits<VkImage>::isValid,
    "VkImage should be valid (registered scalar)");
static_assert(ResourceTypeTraits<VkBuffer>::isValid,
    "VkBuffer should be valid (registered scalar)");
static_assert(ResourceTypeTraits<VkImageView>::isValid,
    "VkImageView should be valid (registered scalar)");
static_assert(ResourceTypeTraits<VkSampler>::isValid,
    "VkSampler should be valid (registered scalar)");

// ----------------------------------------------------------------------------
// Test 2: Vector types (should auto-validate from scalar)
// ----------------------------------------------------------------------------
static_assert(ResourceTypeTraits<std::vector<VkImage>>::isValid,
    "vector<VkImage> should be valid (auto from scalar)");
static_assert(ResourceTypeTraits<std::vector<VkBuffer>>::isValid,
    "vector<VkBuffer> should be valid (auto from scalar)");
static_assert(ResourceTypeTraits<std::vector<VkImageView>>::isValid,
    "vector<VkImageView> should be valid (auto from scalar)");

// Container detection
static_assert(ResourceTypeTraits<std::vector<VkImage>>::isVector,
    "vector<VkImage> should be detected as vector");
static_assert(!ResourceTypeTraits<VkImage>::isVector,
    "VkImage should not be detected as vector");

// ----------------------------------------------------------------------------
// Test 3: Array types (should auto-validate from scalar)
// ----------------------------------------------------------------------------
static_assert(ResourceTypeTraits<std::array<VkImage, 1>>::isValid,
    "array<VkImage, 1> should be valid");
static_assert(ResourceTypeTraits<std::array<VkImage, 10>>::isValid,
    "array<VkImage, 10> should be valid");
static_assert(ResourceTypeTraits<std::array<VkBuffer, 5>>::isValid,
    "array<VkBuffer, 5> should be valid");

// Array size detection
static_assert(ResourceTypeTraits<std::array<VkImage, 10>>::isArray,
    "array<VkImage, 10> should be detected as array");
static_assert(ResourceTypeTraits<std::array<VkImage, 10>>::arraySize == 10,
    "array size should be detected correctly");

// ----------------------------------------------------------------------------
// Test 4: ResourceVariant itself (macro-generated variant)
// ----------------------------------------------------------------------------
static_assert(ResourceTypeTraits<ResourceVariant>::isValid,
    "ResourceVariant itself should be valid");
static_assert(ResourceTypeTraits<ResourceVariant>::isResourceVariant,
    "ResourceVariant should be detected as ResourceVariant");

// Vector/array of ResourceVariant
static_assert(ResourceTypeTraits<std::vector<ResourceVariant>>::isValid,
    "vector<ResourceVariant> should be valid");
static_assert(ResourceTypeTraits<std::array<ResourceVariant, 5>>::isValid,
    "array<ResourceVariant, 5> should be valid");

// ----------------------------------------------------------------------------
// Test 5: Custom variants (type-safe subsets)
// ----------------------------------------------------------------------------
using TextureHandles = std::variant<VkImage, VkImageView, VkSampler>;
using BufferHandles = std::variant<VkBuffer, VkBufferView>;

static_assert(ResourceTypeTraits<TextureHandles>::isValid,
    "Custom variant with registered types should be valid");
static_assert(ResourceTypeTraits<TextureHandles>::isCustomVariant,
    "TextureHandles should be detected as custom variant");
static_assert(ResourceTypeTraits<BufferHandles>::isValid,
    "BufferHandles custom variant should be valid");

// Vector/array of custom variants
static_assert(ResourceTypeTraits<std::vector<TextureHandles>>::isValid,
    "vector<TextureHandles> should be valid");
static_assert(ResourceTypeTraits<std::array<BufferHandles, 3>>::isValid,
    "array<BufferHandles, 3> should be valid");

// ----------------------------------------------------------------------------
// Test 6: Invalid types (should be rejected)
// ----------------------------------------------------------------------------
static_assert(!ResourceTypeTraits<UnknownType>::isValid,
    "Unregistered type should be invalid");
static_assert(!ResourceTypeTraits<std::vector<UnknownType>>::isValid,
    "vector<UnknownType> should be invalid");
static_assert(!ResourceTypeTraits<std::array<UnknownType, 5>>::isValid,
    "array<UnknownType, 5> should be invalid");

// Custom variant with unregistered type
using InvalidVariant = std::variant<VkImage, UnknownType>;
static_assert(!ResourceTypeTraits<InvalidVariant>::isValid,
    "Custom variant with unregistered type should be invalid");

// ----------------------------------------------------------------------------
// Test 7: Base type extraction
// ----------------------------------------------------------------------------
static_assert(std::is_same_v<ResourceTypeTraits<VkImage>::BaseType, VkImage>,
    "Scalar base type should be itself");
static_assert(std::is_same_v<ResourceTypeTraits<std::vector<VkImage>>::BaseType, VkImage>,
    "Vector base type should be element type");
static_assert(std::is_same_v<ResourceTypeTraits<std::array<VkBuffer, 10>>::BaseType, VkBuffer>,
    "Array base type should be element type");

} // namespace CompileTimeTests

// ============================================================================
// RUNTIME VALIDATION (informational output)
// ============================================================================

template<typename T>
void printTypeInfo(const char* typeName) {
    std::cout << "  " << typeName << ":\n";
    std::cout << "    isValid: " << ResourceTypeTraits<T>::isValid << "\n";
    std::cout << "    isVector: " << ResourceTypeTraits<T>::isVector << "\n";
    std::cout << "    isArray: " << ResourceTypeTraits<T>::isArray << "\n";
    if (ResourceTypeTraits<T>::isArray) {
        std::cout << "    arraySize: " << ResourceTypeTraits<T>::arraySize << "\n";
    }
    std::cout << "    isResourceVariant: " << ResourceTypeTraits<T>::isResourceVariant << "\n";
    std::cout << "    isCustomVariant: " << ResourceTypeTraits<T>::isCustomVariant << "\n";
}

int main() {
    std::cout << "\n=== Array Type Validation Tests ===\n\n";

    std::cout << "Test 1: Scalar types\n";
    printTypeInfo<VkImage>("VkImage");
    printTypeInfo<VkBuffer>("VkBuffer");
    std::cout << "\n";

    std::cout << "Test 2: Vector types\n";
    printTypeInfo<std::vector<VkImage>>("vector<VkImage>");
    printTypeInfo<std::vector<VkBuffer>>("vector<VkBuffer>");
    std::cout << "\n";

    std::cout << "Test 3: Array types\n";
    printTypeInfo<std::array<VkImage, 10>>("array<VkImage, 10>");
    printTypeInfo<std::array<VkBuffer, 5>>("array<VkBuffer, 5>");
    std::cout << "\n";

    std::cout << "Test 4: ResourceVariant\n";
    printTypeInfo<ResourceVariant>("ResourceVariant");
    printTypeInfo<std::vector<ResourceVariant>>("vector<ResourceVariant>");
    std::cout << "\n";

    std::cout << "Test 5: Custom variants\n";
    using TextureHandles = std::variant<VkImage, VkImageView, VkSampler>;
    printTypeInfo<TextureHandles>("variant<VkImage, VkImageView, VkSampler>");
    printTypeInfo<std::vector<TextureHandles>>("vector<TextureHandles>");
    std::cout << "\n";

    std::cout << "Test 6: Invalid types\n";
    printTypeInfo<UnknownType>("UnknownType");
    printTypeInfo<std::vector<UnknownType>>("vector<UnknownType>");
    std::cout << "\n";

    std::cout << "✅ All tests passed!\n";
    std::cout << "(If compilation succeeded, all static_assert checks passed)\n\n";

    return 0;
}
