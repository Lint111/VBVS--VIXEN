# ShaderManagement static library
# Pure shader compilation and management logic - no RenderGraph dependencies

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
        ${spirv_reflect_SOURCE_DIR}  # For spirv_reflect.h/c
)

# Link Vulkan headers
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