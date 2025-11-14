// Test to demonstrate explicit compile-time caching
#include "Headers.h"
#include "RenderGraph/include/Data/Core/ResourceV3.h"
#include "RenderGraph/include/Data/Core/ResourceTypeCache.h"
#include <iostream>
#include <vector>
#include <array>

using namespace Vixen::RenderGraph;

// Helper to count template instantiation depth (for demonstration)
template<typename T, int Depth = 0>
struct InstantiationDepth {
    static constexpr int value = Depth;
};

// Measure how many template instantiations happen (simplified demonstration)
template<typename T>
struct MeasureInstantiations {
    // Without cache: ResourceTypeTraits<vector<T>> instantiates:
    // 1. ResourceTypeTraits<vector<T>>
    // 2. StripContainer<vector<T>>
    // 3. IsRegisteredType<T>
    // = 3 instantiations

    // With cache: ResourceTypeTraits<vector<T>> uses explicit specialization:
    // 1. Pre-existing specialization (0 new instantiations at use site)
    // = 0 instantiations at use site

    static constexpr int without_cache = 3;  // Recursive path
    static constexpr int with_cache = 0;     // Direct specialization
    static constexpr int savings = without_cache - with_cache;
};

int main() {
    std::cout << "==============================================\n";
    std::cout << " COMPILE-TIME CACHE PERFORMANCE DEMONSTRATION\n";
    std::cout << "==============================================\n\n";

    std::cout << "How C++ Template Instantiation Works:\n";
    std::cout << "-------------------------------------\n";
    std::cout << "1. First use of a template instantiates it\n";
    std::cout << "2. Subsequent uses reuse the instantiation\n";
    std::cout << "3. Explicit specializations avoid recursive instantiation\n\n";

    // Test 1: Uncached type (uses recursive validation)
    std::cout << "--- Uncached Type (Recursive Validation) ---\n";
    using UncachedType = std::vector<VkSampler>;
    std::cout << "Type: std::vector<VkSampler>\n";
    std::cout << "Valid: " << ResourceTypeTraits<UncachedType>::isValid << "\n";
    std::cout << "Method: Recursive (instantiates generic template)\n";
    std::cout << "  - Instantiates ResourceTypeTraits<vector<VkSampler>>\n";
    std::cout << "  - Instantiates StripContainer<vector<VkSampler>>\n";
    std::cout << "  - Checks IsRegisteredType<VkSampler>\n";
    std::cout << "Estimated instantiations: ~3\n\n";

    // Test 2: Cached type (uses explicit specialization)
    std::cout << "--- Cached Type (Explicit Specialization) ---\n";
    using CachedType = std::vector<VkImage>;
    std::cout << "Type: std::vector<VkImage>\n";
    std::cout << "Valid: " << ResourceTypeTraits<CachedType>::isValid << "\n";
    std::cout << "Method: Pre-cached (uses explicit specialization)\n";
    std::cout << "  - Uses pre-existing specialization from ResourceTypeCache.h\n";
    std::cout << "  - NO recursive template instantiation\n";
    std::cout << "  - Result is pre-computed\n";
    std::cout << "Estimated instantiations: 0 (already specialized)\n\n";

    // Test 3: Multiple uses of same type (compiler memoization)
    std::cout << "--- Multiple Uses of Same Type ---\n";
    std::cout << "First use:  ResourceTypeTraits<std::vector<VkBuffer>>\n";
    std::cout << "  - Compiler instantiates template (from cache or recursive)\n";
    bool first_use = ResourceTypeTraits<std::vector<VkBuffer>>::isValid;

    std::cout << "Second use: ResourceTypeTraits<std::vector<VkBuffer>>\n";
    std::cout << "  - Compiler REUSES existing instantiation\n";
    std::cout << "  - Zero additional work\n";
    bool second_use = ResourceTypeTraits<std::vector<VkBuffer>>::isValid;

    std::cout << "Third use:  ResourceTypeTraits<std::vector<VkBuffer>>\n";
    std::cout << "  - Compiler REUSES existing instantiation\n";
    std::cout << "  - Zero additional work\n";
    bool third_use = ResourceTypeTraits<std::vector<VkBuffer>>::isValid;

    std::cout << "All uses return same result: " << (first_use && second_use && third_use) << "\n\n";

    // Test 4: Nested types (deep recursion vs cached)
    std::cout << "--- Nested Types (Deep Recursion) ---\n";
    using NestedUncached = std::vector<std::vector<VkDescriptorSet>>;
    using NestedCached = std::vector<std::vector<VkImage>>;

    std::cout << "Uncached: std::vector<std::vector<VkDescriptorSet>>\n";
    std::cout << "  - Instantiates ResourceTypeTraits<vector<vector<VkDescriptorSet>>>\n";
    std::cout << "  - Recursively instantiates ResourceTypeTraits<vector<VkDescriptorSet>>\n";
    std::cout << "  - Recursively instantiates ResourceTypeTraits<VkDescriptorSet>\n";
    std::cout << "  - Plus StripContainer for each level\n";
    std::cout << "Valid: " << ResourceTypeTraits<NestedUncached>::isValid << "\n";
    std::cout << "Estimated instantiations: ~6-8\n\n";

    std::cout << "Cached: std::vector<std::vector<VkImage>>\n";
    std::cout << "  - Uses pre-existing specialization from ResourceTypeCache.h\n";
    std::cout << "  - NO recursive instantiation needed\n";
    std::cout << "Valid: " << ResourceTypeTraits<NestedCached>::isValid << "\n";
    std::cout << "Estimated instantiations: 0 (pre-specialized)\n\n";

    // Compile-time verification
    static_assert(ResourceTypeTraits<std::vector<VkImage>>::isValid,
                  "Cached type should be valid");
    static_assert(ResourceTypeTraits<std::vector<VkImageView>>::isValid,
                  "Cached type should be valid");
    static_assert(ResourceTypeTraits<std::vector<VkBuffer>>::isValid,
                  "Cached type should be valid");
    static_assert(ResourceTypeTraits<std::vector<std::vector<VkImage>>>::isValid,
                  "Nested cached type should be valid");

    std::cout << "==============================================\n";
    std::cout << " CACHE STRATEGY RECOMMENDATIONS\n";
    std::cout << "==============================================\n\n";

    std::cout << "When to pre-cache a type:\n";
    std::cout << "1. Type is used in >5 different files\n";
    std::cout << "2. Type is deeply nested (vector<vector<T>>)\n";
    std::cout << "3. Type is used in hot compilation paths (headers)\n";
    std::cout << "4. Type causes slow template instantiation\n\n";

    std::cout << "How to add to cache:\n";
    std::cout << "1. Add explicit specialization to ResourceTypeCache.h\n";
    std::cout << "2. Or use CACHE_COMPLEX_TYPE macro\n";
    std::cout << "3. Verify with static_assert\n\n";

    std::cout << "Example:\n";
    std::cout << "  // In ResourceTypeCache.h:\n";
    std::cout << "  template<>\n";
    std::cout << "  struct ResourceTypeTraits<std::vector<VkDescriptorSet>> {\n";
    std::cout << "      using BaseType = VkDescriptorSet;\n";
    std::cout << "      static constexpr bool isValid = true;  // Pre-validated!\n";
    std::cout << "      // ... other traits ...\n";
    std::cout << "  };\n\n";

    std::cout << "Benefits:\n";
    std::cout << "- Faster compilation (avoid recursive instantiation)\n";
    std::cout << "- Better error messages (specialization is direct)\n";
    std::cout << "- Documentation (shows commonly used types)\n";
    std::cout << "- Zero runtime cost (all compile-time)\n\n";

    std::cout << "==============================================\n";
    std::cout << " AUTOMATIC CACHE GENERATION (Future)\n";
    std::cout << "==============================================\n\n";

    std::cout << "Ideal build process:\n";
    std::cout << "1. Build step analyzes which types are frequently validated\n";
    std::cout << "2. Generates ResourceTypeCache.h with specializations\n";
    std::cout << "3. Subsequent builds use pre-cached types\n";
    std::cout << "4. Compilation time reduced by 10-30% (for heavy template use)\n\n";

    std::cout << "Tools that could help:\n";
    std::cout << "- ClangBuildAnalyzer (analyze template instantiation costs)\n";
    std::cout << "- CMake script to track most-used types\n";
    std::cout << "- Custom MSVC /d1reportTime analysis\n\n";

    return 0;
}