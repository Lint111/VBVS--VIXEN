# ShaderManagement Build Tools

# Shader tool executable
add_executable(shader_tool
    shader_tool.cpp
)

target_link_libraries(shader_tool
    PRIVATE
        ShaderManagement
        nlohmann_json::nlohmann_json
)

target_include_directories(shader_tool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Set C++17 standard
target_compile_features(shader_tool PRIVATE cxx_std_17)

# Install shader_tool to bin directory for use in CMake build steps
install(TARGETS shader_tool
    RUNTIME DESTINATION bin
    COMPONENT ShaderTools
)

# Export the target for use in parent build
set_target_properties(shader_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Make shader_tool available as a target for dependent projects
export(TARGETS shader_tool FILE ShaderToolTargets.cmake)
