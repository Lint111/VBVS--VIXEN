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

// Test 4: Variant type itself
static_assert(ResourceTypeTraits<ResourceHandleVariant>::isValid,
              "ResourceHandleVariant itself should be valid");
static_assert(ResourceTypeTraits<std::vector<ResourceHandleVariant>>::isValid,
              "vector<ResourceHandleVariant> should be valid");

// Test 5: Unregistered types (should fail)
struct UnknownType {};
static_assert(!ResourceTypeTraits<UnknownType>::isValid,
              "UnknownType should be invalid");
static_assert(!ResourceTypeTraits<std::vector<UnknownType>>::isValid,
              "vector<UnknownType> should be invalid");

// Test 6: Container detection
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

// Test 7: Base type extraction
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

// Define test config with array slots
CONSTEXPR_NODE_CONFIG(TestArrayNodeConfig, 3, 2, SlotArrayMode::Single) {
    // Scalar slot
    CONSTEXPR_INPUT(IMAGE, VkImage, 0, false);

    // Vector slot (explicitly typed as vector)
    CONSTEXPR_INPUT(IMAGES, std::vector<VkImage>, 1, false);

    // Variant slot (accepts any registered type)
    CONSTEXPR_INPUT(ANY_HANDLE, ResourceHandleVariant, 2, false);

    // Output vector
    CONSTEXPR_OUTPUT(OUTPUT_BUFFERS, std::vector<VkBuffer>, 0, false);

    // Output variant
    CONSTEXPR_OUTPUT(OUTPUT_ANY, ResourceHandleVariant, 1, false);
};

// Slots should compile successfully with array types
static_assert(TestArrayNodeConfig::IMAGE_Slot::index == 0, "IMAGE at index 0");
static_assert(TestArrayNodeConfig::IMAGES_Slot::index == 1, "IMAGES at index 1");
static_assert(TestArrayNodeConfig::ANY_HANDLE_Slot::index == 2, "ANY_HANDLE at index 2");

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

    // Test 4: Variant types
    std::cout << "\nTest 4: Variant types" << std::endl;
    std::cout << "  ResourceHandleVariant valid: "
              << ResourceTypeTraits<ResourceHandleVariant>::isValid << std::endl;
    std::cout << "  vector<ResourceHandleVariant> valid: "
              << ResourceTypeTraits<std::vector<ResourceHandleVariant>>::isValid << std::endl;

    // Test 5: Container detection
    std::cout << "\nTest 5: Container detection" << std::endl;
    std::cout << "  vector<VkImage> is container: "
              << ResourceTypeTraits<std::vector<VkImage>>::isContainer << std::endl;
    std::cout << "  vector<VkImage> is vector: "
              << ResourceTypeTraits<std::vector<VkImage>>::isVector << std::endl;
    std::cout << "  array<VkImage, 10> array size: "
              << ResourceTypeTraits<std::array<VkImage, 10>>::arraySize << std::endl;

    // Test 6: Config validation
    std::cout << "\nTest 6: Config validation" << std::endl;
    std::cout << "  TestArrayNodeConfig INPUT_COUNT: "
              << TestArrayNodeConfig::INPUT_COUNT << std::endl;
    std::cout << "  TestArrayNodeConfig OUTPUT_COUNT: "
              << TestArrayNodeConfig::OUTPUT_COUNT << std::endl;

    std::cout << "\n✅ All tests passed!" << std::endl;
    return 0;
}
