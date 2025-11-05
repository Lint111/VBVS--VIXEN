# ===========================================================================
# Resource Management Tests
# ===========================================================================
# Tests for RenderGraph resource management systems:
# - ResourceBudgetManager (memory budget tracking)
# - DeferredDestruction (cleanup queue management)
# - StatefulContainer (resource state tracking)
# - SlotTask (task status management)
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only).
# ===========================================================================

message(STATUS "Configuring test_resource_management (trimmed build compatible)")

if(TARGET GTest::gtest_main)
    add_executable(test_resource_management
        test_resource_management.cpp
    )

    target_include_directories(test_resource_management PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )

    target_link_libraries(test_resource_management PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Platform-specific settings
    if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
        # Full build mode
        if(TARGET Vulkan::Vulkan)
            target_link_libraries(test_resource_management PRIVATE Vulkan::Vulkan)
        endif()
    else()
        # Trimmed build mode
        message(STATUS "  Using Vulkan headers from: ${VULKAN_HEADERS_INCLUDE_DIR}")
    endif()

    gtest_discover_tests(test_resource_management)

    message(STATUS "✓ test_resource_management configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
else()
    message(STATUS "⊗ test_resource_management skipped (GoogleTest not available)")
endif()
