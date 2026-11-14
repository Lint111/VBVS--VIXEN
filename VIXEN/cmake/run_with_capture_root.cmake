# run_with_capture_root.cmake
#
# Usage: cmake -DVIXEN_CAPTURE_DEFAULT_ROOT=<baked root> [-DVIXEN_CAPTURE_REBASE_VARS=<ENV,NAMES>]
#              [-DVIXEN_CAPTURE_MKPARENT_VARS=<ENV,NAMES>]
#              -P run_with_capture_root.cmake <command> [args...]
#
# Runs <command> with each listed environment variable moved from the baked default capture root to
# VIXEN_CAPTURE_ROOT when that is set (T-6564), so a CTest case registered with a configure-time
# ENVIRONMENT path still writes where the caller asked at run time. Output streams through; a
# non-zero exit fails the test with the command named.
if(NOT DEFINED VIXEN_CAPTURE_DEFAULT_ROOT)
    message(FATAL_ERROR "run_with_capture_root.cmake: -DVIXEN_CAPTURE_DEFAULT_ROOT is required "
        "(registered by vixen_capture_command in cmake/VixenCaptureRoot.cmake)")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/VixenCaptureRootRuntime.cmake")

set(_command)
set(_after_script FALSE)
foreach(_index RANGE 0 ${CMAKE_ARGC})
    if(_index GREATER_EQUAL CMAKE_ARGC)
        break()
    endif()
    set(_arg "${CMAKE_ARGV${_index}}")
    if(_after_script)
        list(APPEND _command "${_arg}")
    elseif(_previous STREQUAL "-P")
        set(_after_script TRUE)
    endif()
    set(_previous "${_arg}")
endforeach()
if(NOT _command)
    message(FATAL_ERROR "run_with_capture_root.cmake: no command given after the script path")
endif()

vixen_capture_runtime_root("${VIXEN_CAPTURE_DEFAULT_ROOT}" _root)
string(REPLACE "," ";" _rebase_vars "${VIXEN_CAPTURE_REBASE_VARS}")
foreach(_name IN LISTS _rebase_vars)
    if(DEFINED ENV{${_name}})
        vixen_capture_runtime_rebase("$ENV{${_name}}" "${VIXEN_CAPTURE_DEFAULT_ROOT}" _value)
        set(ENV{${_name}} "${_value}")
    endif()
endforeach()
string(REPLACE "," ";" _mkparent_vars "${VIXEN_CAPTURE_MKPARENT_VARS}")
foreach(_name IN LISTS _mkparent_vars)
    if(DEFINED ENV{${_name}})
        get_filename_component(_parent "$ENV{${_name}}" DIRECTORY)
        file(MAKE_DIRECTORY "${_parent}")
    endif()
endforeach()
# Commands such as the cold-pipeline witness receive capture paths as ordinary arguments (for
 # example --expect-file), not environment variables. Keep those arguments in the same runtime root
 # as the rebased capture environment.
set(_rebased_command)
# An option spelled --name=<path> keeps its name and rebases its value. (A command that takes a path option must spell it
# this way: cmake itself reads the arguments after -P, so a bare cmake option such as --fresh <dir> is rejected by cmake
# before this script runs.)
foreach(_arg IN LISTS _command)
    if("${_arg}" MATCHES "^(--[^=]+=)(.*)$")
        vixen_capture_runtime_rebase("${CMAKE_MATCH_2}" "${VIXEN_CAPTURE_DEFAULT_ROOT}" _value)
        set(_value "${CMAKE_MATCH_1}${_value}")
    else()
        vixen_capture_runtime_rebase("${_arg}" "${VIXEN_CAPTURE_DEFAULT_ROOT}" _value)
    endif()
    list(APPEND _rebased_command "${_value}")
endforeach()
set(_command "${_rebased_command}")
if(NOT _root STREQUAL VIXEN_CAPTURE_DEFAULT_ROOT)
    message(STATUS "Capture root ${_root} (VIXEN_CAPTURE_ROOT) replaces ${VIXEN_CAPTURE_DEFAULT_ROOT}")
endif()

execute_process(COMMAND ${_command} RESULT_VARIABLE _result)
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "Capture command failed (${_result}): ${_command}\n"
        "Capture root in effect: ${_root}")
endif()
