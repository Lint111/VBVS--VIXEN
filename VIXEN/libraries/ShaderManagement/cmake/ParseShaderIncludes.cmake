# ParseShaderIncludes.cmake
# Utility to extract #include dependencies from GLSL shader files

# ===== parse_shader_includes =====
#
# Recursively parse a shader file to find all #include dependencies
#
# Usage:
#   parse_shader_includes(
#       SHADER_FILE <path/to/shader.vert>
#       OUTPUT_VAR <variable_name>
#       [SEARCH_PATHS <path1> <path2> ...]
#   )
#
# Sets OUTPUT_VAR to a list of all shader files included (directly or indirectly)
#
# Example:
#   parse_shader_includes(
#       SHADER_FILE shaders/main.vert
#       OUTPUT_VAR DEPENDENCIES
#       SEARCH_PATHS shaders/ shaders/common/
#   )
#   # Now DEPENDENCIES contains all included files
#
function(parse_shader_includes)
    set(options "")
    set(oneValueArgs SHADER_FILE OUTPUT_VAR)
    set(multiValueArgs SEARCH_PATHS)
    cmake_parse_arguments(PARSE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT PARSE_SHADER_FILE)
        message(FATAL_ERROR "parse_shader_includes: SHADER_FILE is required")
    endif()

    if(NOT PARSE_OUTPUT_VAR)
        message(FATAL_ERROR "parse_shader_includes: OUTPUT_VAR is required")
    endif()

    # Get absolute path to shader file
    get_filename_component(SHADER_ABS "${PARSE_SHADER_FILE}" ABSOLUTE)
    get_filename_component(SHADER_DIR "${SHADER_ABS}" DIRECTORY)

    # Initialize search paths (shader's directory + user-specified)
    set(SEARCH_PATHS "${SHADER_DIR}")
    if(PARSE_SEARCH_PATHS)
        list(APPEND SEARCH_PATHS ${PARSE_SEARCH_PATHS})
    endif()

    # Remove duplicates from search paths
    list(REMOVE_DUPLICATES SEARCH_PATHS)

    # Initialize dependency list and visited set (to avoid circular includes)
    set(ALL_DEPENDENCIES "")
    set(VISITED_FILES "")

    # Recursively parse the shader file
    _parse_shader_file_recursive(
        "${SHADER_ABS}"
        ALL_DEPENDENCIES
        VISITED_FILES
        "${SEARCH_PATHS}"
    )

    # Return the dependency list
    set(${PARSE_OUTPUT_VAR} ${ALL_DEPENDENCIES} PARENT_SCOPE)
endfunction()

# Internal recursive function to parse a single shader file
function(_parse_shader_file_recursive SHADER_FILE DEP_VAR VISITED_VAR SEARCH_PATHS)
    # Get current values from parent scope
    set(DEPENDENCIES ${${DEP_VAR}})
    set(VISITED ${${VISITED_VAR}})

    # Check if already visited (avoid circular includes)
    list(FIND VISITED "${SHADER_FILE}" ALREADY_VISITED)
    if(NOT ALREADY_VISITED EQUAL -1)
        return()
    endif()

    # Mark as visited
    list(APPEND VISITED "${SHADER_FILE}")

    # Check if file exists
    if(NOT EXISTS "${SHADER_FILE}")
        message(WARNING "Shader file not found: ${SHADER_FILE}")
        set(${VISITED_VAR} ${VISITED} PARENT_SCOPE)
        return()
    endif()

    # Read the shader file
    file(READ "${SHADER_FILE}" SHADER_CONTENT)

    # Extract #include directives using regex
    # Matches: #include "filename" or #include <filename>
    string(REGEX MATCHALL "#include[ \t]+[\"<]([^\">\n]+)[\">]" INCLUDES "${SHADER_CONTENT}")

    foreach(INCLUDE_LINE ${INCLUDES})
        # Extract the filename from the #include directive
        string(REGEX REPLACE "#include[ \t]+[\"<]([^\">\n]+)[\">]" "\\1" INCLUDE_FILE "${INCLUDE_LINE}")

        # Try to find the included file in search paths
        set(FOUND_PATH "")
        foreach(SEARCH_PATH ${SEARCH_PATHS})
            get_filename_component(SEARCH_PATH_ABS "${SEARCH_PATH}" ABSOLUTE)
            set(POTENTIAL_PATH "${SEARCH_PATH_ABS}/${INCLUDE_FILE}")

            if(EXISTS "${POTENTIAL_PATH}")
                get_filename_component(FOUND_PATH "${POTENTIAL_PATH}" ABSOLUTE)
                break()
            endif()
        endforeach()

        if(FOUND_PATH)
            # Add to dependencies
            list(FIND DEPENDENCIES "${FOUND_PATH}" ALREADY_IN_DEPS)
            if(ALREADY_IN_DEPS EQUAL -1)
                list(APPEND DEPENDENCIES "${FOUND_PATH}")
            endif()

            # Recursively parse the included file
            _parse_shader_file_recursive(
                "${FOUND_PATH}"
                DEPENDENCIES
                VISITED
                "${SEARCH_PATHS}"
            )
        else()
            message(WARNING "Could not find included file: ${INCLUDE_FILE} (referenced in ${SHADER_FILE})")
        endif()
    endforeach()

    # Return updated lists to parent scope
    set(${DEP_VAR} ${DEPENDENCIES} PARENT_SCOPE)
    set(${VISITED_VAR} ${VISITED} PARENT_SCOPE)
endfunction()

# ===== get_shader_dependencies =====
#
# Convenience wrapper to get all dependencies for a shader file
# Returns absolute paths to all included files
#
# Usage:
#   get_shader_dependencies(<shader_file> <output_var> [SEARCH_PATHS ...])
#
function(get_shader_dependencies SHADER_FILE OUTPUT_VAR)
    set(SEARCH_PATHS ${ARGN})

    parse_shader_includes(
        SHADER_FILE "${SHADER_FILE}"
        OUTPUT_VAR DEPS
        SEARCH_PATHS ${SEARCH_PATHS}
    )

    set(${OUTPUT_VAR} ${DEPS} PARENT_SCOPE)
endfunction()
