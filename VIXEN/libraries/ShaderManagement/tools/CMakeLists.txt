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

# Merged-SDI currency gate in the default build (not only ctest): a stale committed
# generated/sdi/merged/*-SDI.g.h fails the build, before the test suite runs. Regenerate
# with the sdi_merged_regen target.
get_filename_component(_vixen_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include("${_vixen_root}/cmake/VixenChecks.cmake")
file(GLOB_RECURSE _sdi_merged_inputs CONFIGURE_DEPENDS
    "${_vixen_root}/shaders/*.comp" "${_vixen_root}/shaders/*.glsl" "${_vixen_root}/shaders/*.h"
    "${_vixen_root}/libraries/SVO/shaders/*.comp" "${_vixen_root}/libraries/SVO/shaders/*.glsl"
    "${_vixen_root}/libraries/SVO/shaders/*.h"
    "${_vixen_root}/generated/sdi/merged/*-SDI.g.h")
vixen_add_checked_target(sdi_merged_drift_gate ALL
    COMMAND sdi_tool merge-variants "${_vixen_root}/shaders/sdi-variants.json" --check
    DEPENDS sdi_tool "${_vixen_root}/shaders/sdi-variants.json" ${_sdi_merged_inputs}
    COMMENT "Merged SDI drift gate (stale? build target sdi_merged_regen)"
    VERBATIM)
add_custom_target(sdi_merged_regen
    COMMAND sdi_tool merge-variants "${_vixen_root}/shaders/sdi-variants.json"
    WORKING_DIRECTORY "${_vixen_root}"
    COMMENT "Regenerate merged SDI headers from shaders/sdi-variants.json"
    VERBATIM)
add_dependencies(sdi_merged_regen sdi_tool)

vixen_schedule_runtime_dlls_for_current_directory()
