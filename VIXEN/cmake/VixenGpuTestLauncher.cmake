# Runtime launcher for discovered GPU tests.
#
# Translated Vulkan (Mesa Dozen/Venus) is a correctness smoke device only.  CTest invokes this
# script through the target TEST_LAUNCHER property, so both gtest discovery and each case use the
# exact same environment.  Discovery is always forwarded; one case per target is marked with
# VIXEN_GPU_TEST_SMOKE=1 and is the only case allowed to execute on a translated device.

set(_command)
set(_start 3) # cmake -P <script> [--] <executable> <arguments...>
if(CMAKE_ARGC GREATER 3 AND CMAKE_ARGV3 STREQUAL "--")
    set(_start 4)
endif()
if(CMAKE_ARGC LESS_EQUAL ${_start})
    message(FATAL_ERROR "VixenGpuTestLauncher.cmake: missing test command")
endif()
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_index RANGE ${_start} ${_last})
    list(APPEND _command "${CMAKE_ARGV${_index}}")
endforeach()

list(FIND _command "--gtest_list_tests" _list_index)
set(_is_discovery FALSE)
if(NOT _list_index EQUAL -1)
    set(_is_discovery TRUE)
endif()

set(_translated FALSE)
if(DEFINED ENV{VIXEN_TEST_DEVICE_CLASS} AND "$ENV{VIXEN_TEST_DEVICE_CLASS}" STREQUAL "translated")
    set(_translated TRUE)
elseif(DEFINED ENV{VK_ICD_FILENAMES}
       AND "$ENV{VK_ICD_FILENAMES}" MATCHES "[Dd][Zz][Nn]|[Dd][Oo][Zz][Ee][Nn]|[Vv][Ee][Nn][Uu][Ss]")
    set(_translated TRUE)
else()
    find_program(_vulkaninfo NAMES vulkaninfo vulkaninfo.exe)
    if(_vulkaninfo)
        execute_process(COMMAND "${_vulkaninfo}" --summary
            RESULT_VARIABLE _vulkaninfo_result
            OUTPUT_VARIABLE _vulkaninfo_output
            ERROR_VARIABLE _vulkaninfo_error
            TIMEOUT 10)
        string(CONCAT _vulkaninfo_text "${_vulkaninfo_output}" "${_vulkaninfo_error}")
        if(_vulkaninfo_text MATCHES "[Dd]irect3[Dd]12|[Dd]ozen|[Vv]enus")
            set(_translated TRUE)
        endif()
    endif()
endif()

if(_translated AND NOT _is_discovery
   AND (NOT DEFINED ENV{VIXEN_GPU_TEST_SMOKE} OR NOT "$ENV{VIXEN_GPU_TEST_SMOKE}" STREQUAL "1"))
    message(STATUS "[GPU TEST SKIP] smoke only on translated device (set VIXEN_GPU_TEST_SMOKE=1 for the family smoke)")
    cmake_language(EXIT 77)
endif()

execute_process(COMMAND ${_command} RESULT_VARIABLE _result)
if(_result MATCHES "^[0-9]+$")
    cmake_language(EXIT ${_result})
endif()
message(FATAL_ERROR "VixenGpuTestLauncher.cmake: test terminated abnormally (${_result})")
