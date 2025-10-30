cmake_minimum_required(VERSION 3.11..4.0 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch magic_enum for enum-to-string conversions
include(FetchContent)
FetchContent_Declare(
    magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum
    GIT_TAG v0.9.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(magic_enum)

# Source files for RenderGraph static library
file(GLOB_RECURSE RENDERGRAPH_SRC
    "include/*.h"
    "src/**/*.cpp"
)

# Exclude legacy Resource.cpp (replaced by variant system)
list(FILTER RENDERGRAPH_SRC EXCLUDE REGEX "src/Core/Resource\\.cpp$")

add_library(RenderGraph STATIC ${RENDERGRAPH_SRC})

# MSVC: Enable /FS for parallel compilation PDB access
if(MSVC)
    target_compile_options(RenderGraph PRIVATE /FS)
endif()

# Include directories
target_include_directories(RenderGraph PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../CashSystem/include
)

# Link dependencies
target_link_libraries(RenderGraph PUBLIC
    VulkanResources
    Logger
    EventBus
    ResourceManagement
    CashSystem
    # ShaderManagement  # MVP: Temporarily disabled
    magic_enum::magic_enum
)

# Optionally set output directory
set_target_properties(RenderGraph PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
