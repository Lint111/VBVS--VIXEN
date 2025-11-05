# ===========================================================================
# Naming.h Pattern Test Configuration
# ===========================================================================
# Tests the FINAL pattern where naming.h is self-contained with ZERO
# manual declarations required.
#
# KEY PRINCIPLE:
# SDI generates naming.h with embedded _Reflection metadata.
# Include naming.h and it "just works" - no external trait declarations.
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only).
# ===========================================================================

message(STATUS "Configuring test_naming_h_pattern (zero configuration test)")

add_executable(test_naming_h_pattern
    test_naming_h_pattern.cpp
)

# Include RenderGraph headers
target_include_directories(test_naming_h_pattern PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../RenderGraph/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_BINARY_DIR}/_deps/glm-src
    ${CMAKE_BINARY_DIR}/_deps/gli-src
)

# Add Vulkan headers
if(VULKAN_TRIMMED_BUILD_ACTIVE)
    target_include_directories(test_naming_h_pattern PRIVATE
        ${VULKAN_HEADERS_INCLUDE_DIR}
    )
    message(STATUS "  Using Vulkan headers from: ${VULKAN_HEADERS_INCLUDE_DIR}")
else()
    if(DEFINED VULKAN_PATH)
        target_include_directories(test_naming_h_pattern PRIVATE
            ${VULKAN_PATH}/Include
        )
    endif()
endif()

# C++23 required for structured bindings
target_compile_features(test_naming_h_pattern PRIVATE cxx_std_23)

# Platform defines
if(UNIX AND NOT APPLE)
    target_compile_definitions(test_naming_h_pattern PRIVATE VK_USE_PLATFORM_XCB_KHR)
elseif(WIN32)
    target_compile_definitions(test_naming_h_pattern PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    target_compile_definitions(test_naming_h_pattern PRIVATE VK_USE_PLATFORM_MACOS_MVK)
endif()

# Compile options
if(MSVC)
    target_compile_options(test_naming_h_pattern PRIVATE /W4)
else()
    target_compile_options(test_naming_h_pattern PRIVATE -Wall -Wextra)
endif()

message(STATUS "✓ test_naming_h_pattern configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
