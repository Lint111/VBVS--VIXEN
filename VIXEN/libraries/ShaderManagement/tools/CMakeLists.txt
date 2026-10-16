# ShaderManagement Build Tools

# SDI tool executable (shader compiler + descriptor interface generator)
add_executable(sdi_tool
    shader_tool.cpp
)

# Link against library target - includes propagate automatically
target_link_libraries(sdi_tool
    PRIVATE
        ShaderManagement
        nlohmann_json::nlohmann_json
)

# Set C++17 standard
target_compile_features(sdi_tool PRIVATE cxx_std_17)

# Install sdi_tool to bin directory for use in CMake build steps
install(TARGETS sdi_tool
    RUNTIME DESTINATION bin
    COMPONENT ShaderTools
)

# Export the target for use in parent build
# Output to predictable location (no Debug/Release subfolder) for easy developer access
set_target_properties(sdi_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

# Make sdi_tool available as a target for dependent projects
export(TARGETS sdi_tool FILE SdiToolTargets.cmake)

# Visual Studio solution folder organization
set_target_properties(sdi_tool PROPERTIES FOLDER "Application")

# Backward compatibility alias
add_executable(shader_tool ALIAS sdi_tool)
