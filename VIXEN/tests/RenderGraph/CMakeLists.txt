cmake_minimum_required(VERSION 3.15)
project(RenderGraphTests)

add_executable(test_rendergraph_basic
    test_rendergraph_basic.cpp
    test_rendergraph_dependency.cpp
)
# Link against the RenderGraph library target if it exists in the main project
# Use the imported target GTest::gtest_main provided by the top-level FetchContent
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_rendergraph_basic)

add_executable(test_rendergraph_dependency
    test_rendergraph_dependency.cpp
    test_array_slot_dedup.cpp
    test_multiple_producers.cpp
)
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_rendergraph_dependency)

add_executable(test_typednode_helpers test_typednode_helpers.cpp)
if(TARGET RenderGraph)
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_typednode_helpers)

# Additional typed-node tests
target_sources(test_typednode_helpers PRIVATE test_executeonly_behavior.cpp)

# ===========================================================================
# Standalone array type validation test (no Vulkan runtime required)
# ===========================================================================
# This test validates compile-time type traits only - no Vulkan needed!
# If this compiles successfully, all static_assert tests passed.

include(test_array_validation.cmake)

# ===========================================================================
# Standalone field extraction test (no Vulkan runtime required)
# ===========================================================================
# This test validates struct field extraction for ergonomic slot connections.
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_field_extraction.cmake)

# ===========================================================================
# Resource gatherer node test (no Vulkan runtime required)
# ===========================================================================
# This test validates the variadic template resource gatherer node works
# with all type system features (arrays, variants, field extraction).
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_resource_gatherer.cmake)

# ===========================================================================
# Descriptor Resource Gatherer - Comprehensive Test Suite
# ===========================================================================
# This test suite validates DescriptorResourceGathererNode (Phase G):
# - Order-agnostic binding connections
# - SDI naming.h integration
# - Expected failures and edge cases
# - Full validation coverage
# Requires GoogleTest framework.

if(TARGET GTest::gtest_main)
    add_executable(test_descriptor_gatherer_comprehensive
        test_descriptor_gatherer_comprehensive.cpp
    )

    target_include_directories(test_descriptor_gatherer_comprehensive PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )

    target_link_libraries(test_descriptor_gatherer_comprehensive PRIVATE
        GTest::gtest_main
        RenderGraph
        ShaderManagement
    )

    if(TARGET Vulkan::Vulkan)
        target_link_libraries(test_descriptor_gatherer_comprehensive PRIVATE Vulkan::Vulkan)
    endif()

    gtest_discover_tests(test_descriptor_gatherer_comprehensive)

    message(STATUS "✓ test_descriptor_gatherer_comprehensive configured")
else()
    message(STATUS "⊗ test_descriptor_gatherer_comprehensive skipped (GoogleTest not available)")
endif()
