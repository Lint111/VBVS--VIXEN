# ShaderManagement static library
# Pure shader compilation and management logic - no RenderGraph dependencies

cmake_minimum_required(VERSION 3.15)

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

# Link to project-level hash provider for consistent hashing behavior
target_link_libraries(ShaderManagement PRIVATE ProjectHash)

# Find dependencies
find_package(Vulkan REQUIRED)

# Optional OpenSSL for SHA256 hashing
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found - enabling SHA256 shader hashing for ShaderManagement")
    target_link_libraries(ShaderManagement PRIVATE OpenSSL::Crypto)
    target_compile_definitions(ShaderManagement PRIVATE HAS_OPENSSL)
    target_include_directories(ShaderManagement PRIVATE ${OPENSSL_INCLUDE_DIR})
else()
    message(WARNING "OpenSSL not found - ShaderManagement will use the project hash fallback or header-only provider if available")
endif()

# Link Vulkan
target_link_libraries(ShaderManagement
    PUBLIC
        Vulkan::Headers
)

# C++23 standard
target_compile_features(ShaderManagement PUBLIC cxx_std_23)

# Platform definitions
if(WIN32)
    target_compile_definitions(ShaderManagement PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

# Organize in IDE
set_target_properties(ShaderManagement PROPERTIES FOLDER "Libraries")

# ===== Build Tools =====

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

# Add tests subdirectory (if testing is enabled)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Install CMake utility modules for downstream projects
install(FILES
    cmake/ShaderToolUtils.cmake
    cmake/ParseShaderIncludes.cmake
    DESTINATION lib/cmake/ShaderManagement
    COMPONENT ShaderTools
)