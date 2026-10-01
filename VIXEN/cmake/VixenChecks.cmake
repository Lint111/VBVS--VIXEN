include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/VixenWriteIfDifferent.cmake")

# A successful check is a build output. Both its command and complete input list
# are dependencies, including the generated files being checked. Deleting or
# adding a schema changes the signature even if all remaining mtimes are old.
function(vixen_add_checked_target name)
    cmake_parse_arguments(CHECK "ALL;VERBATIM" "COMMENT;JOB_POOL" "COMMAND;DEPENDS" ${ARGN})
    if(CHECK_UNPARSED_ARGUMENTS OR NOT CHECK_COMMAND)
        message(FATAL_ERROR "vixen_add_checked_target(${name}): invalid arguments")
    endif()
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/checks/${name}.stamp")
    set(_signature "${CMAKE_CURRENT_BINARY_DIR}/checks/${name}.inputs")
    vixen_write_if_different("${_signature}" "${CHECK_COMMAND}\n${CHECK_DEPENDS}\n")
    set(_pool)
    if(CHECK_JOB_POOL)
        set(_pool JOB_POOL "${CHECK_JOB_POOL}")
    endif()
    add_custom_command(OUTPUT "${_stamp}"
        COMMAND ${CHECK_COMMAND}
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS ${CHECK_DEPENDS} "${_signature}" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        COMMENT "${CHECK_COMMENT}" ${_pool} VERBATIM)
    set(_all)
    if(CHECK_ALL)
        set(_all ALL)
    endif()
    add_custom_target(${name} ${_all} DEPENDS "${_stamp}")
endfunction()

function(vixen_add_codegen_check name)
    cmake_parse_arguments(CHECK "ALL;VERBATIM" "COMMENT;JOB_POOL" "COMMAND" ${ARGN})
    set(_inputs "${_yk_dll}" engine_codegen_tool)
    foreach(_argument IN LISTS CHECK_COMMAND)
        set(_path "${_argument}")
        if(_codegen_need_wsl_bridge AND _argument MATCHES "^/")
            # The command uses WSL paths; Ninja on Windows needs native dependency
            # paths. Keep the conversion symmetric with _codegen_to_wsl_path.
            execute_process(COMMAND "${WSL_EXE}" -e wslpath -w "${_argument}"
                OUTPUT_VARIABLE _path OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _translated)
            if(NOT _translated EQUAL 0 OR NOT _path)
                message(FATAL_ERROR "[codegen] cannot translate dependency path ${_argument}")
            endif()
            file(TO_CMAKE_PATH "${_path}" _path)
        endif()
        if(IS_DIRECTORY "${_path}")
            file(GLOB_RECURSE _sources CONFIGURE_DEPENDS "${_path}/*.cs" "${_path}/*.json")
            list(APPEND _inputs ${_sources})
        elseif(IS_ABSOLUTE "${_path}")
            list(APPEND _inputs "${_path}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _inputs)
    set(_all)
    if(CHECK_ALL)
        set(_all ALL)
    endif()
    vixen_add_checked_target(${name} ${_all}
        COMMAND ${CHECK_COMMAND} DEPENDS ${_inputs}
        COMMENT "${CHECK_COMMENT}" JOB_POOL vixen_codegen VERBATIM)
endfunction()
