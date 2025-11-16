# ShaderManagement static library
# Pure shader compilation and management logic - no RenderGraph dependencies

cmake_minimum_required(VERSION 3.15...4.10 FATAL_ERROR)

project(ShaderManagement)

# Fetch SPIRV-Reflect for shader reflection
include(FetchContent)
FetchContent_Declare(
    spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG        vulkan-sdk-1.3.290.0  # Stable release tag
    GIT_SHALLOW    TRUE
)
# Don't build executables, we just need the header/source
set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spirv_reflect)

# Collect source files
file(GLOB SHADER_MGMT_HEADERS "include/ShaderManagement/*.h")
file(GLOB SHADER_MGMT_SOURCES "src/*.cpp")

# Add SPIRV-Reflect source directly (header-only library pattern)
list(APPEND SHADER_MGMT_SOURCES 
    ${spirv_reflect_SOURCE_DIR}/spirv_reflect.c
)

# Create static library
add_library(ShaderManagement STATIC
    ${SHADER_MGMT_HEADERS}
    ${SHADER_MGMT_SOURCES}
)

# Include directories
target_include_directories(ShaderManagement
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${spirv_reflect_SOURCE_DIR}
)

# Note: EventBus includes come from target_link_libraries(ShaderManagement PUBLIC EventBus) below

# Add Vulkan include directories (conditional on build mode)
if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
    target_include_directories(ShaderManagement PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

# Link to project-level hash provider for consistent hashing behavior
target_link_libraries(ShaderManagement PRIVATE ProjectHash)

# Link to EventBus for shader events
target_link_libraries(ShaderManagement PUBLIC EventBus)

# Link to Logger for ILoggable support
target_link_libraries(ShaderManagement PUBLIC Logger)

# Find dependencies
# Find Vulkan (optional in trimmed build mode)
if(VULKAN_TRIMMED_BUILD_ACTIVE)
    message(STATUS "ShaderManagement: Using trimmed build mode (headers only)")
else()
    find_package(Vulkan REQUIRED)
endif()

# Build glslang library list for ShaderCompiler (skip in trimmed mode)
if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
    set(GLSLANG_LIBS SPIRV glslang OSDependent glslang-default-resource-limits SPIRV-Tools SPIRV-Tools-opt)

    # Generate the list of files to link, per flavor (debug/optimized)
    set(GLSLANG_LIB_LIST "")
    foreach(x ${GLSLANG_LIBS})
        list(APPEND GLSLANG_LIB_LIST debug ${x}d optimized ${x})
    endforeach()

    message(STATUS "ShaderManagement: Using glslang libraries from Vulkan SDK")
else()
    message(STATUS "ShaderManagement: Skipping glslang (trimmed build mode)")
endif()

# Optional OpenSSL for SHA256 hashing
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found - enabling SHA256 shader hashing for ShaderManagement")
    target_link_libraries(ShaderManagement PRIVATE OpenSSL::Crypto)
    target_compile_definitions(ShaderManagement PRIVATE HAS_OPENSSL)
    target_include_directories(ShaderManagement PRIVATE ${OPENSSL_INCLUDE_DIR})
else()
    message(STATUS "OpenSSL not found - ShaderManagement will use the project hash fallback or header-only provider if available")
endif()

# Link Vulkan and glslang (conditional)
if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
    target_link_libraries(ShaderManagement
        PUBLIC
            Vulkan::Headers
        PRIVATE
            ${GLSLANG_LIB_LIST}
    )
else()
    # Trimmed build: headers only
    if(TARGET Vulkan::Headers)
        target_link_libraries(ShaderManagement PUBLIC Vulkan::Headers)
    endif()
    target_include_directories(ShaderManagement PUBLIC ${VULKAN_HEADERS_INCLUDE_DIR})
endif()

# C++23 standard
target_compile_features(ShaderManagement PUBLIC cxx_std_23)

# Ensure spirv_reflect.c is compiled as C code without PCH
set_source_files_properties(
    ${spirv_reflect_SOURCE_DIR}/spirv_reflect.c
    PROPERTIES 
        LANGUAGE C
        SKIP_PRECOMPILE_HEADERS ON
)

# Precompiled headers for faster compilation (C++ only)
if(USE_PRECOMPILED_HEADERS)
    # Use the central Headers.h as PCH for consistency across project
    target_precompile_headers(ShaderManagement PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/Headers.h"
    )
    message(STATUS "ShaderManagement: Precompiled headers enabled (using Headers.h)")
endif()

# Unity build support (optional)
if(USE_UNITY_BUILD)
    set_target_properties(ShaderManagement PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 12
    )
    message(STATUS "ShaderManagement: Unity build enabled")
endif()

# Platform definitions
if(WIN32)
    target_compile_definitions(ShaderManagement PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

# Organize in IDE
set_target_properties(ShaderManagement PROPERTIES FOLDER "Libraries")

# ===== Build Tools =====

# Skip tools in trimmed build mode (require runtime compilation)
if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
    # Fetch nlohmann_json for shader_tool
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)

    # Add tools subdirectory (shader_tool executable)
    add_subdirectory(tools)
    message(STATUS "ShaderManagement: Building shader tools")
else()
    message(STATUS "ShaderManagement: Skipping shader tools (trimmed build mode)")
endif()

# Add tests subdirectory (if testing is enabled)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Install CMake utility modules for downstream projects
install(FILES
    cmake/ShaderToolUtils.cmake
    cmake/ParseShaderIncludes.cmake
    DESTINATION lib/cmake/ShaderManagement
    COMPONENT ShaderTools
)