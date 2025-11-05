# ============================================================================
# Resource Gatherer Node Test Configuration
# ============================================================================
# This test validates the variadic template resource gatherer node works
# with all type system features. Compatible with VULKAN_TRIMMED_BUILD.

message(STATUS "Configuring test_resource_gatherer (trimmed build compatible)")

# Only configure if trimmed build or full SDK available
if(VULKAN_TRIMMED_BUILD_ACTIVE OR NOT VULKAN_TRIMMED_BUILD)
    # Create test executable
    add_executable(test_resource_gatherer
        test_resource_gatherer.cpp
    )

    # Include RenderGraph headers
    target_include_directories(test_resource_gatherer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../RenderGraph/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_BINARY_DIR}/_deps/glm-src
        ${CMAKE_BINARY_DIR}/_deps/gli-src
    )

    # Add Vulkan headers
    if(VULKAN_TRIMMED_BUILD_ACTIVE)
        # Use fetched headers
        target_include_directories(test_resource_gatherer PRIVATE
            ${VULKAN_HEADERS_INCLUDE_DIR}
        )
        message(STATUS "  Using Vulkan headers from: ${VULKAN_HEADERS_INCLUDE_DIR}")
    else()
        # Use SDK headers
        target_include_directories(test_resource_gatherer PRIVATE
            ${VULKAN_PATH}/Include
        )
    endif()

    # Set C++23 (required for variadic template features)
    target_compile_features(test_resource_gatherer PRIVATE cxx_std_23)

    # Platform defines for Vulkan headers
    if(UNIX AND NOT APPLE)
        target_compile_definitions(test_resource_gatherer PRIVATE VK_USE_PLATFORM_XCB_KHR)
    elseif(WIN32)
        target_compile_definitions(test_resource_gatherer PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    elseif(APPLE)
        target_compile_definitions(test_resource_gatherer PRIVATE VK_USE_PLATFORM_MACOS_MVK)
    endif()

    # Mark as trimmed build compatible
    set_target_properties(test_resource_gatherer PROPERTIES
        VULKAN_TRIMMED_BUILD_COMPATIBLE TRUE
        FOLDER "Tests/RenderGraph"
    )

    message(STATUS "✓ test_resource_gatherer configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
else()
    message(STATUS "⊗ test_resource_gatherer skipped (requires Vulkan SDK or trimmed build)")
endif()
