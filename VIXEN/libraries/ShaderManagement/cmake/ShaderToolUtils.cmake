# ShaderToolUtils.cmake
# CMake utilities for build-time shader compilation using shader_tool

# Include shader dependency parser
include(${CMAKE_CURRENT_LIST_DIR}/ParseShaderIncludes.cmake)

# Find shader_tool executable
find_program(SHADER_TOOL_EXECUTABLE
    NAMES shader_tool
    PATHS
        ${CMAKE_BINARY_DIR}/bin
        ${CMAKE_CURRENT_LIST_DIR}/../build/bin
        ${CMAKE_INSTALL_PREFIX}/bin
    DOC "Shader compilation tool"
)

if(NOT SHADER_TOOL_EXECUTABLE)
    message(WARNING "shader_tool not found. Shader build functions will not work.")
endif()

# Set default output directory for generated shader files
if(NOT DEFINED SHADER_OUTPUT_DIR)
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/shaders" CACHE PATH "Output directory for compiled shaders")
endif()

if(NOT DEFINED SDI_OUTPUT_DIR)
    set(SDI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/sdi" CACHE PATH "Output directory for SDI headers")
endif()

# ===== add_shader_bundle =====
#
# Compile shader stages into a shader bundle at build time
#
# Usage:
#   add_shader_bundle(TARGET_NAME
#       PROGRAM_NAME <name>
#       VERTEX <vertex.vert>
#       [FRAGMENT <fragment.frag>]
#       [COMPUTE <compute.comp>]
#       [GEOMETRY <geometry.geom>]
#       [TESS_CONTROL <tess_control.tesc>]
#       [TESS_EVAL <tess_eval.tese>]
#       [MESH <mesh.mesh>]
#       [TASK <task.task>]
#       [OUTPUT_DIR <directory>]
#       [SDI_NAMESPACE <namespace>]
#       [NO_SDI]
#       [DEPENDS <targets...>]
#   )
#
# Creates a custom target that compiles the shader stages and generates
# a shader bundle JSON file and SDI header.
#
# Example:
#   add_shader_bundle(MyShader_Bundle
#       PROGRAM_NAME "MyShader"
#       VERTEX shaders/shader.vert
#       FRAGMENT shaders/shader.frag
#       OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders
#   )
#
function(add_shader_bundle TARGET_NAME)
    if(NOT SHADER_TOOL_EXECUTABLE)
        message(FATAL_ERROR "shader_tool not found. Cannot add shader bundle ${TARGET_NAME}")
    endif()

    set(options NO_SDI VERBOSE)
    set(oneValueArgs PROGRAM_NAME OUTPUT_DIR SDI_NAMESPACE PIPELINE)
    set(multiValueArgs VERTEX FRAGMENT COMPUTE GEOMETRY TESS_CONTROL TESS_EVAL MESH TASK DEPENDS)
    cmake_parse_arguments(SHADER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SHADER_PROGRAM_NAME)
        message(FATAL_ERROR "add_shader_bundle: PROGRAM_NAME is required")
    endif()

    # Collect all input files
    set(INPUT_FILES "")
    set(INPUT_PATHS "")
    set(ALL_DEPENDENCIES "")

    foreach(STAGE VERTEX FRAGMENT COMPUTE GEOMETRY TESS_CONTROL TESS_EVAL MESH TASK)
        if(SHADER_${STAGE})
            foreach(FILE ${SHADER_${STAGE}})
                # Resolve to absolute path
                get_filename_component(ABS_PATH "${FILE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                list(APPEND INPUT_FILES "${ABS_PATH}")
                list(APPEND INPUT_PATHS "${ABS_PATH}")

                # Parse shader includes for dependency tracking
                # This enables incremental builds when included files change
                get_shader_dependencies("${ABS_PATH}" SHADER_DEPS
                    ${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders
                )

                if(SHADER_DEPS)
                    list(APPEND ALL_DEPENDENCIES ${SHADER_DEPS})
                endif()
            endforeach()
        endif()
    endforeach()

    if(NOT INPUT_FILES)
        message(FATAL_ERROR "add_shader_bundle: No shader stages specified")
    endif()

    # Remove duplicate dependencies
    if(ALL_DEPENDENCIES)
        list(REMOVE_DUPLICATES ALL_DEPENDENCIES)
    endif()

    # Add user-specified dependencies
    if(SHADER_DEPENDS)
        list(APPEND ALL_DEPENDENCIES ${SHADER_DEPENDS})
    endif()

    # Determine output directory
    if(SHADER_OUTPUT_DIR)
        set(OUTPUT_DIR "${SHADER_OUTPUT_DIR}")
    else()
        set(OUTPUT_DIR "${SHADER_OUTPUT_DIR}")
    endif()

    # Determine SDI namespace
    set(SDI_NS "SDI")
    if(SHADER_SDI_NAMESPACE)
        set(SDI_NS "${SHADER_SDI_NAMESPACE}")
    endif()

    # Output files
    set(OUTPUT_BUNDLE "${OUTPUT_DIR}/${SHADER_PROGRAM_NAME}.json")
    set(OUTPUT_SDI "${SDI_OUTPUT_DIR}/${SHADER_PROGRAM_NAME}_SDI.h")

    # Build command
    set(COMMAND_ARGS
        "${SHADER_TOOL_EXECUTABLE}"
        compile
        ${INPUT_FILES}
        --name "${SHADER_PROGRAM_NAME}"
        --output-dir "${OUTPUT_DIR}"
        --sdi-namespace "${SDI_NS}"
        --sdi-dir "${SDI_OUTPUT_DIR}"
    )

    if(SHADER_NO_SDI)
        list(APPEND COMMAND_ARGS --no-sdi)
    endif()

    if(SHADER_VERBOSE)
        list(APPEND COMMAND_ARGS --verbose)
    endif()

    # Create output directories
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    file(MAKE_DIRECTORY "${SDI_OUTPUT_DIR}")

    # Create custom command with proper error handling
    # Using || (echo ... && exit 1) ensures build fails on error
    # Includes automatic dependency tracking for #include directives
    add_custom_command(
        OUTPUT ${OUTPUT_BUNDLE}
        COMMAND ${COMMAND_ARGS} || (${CMAKE_COMMAND} -E echo "ERROR: Shader compilation failed for ${SHADER_PROGRAM_NAME}" && ${CMAKE_COMMAND} -E false)
        DEPENDS ${INPUT_PATHS} ${ALL_DEPENDENCIES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Compiling shader bundle: ${SHADER_PROGRAM_NAME}"
        VERBATIM
    )

    # Create custom target
    add_custom_target(${TARGET_NAME}
        DEPENDS ${OUTPUT_BUNDLE}
    )

    # Set properties for external access
    set_target_properties(${TARGET_NAME} PROPERTIES
        SHADER_BUNDLE_PATH "${OUTPUT_BUNDLE}"
        SHADER_SDI_PATH "${OUTPUT_SDI}"
        SHADER_PROGRAM_NAME "${SHADER_PROGRAM_NAME}"
    )

    message(STATUS "Added shader bundle: ${SHADER_PROGRAM_NAME} -> ${OUTPUT_BUNDLE}")
endfunction()

# ===== add_shader_registry =====
#
# Build a central SDI registry from multiple shader bundles
#
# Usage:
#   add_shader_registry(TARGET_NAME
#       BUNDLES <bundle_target1> <bundle_target2> ...
#       [OUTPUT <registry.h>]
#       [SDI_NAMESPACE <namespace>]
#   )
#
# Creates a custom target that generates SDI_Registry.h from all
# specified shader bundles.
#
# Example:
#   add_shader_registry(ShaderRegistry
#       BUNDLES Shader1_Bundle Shader2_Bundle Shader3_Bundle
#       OUTPUT ${CMAKE_BINARY_DIR}/generated/SDI_Registry.h
#   )
#
function(add_shader_registry TARGET_NAME)
    if(NOT SHADER_TOOL_EXECUTABLE)
        message(FATAL_ERROR "shader_tool not found. Cannot add shader registry ${TARGET_NAME}")
    endif()

    set(options VERBOSE)
    set(oneValueArgs OUTPUT SDI_NAMESPACE)
    set(multiValueArgs BUNDLES)
    cmake_parse_arguments(REGISTRY "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT REGISTRY_BUNDLES)
        message(FATAL_ERROR "add_shader_registry: BUNDLES is required")
    endif()

    # Collect bundle paths
    set(BUNDLE_PATHS "")
    set(BUNDLE_DEPENDS "")
    foreach(BUNDLE_TARGET ${REGISTRY_BUNDLES})
        get_target_property(BUNDLE_PATH ${BUNDLE_TARGET} SHADER_BUNDLE_PATH)
        if(NOT BUNDLE_PATH)
            message(FATAL_ERROR "add_shader_registry: ${BUNDLE_TARGET} is not a shader bundle target")
        endif()
        list(APPEND BUNDLE_PATHS "${BUNDLE_PATH}")
        list(APPEND BUNDLE_DEPENDS ${BUNDLE_TARGET})
    endforeach()

    # Determine output path
    if(REGISTRY_OUTPUT)
        set(OUTPUT_FILE "${REGISTRY_OUTPUT}")
    else()
        set(OUTPUT_FILE "${SDI_OUTPUT_DIR}/SDI_Registry.h")
    endif()

    get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    # Build command
    set(COMMAND_ARGS
        "${SHADER_TOOL_EXECUTABLE}"
        build-registry
        ${BUNDLE_PATHS}
        --output "${OUTPUT_FILE}"
    )

    if(REGISTRY_SDI_NAMESPACE)
        list(APPEND COMMAND_ARGS --sdi-namespace "${REGISTRY_SDI_NAMESPACE}")
    endif()

    if(REGISTRY_VERBOSE)
        list(APPEND COMMAND_ARGS --verbose)
    endif()

    # Create custom command with error handling
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${COMMAND_ARGS} || (${CMAKE_COMMAND} -E echo "ERROR: Shader registry generation failed" && ${CMAKE_COMMAND} -E false)
        DEPENDS ${BUNDLE_PATHS} ${BUNDLE_DEPENDS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building shader registry: ${OUTPUT_FILE}"
        VERBATIM
    )

    # Create custom target
    add_custom_target(${TARGET_NAME}
        DEPENDS ${OUTPUT_FILE}
    )

    # Set properties
    set_target_properties(${TARGET_NAME} PROPERTIES
        SHADER_REGISTRY_PATH "${OUTPUT_FILE}"
    )

    message(STATUS "Added shader registry: ${OUTPUT_FILE}")
endfunction()

# ===== add_shader_batch =====
#
# Process multiple shaders from a JSON configuration file
#
# Usage:
#   add_shader_batch(TARGET_NAME
#       CONFIG <config.json>
#       [OUTPUT_DIR <directory>]
#   )
#
# Config file format:
# {
#   "shaders": [
#     {
#       "name": "MyShader",
#       "stages": ["shader.vert", "shader.frag"],
#       "pipeline": "graphics"
#     },
#     ...
#   ],
#   "buildRegistry": true
# }
#
function(add_shader_batch TARGET_NAME)
    if(NOT SHADER_TOOL_EXECUTABLE)
        message(FATAL_ERROR "shader_tool not found. Cannot add shader batch ${TARGET_NAME}")
    endif()

    set(options VERBOSE)
    set(oneValueArgs CONFIG OUTPUT_DIR)
    cmake_parse_arguments(BATCH "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT BATCH_CONFIG)
        message(FATAL_ERROR "add_shader_batch: CONFIG is required")
    endif()

    # Resolve config path
    get_filename_component(CONFIG_PATH "${BATCH_CONFIG}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    if(NOT EXISTS "${CONFIG_PATH}")
        message(FATAL_ERROR "add_shader_batch: Config file not found: ${CONFIG_PATH}")
    endif()

    # Determine output directory
    if(BATCH_OUTPUT_DIR)
        set(OUTPUT_DIR "${BATCH_OUTPUT_DIR}")
    else()
        set(OUTPUT_DIR "${SHADER_OUTPUT_DIR}")
    endif()

    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    # Build command
    set(COMMAND_ARGS
        "${SHADER_TOOL_EXECUTABLE}"
        batch
        "${CONFIG_PATH}"
        --output-dir "${OUTPUT_DIR}"
    )

    if(BATCH_VERBOSE)
        list(APPEND COMMAND_ARGS --verbose)
    endif()

    # Create custom target (runs every build to detect config changes)
    add_custom_target(${TARGET_NAME}
        COMMAND ${COMMAND_ARGS}
        DEPENDS "${CONFIG_PATH}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Processing shader batch: ${BATCH_CONFIG}"
        VERBATIM
    )

    message(STATUS "Added shader batch: ${BATCH_CONFIG}")
endfunction()

# ===== Example configuration file =====
#
# Save as shaders.json:
# {
#   "shaders": [
#     {
#       "name": "BasicShader",
#       "stages": ["shaders/basic.vert", "shaders/basic.frag"],
#       "pipeline": "graphics"
#     },
#     {
#       "name": "ComputeShader",
#       "stages": ["shaders/compute.comp"],
#       "pipeline": "compute"
#     }
#   ],
#   "buildRegistry": true
# }
#
# Then in CMakeLists.txt:
#   add_shader_batch(AllShaders CONFIG shaders.json)
