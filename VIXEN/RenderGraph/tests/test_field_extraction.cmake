# ============================================================================
# Field Extraction Test Configuration
# ============================================================================
# This test validates the struct field extraction system for ergonomic
# slot connections. Compatible with VULKAN_TRIMMED_BUILD (headers only).

message(STATUS "Configuring test_field_extraction (trimmed build compatible)")

# Only configure if trimmed build or full SDK available
if(VULKAN_TRIMMED_BUILD_ACTIVE OR NOT VULKAN_TRIMMED_BUILD)
    # Create test executable
    add_executable(test_field_extraction
        test_field_extraction.cpp
    )

    # Include RenderGraph headers - allow clean paths: #include "RenderGraph/..."
    target_include_directories(test_field_extraction PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
        ${CMAKE_BINARY_DIR}/_deps/glm-src
        ${CMAKE_BINARY_DIR}/_deps/gli-src
    )

    # Add Vulkan headers
    if(VULKAN_TRIMMED_BUILD_ACTIVE)
        # Use fetched headers
        target_include_directories(test_field_extraction PRIVATE
            ${VULKAN_HEADERS_INCLUDE_DIR}
        )
        message(STATUS "  Using Vulkan headers from: ${VULKAN_HEADERS_INCLUDE_DIR}")
    else()
        # Use SDK headers
        target_include_directories(test_field_extraction PRIVATE
            ${VULKAN_PATH}/Include
        )
    endif()

    # Set C++23 (required for advanced template features)
    target_compile_features(test_field_extraction PRIVATE cxx_std_23)

    # Platform defines for Vulkan headers
    if(UNIX AND NOT APPLE)
        target_compile_definitions(test_field_extraction PRIVATE VK_USE_PLATFORM_XCB_KHR)
    elseif(WIN32)
        target_compile_definitions(test_field_extraction PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    elseif(APPLE)
        target_compile_definitions(test_field_extraction PRIVATE VK_USE_PLATFORM_MACOS_MVK)
    endif()

    # Mark as trimmed build compatible
    set_target_properties(test_field_extraction PROPERTIES
        VULKAN_TRIMMED_BUILD_COMPATIBLE TRUE
        FOLDER "Tests/RenderGraph"
    )

    message(STATUS "✓ test_field_extraction configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
else()
    message(STATUS "⊗ test_field_extraction skipped (requires Vulkan SDK or trimmed build)")
endif()
