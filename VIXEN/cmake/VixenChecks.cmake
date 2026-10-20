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
    set_property(GLOBAL APPEND PROPERTY VIXEN_CHECK_TARGETS ${name})
endfunction()

# Working directories and launcher options are command identity, not schema
# inputs. In particular, `wsl --cd /` must never cause a recursive scan of `/`.
function(vixen_codegen_payload_arguments out_var)
    set(_command ${ARGN})
    list(LENGTH _CODEGEN_RUNNER _launcher_count)
    list(LENGTH _command _command_count)
    if(_command_count LESS _launcher_count OR _launcher_count EQUAL 0)
        message(FATAL_ERROR "[codegen] command does not start with the configured launcher")
    endif()
    list(SUBLIST _command 0 ${_launcher_count} _prefix)
    if(NOT "${_prefix}" STREQUAL "${_CODEGEN_RUNNER}")
        message(FATAL_ERROR "[codegen] command does not start with the configured launcher")
    endif()
    list(SUBLIST _command ${_launcher_count} -1 _payload)
    set(${out_var} "${_payload}" PARENT_SCOPE)
endfunction()

function(vixen_add_codegen_check name)
    cmake_parse_arguments(CHECK "ALL;VERBATIM" "COMMENT;JOB_POOL" "COMMAND" ${ARGN})
    set(_inputs "${_yk_dll}" "${_tool_stamp}" engine_codegen_tool)
    vixen_codegen_payload_arguments(_payload ${CHECK_COMMAND})
    foreach(_argument IN LISTS _payload)
        set(_path "${_argument}")
        if(_codegen_need_wsl_bridge AND _argument MATCHES "^/")
            # The command uses WSL paths; Ninja on Windows needs native dependency
            # paths. Keep the conversion symmetric with _codegen_to_wsl_path.
            string(SHA256 _path_key "${_argument}")
            get_property(_path GLOBAL PROPERTY "_vixen_codegen_native_path_${_path_key}")
            if(NOT _path)
                execute_process(COMMAND "${WSL_EXE}" --cd / -e wslpath -w "${_argument}"
                    OUTPUT_VARIABLE _path OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _translated)
                if(NOT _translated EQUAL 0 OR NOT _path)
                    message(FATAL_ERROR "[codegen] cannot translate dependency path ${_argument}")
                endif()
                file(TO_CMAKE_PATH "${_path}" _path)
                set_property(GLOBAL PROPERTY "_vixen_codegen_native_path_${_path_key}" "${_path}")
            endif()
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
