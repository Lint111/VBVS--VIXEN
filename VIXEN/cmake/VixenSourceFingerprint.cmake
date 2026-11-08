include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/VixenWriteIfDifferent.cmake")  # vixen_write_if_different

# Content fingerprint of the code that produces a cached artifact.
#
#   vixen_source_fingerprint(<target> HEADER <path> CONSTANT <ns::name>
#       [DIRS <dir>...] [FILES <file>...])
#
# Defines custom target <target> that (re)writes HEADER with
#   namespace <ns> { inline constexpr char <name>[] = "<16 hex>"; }
# whenever the SHA-256 over every .h/.hpp/.inl/.cpp under DIRS (recursively) plus FILES, salted with
# the compiler id and version, changes. The value depends on file contents only, never on paths, so
# two build trees (or branches) with identical generator code agree on it and may share a store; any
# source difference yields a different value and therefore a different store entry. A cache key that
# folds this value in cannot serve data built by other generator code.
function(vixen_source_fingerprint target)
    # Function-relative: a plain variable set at include time is invisible from other directories.
    set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VixenSourceFingerprintScript.cmake")
    cmake_parse_arguments(FP "" "HEADER;CONSTANT" "DIRS;FILES" ${ARGN})
    if(NOT FP_HEADER OR NOT FP_CONSTANT)
        message(FATAL_ERROR "vixen_source_fingerprint(${target}): HEADER and CONSTANT are required")
    endif()

    set(_inputs "")
    foreach(_dir IN LISTS FP_DIRS)
        file(GLOB_RECURSE _found CONFIGURE_DEPENDS
            "${_dir}/*.h" "${_dir}/*.hpp" "${_dir}/*.inl" "${_dir}/*.cpp")
        list(APPEND _inputs ${_found})
    endforeach()
    list(APPEND _inputs ${FP_FILES})
    list(REMOVE_DUPLICATES _inputs)
    list(SORT _inputs)
    if(NOT _inputs)
        message(FATAL_ERROR "vixen_source_fingerprint(${target}): no source files under DIRS='${FP_DIRS}' FILES='${FP_FILES}'")
    endif()

    set(_list_file "${CMAKE_CURRENT_BINARY_DIR}/${target}.inputs.txt")
    string(REPLACE ";" "\n" _list_text "${_inputs}")
    vixen_write_if_different("${_list_file}" "${_list_text}\n")

    add_custom_command(OUTPUT "${FP_HEADER}"
        COMMAND "${CMAKE_COMMAND}"
            "-DOUT_HEADER=${FP_HEADER}"
            "-DCONSTANT=${FP_CONSTANT}"
            "-DLIST_FILE=${_list_file}"
            "-DSALT=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
            -P "${_script}"
        DEPENDS ${_inputs} "${_list_file}" "${_script}"
        COMMENT "Fingerprinting ${target} generator sources"
        VERBATIM)
    add_custom_target(${target} DEPENDS "${FP_HEADER}")
endfunction()
