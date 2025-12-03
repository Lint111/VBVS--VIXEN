// Test compile-time recursive validation with caching
#include "Headers.h"
#include "Data/Core/ResourceTypeValidation.h"
#include <iostream>
#include <chrono>

using namespace Vixen::RenderGraph;

// Template to force compile-time evaluation and measure instantiation
template<typename T>
struct ForceCompileTimeCheck {
    static constexpr bool result = ValidateType<T>::value;
    static void print() {
        std::cout << "Type: " << typeid(T).name() << "\n";
        std::cout << "  Valid: " << result << "\n";
        std::cout << "  Hash: 0x" << std::hex << CompileTimeTypeHash<T>::value << std::dec << "\n";
        std::cout << "  Method: " << ValidateType<T>::validation_method << "\n";
        std::cout << "  Is Container: " << RecursiveTypeValidator<T>::is_container << "\n";
        std::cout << "  Is Variant: " << RecursiveTypeValidator<T>::is_variant << "\n";
        std::cout << "  Validation Path: " << RecursiveTypeValidator<T>::validation_path << "\n\n";
    }
};

int main() {
    std::cout << "==============================================\n";
    std::cout << " COMPILE-TIME RECURSIVE VALIDATION WITH CACHE\n";
    std::cout << "==============================================\n\n";

    std::cout << "All validation happens at COMPILE TIME!\n";
    std::cout << "Template specializations act as compile-time cache.\n\n";

    // Test 1: Direct types (base cache)
    std::cout << "--- Direct Registered Types ---\n";
    ForceCompileTimeCheck<VkImage>::print();
    ForceCompileTimeCheck<VkSwapchainKHR>::print();
    ForceCompileTimeCheck<uint32_t>::print();

    // Test 2: Containers (recursive validation)
    std::cout << "--- Container Types (Recursive) ---\n";
    ForceCompileTimeCheck<std::vector<VkImage>>::print();
    ForceCompileTimeCheck<std::array<VkBuffer, 5>>::print();

    // Test 3: Nested containers (deep recursion)
    std::cout << "--- Nested Containers (Deep Recursion) ---\n";
    ForceCompileTimeCheck<std::vector<std::vector<VkImageView>>>::print();
    ForceCompileTimeCheck<std::array<std::vector<VkSampler>, 3>>::print();

    // Test 4: Variants (all types validated)
    std::cout << "--- Variant Types (All Members Validated) ---\n";
    ForceCompileTimeCheck<std::variant<VkImage, VkBuffer>>::print();
    ForceCompileTimeCheck<std::variant<uint32_t, float, bool>>::print();

    // Test 5: Complex nested types
    std::cout << "--- Complex Composite Types ---\n";
    using ComplexType1 = std::vector<std::variant<VkImage, VkBuffer>>;
    using ComplexType2 = std::variant<std::vector<VkImage>, std::array<VkBuffer, 10>>;

    ForceCompileTimeCheck<ComplexType1>::print();
    ForceCompileTimeCheck<ComplexType2>::print();

    // Test 6: Invalid types (should fail validation)
    std::cout << "--- Invalid Types (Should Fail) ---\n";
    struct UnregisteredType { int x; };
    ForceCompileTimeCheck<UnregisteredType>::print();
    ForceCompileTimeCheck<std::vector<UnregisteredType>>::print();

    // Compile-time assertions to prove validation happens at compile time
    static_assert(ValidateType<VkImage>::value, "VkImage should be valid");
    static_assert(ValidateType<std::vector<VkImage>>::value, "vector<VkImage> should be valid");
    static_assert(ValidateType<std::vector<std::vector<VkBuffer>>>::value, "Nested vectors should be valid");
    static_assert(ValidateType<std::variant<VkImage, VkBuffer, uint32_t>>::value, "Variant should be valid");

    // These should fail at compile time if uncommented:
    // static_assert(ValidateType<UnregisteredType>::value, "This should fail!");
    // static_assert(ValidateType<std::vector<UnregisteredType>>::value, "This should fail!");

    std::cout << "==============================================\n";
    std::cout << " PERFORMANCE CHARACTERISTICS\n";
    std::cout << "==============================================\n\n";

    std::cout << "1. ALL validation happens at COMPILE TIME\n";
    std::cout << "2. Zero runtime overhead - all checks are constexpr\n";
    std::cout << "3. Template instantiation acts as compile-time cache:\n";
    std::cout << "   - First use: Template instantiated, validation performed\n";
    std::cout << "   - Subsequent uses: Reuse existing instantiation\n";
    std::cout << "4. Complex types validated recursively:\n";
    std::cout << "   - vector<T> validates T\n";
    std::cout << "   - variant<T1,T2,T3> validates T1, T2, and T3\n";
    std::cout << "   - Arbitrary nesting supported\n\n";

    std::cout << "In ResourceV3:\n";
    std::cout << "- IsRegisteredType<T> = Direct check (O(1) compile time)\n";
    std::cout << "- ResourceTypeTraits<T> = Recursive validation with unwrapping\n";
    std::cout << "- Template specializations = Compile-time memoization\n\n";

    return 0;
}