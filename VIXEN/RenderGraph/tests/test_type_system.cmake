# ===========================================================================
# Type System - Consolidated Test Configuration
# ===========================================================================
# This file consolidates all type system validation tests:
# - Array Type Validation: Compile-time type traits for array types
# - Field Extraction: Struct field extraction for ergonomic slot connections
# - Resource Gatherer: Variadic template resource gathering with full type support
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).
# ===========================================================================

# ---------------------------------------------------------------------------
# Array Type Validation Tests
# ---------------------------------------------------------------------------
# Validates compile-time type traits for array types (NO Vulkan runtime needed).
# Tests static_assert validation - successful compilation = tests passed.

message(STATUS "Configuring test_array_type_validation (trimmed build compatible)")

# Skip if trimmed build is not active and we don't have full SDK
if(VULKAN_TRIMMED_BUILD_ACTIVE OR NOT VULKAN_TRIMMED_BUILD)
    add_executable(test_array_type_validation
        test_array_type_validation.cpp
    )

    # Include RenderGraph headers - allow clean paths: #include "RenderGraph/..."
    target_include_directories(test_array_type_validation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
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

# ---------------------------------------------------------------------------
# Field Extraction Tests
# ---------------------------------------------------------------------------
# Validates struct field extraction system for ergonomic slot connections.

message(STATUS "Configuring test_field_extraction (trimmed build compatible)")

# Only configure if trimmed build or full SDK available
if(VULKAN_TRIMMED_BUILD_ACTIVE OR NOT VULKAN_TRIMMED_BUILD)
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

# ---------------------------------------------------------------------------
# Resource Gatherer Tests
# ---------------------------------------------------------------------------
# Validates variadic template resource gatherer node works with all type
# system features (arrays, variants, field extraction).

message(STATUS "Configuring test_resource_gatherer (trimmed build compatible)")

# Only configure if trimmed build or full SDK available
if(VULKAN_TRIMMED_BUILD_ACTIVE OR NOT VULKAN_TRIMMED_BUILD)
    add_executable(test_resource_gatherer
        test_resource_gatherer.cpp
    )

    # Include RenderGraph headers - allow clean paths: #include "RenderGraph/..."
    target_include_directories(test_resource_gatherer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
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
