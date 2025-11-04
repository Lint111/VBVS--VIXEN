/**
 * @file test_array_type_validation.cpp
 * @brief Test array-aware ResourceTypeTraits validation
 *
 * Validates that registering type T automatically enables:
 * - T (scalar)
 * - vector<T> (dynamic array)
 * - array<T, N> (static array)
 * - ResourceHandleVariant (the variant itself)
 * - vector<ResourceHandleVariant>
 */

#include "Core/ResourceVariant.h"
#include "Core/ResourceConfig.h"
#include <iostream>
#include <vector>
#include <array>

using namespace Vixen::RenderGraph;

// ============================================================================
// COMPILE-TIME VALIDATION TESTS
// ============================================================================

// Test 1: Scalar types
static_assert(ResourceTypeTraits<VkImage>::isValid, "VkImage should be valid");
static_assert(ResourceTypeTraits<VkBuffer>::isValid, "VkBuffer should be valid");
static_assert(ResourceTypeTraits<VkSampler>::isValid, "VkSampler should be valid");

// Test 2: Vector types (auto-enabled from scalar registration)
static_assert(ResourceTypeTraits<std::vector<VkImage>>::isValid,
              "vector<VkImage> should be valid when VkImage is registered");
static_assert(ResourceTypeTraits<std::vector<VkBuffer>>::isValid,
              "vector<VkBuffer> should be valid when VkBuffer is registered");
static_assert(ResourceTypeTraits<std::vector<VkSampler>>::isValid,
              "vector<VkSampler> should be valid when VkSampler is registered");

// Test 3: Array types (auto-enabled from scalar registration)
static_assert(ResourceTypeTraits<std::array<VkImage, 10>>::isValid,
              "array<VkImage, 10> should be valid when VkImage is registered");
static_assert(ResourceTypeTraits<std::array<VkBuffer, 5>>::isValid,
              "array<VkBuffer, 5> should be valid when VkBuffer is registered");

// Test 4: ResourceHandleVariant (macro-generated variant type)
static_assert(ResourceTypeTraits<ResourceHandleVariant>::isValid,
              "ResourceHandleVariant itself should be valid");
static_assert(ResourceTypeTraits<ResourceHandleVariant>::isVariantType,
              "ResourceHandleVariant should be detected as variant type");
static_assert(!ResourceTypeTraits<ResourceHandleVariant>::isContainer,
              "ResourceHandleVariant scalar is not a container");

// Test 5: Containers of ResourceHandleVariant
static_assert(ResourceTypeTraits<std::vector<ResourceHandleVariant>>::isValid,
              "vector<ResourceHandleVariant> should be valid");
static_assert(ResourceTypeTraits<std::vector<ResourceHandleVariant>>::isVariantContainer,
              "vector<ResourceHandleVariant> should be detected as variant container");
static_assert(ResourceTypeTraits<std::vector<ResourceHandleVariant>>::isAnyVariant,
              "vector<ResourceHandleVariant> is any form of variant");
static_assert(ResourceTypeTraits<std::array<ResourceHandleVariant, 5>>::isValid,
              "array<ResourceHandleVariant, 5> should be valid");
static_assert(ResourceTypeTraits<std::array<ResourceHandleVariant, 5>>::isVariantContainer,
              "array<ResourceHandleVariant, 5> should be detected as variant container");

// Test 6: Custom variants (type-safe subsets)
using TextureHandles = std::variant<VkImage, VkImageView, VkSampler>;
using BufferHandles = std::variant<VkBuffer, VkCommandBuffer>;

static_assert(ResourceTypeTraits<TextureHandles>::isValid,
              "Custom variant with all registered types should be valid");
static_assert(ResourceTypeTraits<TextureHandles>::isCustomVariant,
              "TextureHandles should be detected as custom variant");
static_assert(ResourceTypeTraits<std::vector<TextureHandles>>::isValid,
              "vector<TextureHandles> should be valid");
static_assert(ResourceTypeTraits<std::vector<TextureHandles>>::isCustomVariantContainer,
              "vector<TextureHandles> should be detected as custom variant container");
static_assert(ResourceTypeTraits<std::array<BufferHandles, 3>>::isValid,
              "array<BufferHandles, 3> should be valid");

// Test 7: Invalid custom variants (contains unregistered types)
struct UnknownType {};
using InvalidVariant = std::variant<VkImage, UnknownType>;

static_assert(!ResourceTypeTraits<InvalidVariant>::isValid,
              "Custom variant with unregistered type should be invalid");
static_assert(!ResourceTypeTraits<std::vector<InvalidVariant>>::isValid,
              "vector of invalid custom variant should be invalid");

// Test 8: Unregistered types (should fail)
static_assert(!ResourceTypeTraits<UnknownType>::isValid,
              "UnknownType should be invalid");
static_assert(!ResourceTypeTraits<std::vector<UnknownType>>::isValid,
              "vector<UnknownType> should be invalid");

// Test 7: Container detection
static_assert(!ResourceTypeTraits<VkImage>::isContainer,
              "VkImage is not a container");
static_assert(ResourceTypeTraits<std::vector<VkImage>>::isContainer,
              "vector<VkImage> is a container");
static_assert(ResourceTypeTraits<std::vector<VkImage>>::isVector,
              "vector<VkImage> is a vector");
static_assert(!ResourceTypeTraits<std::vector<VkImage>>::isArray,
              "vector<VkImage> is not a static array");

static_assert(ResourceTypeTraits<std::array<VkImage, 10>>::isContainer,
              "array<VkImage, 10> is a container");
static_assert(!ResourceTypeTraits<std::array<VkImage, 10>>::isVector,
              "array<VkImage, 10> is not a vector");
static_assert(ResourceTypeTraits<std::array<VkImage, 10>>::isArray,
              "array<VkImage, 10> is a static array");
static_assert(ResourceTypeTraits<std::array<VkImage, 10>>::arraySize == 10,
              "array<VkImage, 10> has size 10");

// Test 10: Base type extraction
static_assert(std::is_same_v<
                  ResourceTypeTraits<std::vector<VkImage>>::BaseType,
                  VkImage>,
              "BaseType of vector<VkImage> should be VkImage");
static_assert(std::is_same_v<
                  ResourceTypeTraits<std::array<VkBuffer, 5>>::BaseType,
                  VkBuffer>,
              "BaseType of array<VkBuffer, 5> should be VkBuffer");

// ============================================================================
// SLOT VALIDATION TESTS
// ============================================================================

// Define test config with array, variant, and custom variant slots
CONSTEXPR_NODE_CONFIG(TestArrayNodeConfig, 5, 4, SlotArrayMode::Single) {
    // Scalar slot
    CONSTEXPR_INPUT(IMAGE, VkImage, 0, false);

    // Vector slot (explicitly typed as vector)
    CONSTEXPR_INPUT(IMAGES, std::vector<VkImage>, 1, false);

    // Variant slot (accepts any registered type)
    CONSTEXPR_INPUT(ANY_HANDLE, ResourceHandleVariant, 2, false);

    // Variant array slot (accepts array of any type)
    CONSTEXPR_INPUT(ANY_HANDLES, std::vector<ResourceHandleVariant>, 3, false);

    // Custom variant slot (type-safe subset)
    CONSTEXPR_INPUT(TEXTURE_HANDLES, TextureHandles, 4, false);

    // Output vector
    CONSTEXPR_OUTPUT(OUTPUT_BUFFERS, std::vector<VkBuffer>, 0, false);

    // Output variant
    CONSTEXPR_OUTPUT(OUTPUT_ANY, ResourceHandleVariant, 1, false);

    // Output variant array
    CONSTEXPR_OUTPUT(OUTPUT_ANY_ARRAY, std::vector<ResourceHandleVariant>, 2, false);

    // Output custom variant
    CONSTEXPR_OUTPUT(OUTPUT_TEXTURES, TextureHandles, 3, false);
};

// Slots should compile successfully with array, variant, and custom variant types
static_assert(TestArrayNodeConfig::IMAGE_Slot::index == 0, "IMAGE at index 0");
static_assert(TestArrayNodeConfig::IMAGES_Slot::index == 1, "IMAGES at index 1");
static_assert(TestArrayNodeConfig::ANY_HANDLE_Slot::index == 2, "ANY_HANDLE at index 2");
static_assert(TestArrayNodeConfig::ANY_HANDLES_Slot::index == 3, "ANY_HANDLES at index 3");
static_assert(TestArrayNodeConfig::TEXTURE_HANDLES_Slot::index == 4, "TEXTURE_HANDLES at index 4");

// Verify variant type detection in slots
static_assert(ResourceTypeTraits<TestArrayNodeConfig::ANY_HANDLE_Slot::Type>::isVariantType,
              "ANY_HANDLE slot type should be variant");
static_assert(ResourceTypeTraits<TestArrayNodeConfig::ANY_HANDLES_Slot::Type>::isVariantContainer,
              "ANY_HANDLES slot type should be variant container");
static_assert(ResourceTypeTraits<TestArrayNodeConfig::TEXTURE_HANDLES_Slot::Type>::isCustomVariant,
              "TEXTURE_HANDLES slot type should be custom variant");

// ============================================================================
// RUNTIME TEST
// ============================================================================

int main() {
    std::cout << "=== Array Type Validation Tests ===" << std::endl;

    // Test 1: Scalar types
    std::cout << "Test 1: Scalar types" << std::endl;
    std::cout << "  VkImage valid: " << ResourceTypeTraits<VkImage>::isValid << std::endl;
    std::cout << "  VkBuffer valid: " << ResourceTypeTraits<VkBuffer>::isValid << std::endl;

    // Test 2: Vector types
    std::cout << "\nTest 2: Vector types" << std::endl;
    std::cout << "  vector<VkImage> valid: "
              << ResourceTypeTraits<std::vector<VkImage>>::isValid << std::endl;
    std::cout << "  vector<VkBuffer> valid: "
              << ResourceTypeTraits<std::vector<VkBuffer>>::isValid << std::endl;

    // Test 3: Array types
    std::cout << "\nTest 3: Array types" << std::endl;
    std::cout << "  array<VkImage, 10> valid: "
              << ResourceTypeTraits<std::array<VkImage, 10>>::isValid << std::endl;

    // Test 4: ResourceHandleVariant (macro-generated variant)
    std::cout << "\nTest 4: ResourceHandleVariant (macro-generated variant)" << std::endl;
    std::cout << "  ResourceHandleVariant valid: "
              << ResourceTypeTraits<ResourceHandleVariant>::isValid << std::endl;
    std::cout << "  ResourceHandleVariant is variant type: "
              << ResourceTypeTraits<ResourceHandleVariant>::isVariantType << std::endl;
    std::cout << "  ResourceHandleVariant is any variant: "
              << ResourceTypeTraits<ResourceHandleVariant>::isAnyVariant << std::endl;

    // Test 5: Containers of ResourceHandleVariant
    std::cout << "\nTest 5: Containers of ResourceHandleVariant" << std::endl;
    std::cout << "  vector<ResourceHandleVariant> valid: "
              << ResourceTypeTraits<std::vector<ResourceHandleVariant>>::isValid << std::endl;
    std::cout << "  vector<ResourceHandleVariant> is variant container: "
              << ResourceTypeTraits<std::vector<ResourceHandleVariant>>::isVariantContainer << std::endl;
    std::cout << "  array<ResourceHandleVariant, 5> valid: "
              << ResourceTypeTraits<std::array<ResourceHandleVariant, 5>>::isValid << std::endl;
    std::cout << "  array<ResourceHandleVariant, 5> is variant container: "
              << ResourceTypeTraits<std::array<ResourceHandleVariant, 5>>::isVariantContainer << std::endl;

    // Test 6: Custom variants (type-safe subsets)
    std::cout << "\nTest 6: Custom variants (type-safe subsets)" << std::endl;
    std::cout << "  TextureHandles valid: "
              << ResourceTypeTraits<TextureHandles>::isValid << std::endl;
    std::cout << "  TextureHandles is custom variant: "
              << ResourceTypeTraits<TextureHandles>::isCustomVariant << std::endl;
    std::cout << "  vector<TextureHandles> valid: "
              << ResourceTypeTraits<std::vector<TextureHandles>>::isValid << std::endl;
    std::cout << "  vector<TextureHandles> is custom variant container: "
              << ResourceTypeTraits<std::vector<TextureHandles>>::isCustomVariantContainer << std::endl;

    // Test 7: Invalid custom variants
    std::cout << "\nTest 7: Invalid custom variants" << std::endl;
    std::cout << "  InvalidVariant (contains UnknownType) valid: "
              << ResourceTypeTraits<InvalidVariant>::isValid << std::endl;
    std::cout << "  Expected: 0 (false)" << std::endl;

    // Test 9: Container detection
    std::cout << "\nTest 9: Container detection" << std::endl;
    std::cout << "  vector<VkImage> is container: "
              << ResourceTypeTraits<std::vector<VkImage>>::isContainer << std::endl;
    std::cout << "  vector<VkImage> is vector: "
              << ResourceTypeTraits<std::vector<VkImage>>::isVector << std::endl;
    std::cout << "  array<VkImage, 10> array size: "
              << ResourceTypeTraits<std::array<VkImage, 10>>::arraySize << std::endl;

    // Test 10: Config validation with all slot types
    std::cout << "\nTest 10: Config validation with all slot types" << std::endl;
    std::cout << "  TestArrayNodeConfig INPUT_COUNT: "
              << TestArrayNodeConfig::INPUT_COUNT << std::endl;
    std::cout << "  TestArrayNodeConfig OUTPUT_COUNT: "
              << TestArrayNodeConfig::OUTPUT_COUNT << std::endl;

    std::cout << "\n✅ All tests passed!" << std::endl;
    return 0;
}
