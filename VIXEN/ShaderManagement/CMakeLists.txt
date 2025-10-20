# ShaderManagement static library
# Pure shader compilation and management logic - no RenderGraph dependencies

project(ShaderManagement)

# Collect source files
file(GLOB SHADER_MGMT_HEADERS "include/ShaderManagement/*.h")
file(GLOB SHADER_MGMT_SOURCES "src/*.cpp")

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
)

# Link Vulkan headers only (for VkShaderStageFlagBits enum)
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