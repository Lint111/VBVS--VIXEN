cmake_minimum_required(VERSION 3.16...4.2)
project(CashSystem)

# Recursively collect all source files
file(GLOB_RECURSE CASHSYSTEM_SOURCES
    "src/*.cpp"
)

add_library(CashSystem STATIC ${CASHSYSTEM_SOURCES})

# Set C++ standard for CashSystem
set_property(TARGET CashSystem PROPERTY CXX_STANDARD 23)
set_property(TARGET CashSystem PROPERTY CXX_STANDARD_REQUIRED ON)

# Visual Studio solution folder organization
set_target_properties(CashSystem PROPERTIES FOLDER "Libraries")

target_include_directories(CashSystem PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # Main application includes (so CashSystem can use common headers)
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/application/main/include>
    # Generated SDI headers - priority order:
    #   1. Dev-time: Tool-generated SDI files in source tree
    #   2. Build-time: CMake-generated SDI files
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/generated/sdi>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/application/main/generated/sdi>
)

# Link dependencies here if/when required
target_link_libraries(CashSystem PUBLIC
    Logger
    EventBus  # For device invalidation event subscription
    ShaderManagement  # For ShaderDataBundle and reflection data
    VulkanResources  # For Vulkan device and resource abstractions
)

# Link Vulkan (conditional on build mode)
if(VULKAN_TRIMMED_BUILD_ACTIVE)
    # Trimmed build: headers only
    if(TARGET Vulkan::Headers)
        target_link_libraries(CashSystem PUBLIC Vulkan::Headers)
    endif()
    target_include_directories(CashSystem PUBLIC ${VULKAN_HEADERS_INCLUDE_DIR})
else()
    # Full build: Vulkan runtime library
    target_link_libraries(CashSystem PUBLIC Vulkan::Vulkan)
endif()

# Link ProjectHash so CashSystem uses the same hash provider includes/links
target_link_libraries(CashSystem PRIVATE ProjectHash)

# ============================================================================
# Precompiled Headers
# ============================================================================

if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(CashSystem PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h"
    )
    message(STATUS "CashSystem: Precompiled headers enabled")
endif()

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
