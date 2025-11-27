# ============================================================================
# Shader Assets Directory
# ============================================================================
# This directory contains all GLSL shader source files (.vert, .frag, .comp, etc.)
#
# These are source assets that get compiled into SPIR-V by applications at build time
# using add_shader_bundle() from ShaderManagement/cmake/ShaderToolUtils.cmake

cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

# Export shader directory path for applications to reference
set(VIXEN_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Path to VIXEN shader source directory")

message(STATUS "Shader source directory configured: ${VIXEN_SHADER_SOURCE_DIR}")

# List all shader files for IDE organization
file(GLOB_RECURSE SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/*.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.geom
    ${CMAKE_CURRENT_SOURCE_DIR}/*.tesc
    ${CMAKE_CURRENT_SOURCE_DIR}/*.tese
    ${CMAKE_CURRENT_SOURCE_DIR}/*.mesh
    ${CMAKE_CURRENT_SOURCE_DIR}/*.task
    ${CMAKE_CURRENT_SOURCE_DIR}/*.rgen
    ${CMAKE_CURRENT_SOURCE_DIR}/*.rint
    ${CMAKE_CURRENT_SOURCE_DIR}/*.rahit
    ${CMAKE_CURRENT_SOURCE_DIR}/*.rchit
    ${CMAKE_CURRENT_SOURCE_DIR}/*.rmiss
    ${CMAKE_CURRENT_SOURCE_DIR}/*.rcall
    ${CMAKE_CURRENT_SOURCE_DIR}/*.glsl
)

# Create a custom target for shaders (visible in IDE)
add_custom_target(ShaderAssets
    SOURCES ${SHADER_FILES}
    COMMENT "Shader source files (compiled by applications at build time)"
)

# Visual Studio solution folder organization
set_target_properties(ShaderAssets PROPERTIES FOLDER "Assets")

# Group shader files by type in IDE
if(WIN32)
    source_group("Vertex Shaders" REGULAR_EXPRESSION ".*\\.vert$")
    source_group("Fragment Shaders" REGULAR_EXPRESSION ".*\\.frag$")
    source_group("Compute Shaders" REGULAR_EXPRESSION ".*\\.comp$")
    source_group("Geometry Shaders" REGULAR_EXPRESSION ".*\\.geom$")
    source_group("Tessellation Control" REGULAR_EXPRESSION ".*\\.tesc$")
    source_group("Tessellation Evaluation" REGULAR_EXPRESSION ".*\\.tese$")
    source_group("Mesh Shaders" REGULAR_EXPRESSION ".*\\.mesh$")
    source_group("Task Shaders" REGULAR_EXPRESSION ".*\\.task$")
    source_group("Ray Tracing" REGULAR_EXPRESSION ".*\\.(rgen|rint|rahit|rchit|rmiss|rcall)$")
    source_group("Common" REGULAR_EXPRESSION ".*\\.glsl$")
endif()

message(STATUS "ShaderAssets target created with ${CMAKE_MATCH_COUNT} shader files")
