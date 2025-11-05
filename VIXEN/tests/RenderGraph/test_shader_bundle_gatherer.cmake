# ===========================================================================
# Shader Bundle Gatherer Test Configuration
# ===========================================================================
# Tests the final pattern for Phase G resource gathering:
# - Shader bundle headers as "config files"
# - Automatic input slot generation from bundle type
# - Single bundle type parameter with variadic inputs
# - Compile-time type validation
# - Minimal graph setup
#
# This test validates the core pattern requested by the user:
# "1 normal input slot for shaderbundle and variadic input validated
#  along said headerfile provided"
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only).
# ===========================================================================

message(STATUS "Configuring test_shader_bundle_gatherer (trimmed build compatible)")

# Create test executable
add_executable(test_shader_bundle_gatherer
    test_shader_bundle_gatherer.cpp
)

# Include RenderGraph headers
target_include_directories(test_shader_bundle_gatherer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../RenderGraph/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_BINARY_DIR}/_deps/glm-src
    ${CMAKE_BINARY_DIR}/_deps/gli-src
)

# Add Vulkan headers
if(VULKAN_TRIMMED_BUILD_ACTIVE)
    # Use fetched headers
    target_include_directories(test_shader_bundle_gatherer PRIVATE
        ${VULKAN_HEADERS_INCLUDE_DIR}
    )
    message(STATUS "  Using Vulkan headers from: ${VULKAN_HEADERS_INCLUDE_DIR}")
else()
    # Use SDK headers
    if(DEFINED VULKAN_PATH)
        target_include_directories(test_shader_bundle_gatherer PRIVATE
            ${VULKAN_PATH}/Include
        )
    endif()
endif()

# C++23 required for structured bindings and std::expected
target_compile_features(test_shader_bundle_gatherer PRIVATE cxx_std_23)

# Platform defines for Vulkan headers
if(UNIX AND NOT APPLE)
    target_compile_definitions(test_shader_bundle_gatherer PRIVATE VK_USE_PLATFORM_XCB_KHR)
elseif(WIN32)
    target_compile_definitions(test_shader_bundle_gatherer PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    target_compile_definitions(test_shader_bundle_gatherer PRIVATE VK_USE_PLATFORM_MACOS_MVK)
endif()

# Compile options
if(MSVC)
    target_compile_options(test_shader_bundle_gatherer PRIVATE /W4)
else()
    target_compile_options(test_shader_bundle_gatherer PRIVATE -Wall -Wextra)
endif()

message(STATUS "✓ test_shader_bundle_gatherer configured (trimmed build: ${VULKAN_TRIMMED_BUILD})")
