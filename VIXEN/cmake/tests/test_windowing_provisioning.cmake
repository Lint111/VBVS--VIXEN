cmake_minimum_required(VERSION 3.21)

set(_test_root "${VIXEN_WINDOWING_TEST_ROOT}")
if(NOT _test_root)
    set(_test_root "${CMAKE_CURRENT_BINARY_DIR}/windowing-provisioning")
endif()
set(_fake_bin "${_test_root}/fake-bin")
set(_probe "${_test_root}/probe")
set(_build "${_test_root}/build")
set(_cache "${_test_root}/cache")
set(_log "${_test_root}/configure.log")
file(MAKE_DIRECTORY "${_fake_bin}" "${_probe}")

file(WRITE "${_fake_bin}/apt-get" [=[#!/bin/sh
echo "synthetic apt failure" >&2
exit 23
]=])
file(WRITE "${_fake_bin}/dpkg-deb" [=[#!/bin/sh
echo "synthetic dpkg failure" >&2
exit 24
]=])
file(CHMOD "${_fake_bin}/apt-get" "${_fake_bin}/dpkg-deb"
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

set(_vixen_root "${CMAKE_CURRENT_LIST_DIR}/../..")
file(WRITE "${_probe}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.21)\n"
"project(WindowingProvisionProbe NONE)\n"
"set(VIXEN_ROOT \"${_vixen_root}\")\n"
"set(VIXEN_WINDOWING_CACHE_DIR \"${_cache}\")\n"
"set(VIXEN_AUTO_PROVISION_WINDOWING ON)\n"
"set(VIXEN_REQUIRE_WINDOWING ON)\n"
"set(VIXEN_WINDOWING_SYSTEM_INCLUDE_DIR \"${_test_root}/missing-system-header\")\n"
"include(\"${_vixen_root}/cmake/ProvisionWindowingDeps.cmake\")\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_fake_bin}:$ENV{PATH}"
            "${CMAKE_COMMAND}" -S "${_probe}" -B "${_build}"
    RESULT_VARIABLE _result
    OUTPUT_FILE "${_log}"
    ERROR_FILE "${_log}")
if(_result EQUAL 0)
    message(FATAL_ERROR "required X11 provisioning unexpectedly succeeded; see ${_log}")
endif()

file(READ "${_log}" _output)
foreach(_needle IN ITEMS
    "what: X11/Xlib.h"
    "where:"
    "why: GLFW's X11 backend"
    "fix: provision the packages"
    "apt-get download"
    "libx11-dev"
    "synthetic apt failure")
    string(FIND "${_output}" "${_needle}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "windowing failure omitted '${_needle}'; see ${_log}\n${_output}")
    endif()
endforeach()

message(STATUS "windowing prerequisite failure contract includes actionable apt diagnostics")
