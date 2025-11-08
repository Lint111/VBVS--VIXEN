cmake_minimum_required(VERSION 3.15)

# ============================================================================
# STANDALONE TEST: Array Type Validation (TRIMMED BUILD COMPATIBLE)
# ============================================================================
# This test only validates compile-time type traits - NO Vulkan runtime needed!
# Compatible with VULKAN_TRIMMED_BUILD (headers only)

# Skip if trimmed build is not active and we don't have full SDK
if(VULKAN_TRIMMED_BUILD_ACTIVE OR NOT VULKAN_TRIMMED_BUILD)
    message(STATUS "Configuring test_array_type_validation (trimmed build compatible)")

    add_executable(test_array_type_validation
        test_array_type_validation.cpp
    )

    # Include RenderGraph headers (for type traits only)
    target_include_directories(test_array_type_validation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../RenderGraph/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_BINARY_DIR}/_deps/glm-src
        ${CMAKE_BINARY_DIR}/_deps/gli-src
    )

    # Add Vulkan headers
    if(VULKAN_TRIMMED_BUILD_ACTIVE)
        # Use fetched headers
        target_include_directories(test_array_type_validation PRIVATE
            ${VULKAN_HEADERS_INCLUDE_DIR}
        )
        if(TARGET Vulkan::Headers)
            target_link_libraries(test_array_type_validation PRIVATE Vulkan::Headers)
        endif()
    else()
        # Use SDK headers
        target_include_directories(test_array_type_validation PRIVATE
            ${VULKAN_PATH}/Include
        )
    endif()

    # Set C++23 (required for type traits)
    target_compile_features(test_array_type_validation PRIVATE cxx_std_23)

    # Platform defines for Vulkan headers
    if(UNIX AND NOT APPLE)
        target_compile_definitions(test_array_type_validation PRIVATE VK_USE_PLATFORM_XCB_KHR)
    elseif(WIN32)
        target_compile_definitions(test_array_type_validation PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    elseif(APPLE)
        target_compile_definitions(test_array_type_validation PRIVATE VK_USE_PLATFORM_MACOS_MVK)
    endif()

    # Mark as trimmed build compatible
    set_target_properties(test_array_type_validation PROPERTIES
        VULKAN_TRIMMED_BUILD_COMPATIBLE TRUE
    )

    # Add to CTest
    if(BUILD_TESTING)
        add_test(NAME ArrayTypeValidation COMMAND test_array_type_validation)
        set_tests_properties(ArrayTypeValidation PROPERTIES
            PASS_REGULAR_EXPRESSION "All tests passed"
        )
    endif()

    # MSVC: Enable /FS for parallel compilation
    if(MSVC)
        target_compile_options(test_array_type_validation PRIVATE /FS)
    endif()

    message(STATUS "✓ test_array_type_validation configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
else()
    message(STATUS "✗ Skipping test_array_type_validation (requires trimmed build or full SDK)")
endif()
