cmake_minimum_required(VERSION 3.21)

# T-6564: VIXEN_CAPTURE_ROOT moves baked build-tree capture paths at run time.
if(NOT DEFINED VIXEN_CAPTURE_ROOT_TEST_DIR)
    message(FATAL_ERROR "VIXEN_CAPTURE_ROOT_TEST_DIR must be a scratch directory inside the caller's build tree")
endif()
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${vixen_root}/cmake/VixenCaptureRootRuntime.cmake")
file(REMOVE_RECURSE "${VIXEN_CAPTURE_ROOT_TEST_DIR}")
file(MAKE_DIRECTORY "${VIXEN_CAPTURE_ROOT_TEST_DIR}")

function(expect_equal actual expected what)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${what}: got [${actual}], expected [${expected}]")
    endif()
endfunction()

set(default_root "/build/runtime-captures")

unset(ENV{VIXEN_CAPTURE_ROOT})
vixen_capture_runtime_rebase("${default_root}/lookdev/midday" "${default_root}" unset_value)
expect_equal("${unset_value}" "${default_root}/lookdev/midday" "unset root keeps the baked path")

set(ENV{VIXEN_CAPTURE_ROOT} "")
vixen_capture_runtime_rebase("${default_root}/lookdev" "${default_root}" empty_value)
expect_equal("${empty_value}" "${default_root}/lookdev" "empty root keeps the baked path")

set(ENV{VIXEN_CAPTURE_ROOT} "/persist/run1")
vixen_capture_runtime_rebase("${default_root}/lookdev/midday" "${default_root}" nested)
expect_equal("${nested}" "/persist/run1/lookdev/midday" "nested path moves under the run-time root")
vixen_capture_runtime_rebase("${default_root}" "${default_root}" exact)
expect_equal("${exact}" "/persist/run1" "the default root itself moves")
vixen_capture_runtime_rebase("${default_root}-other/x" "${default_root}" sibling)
expect_equal("${sibling}" "${default_root}-other/x" "a sibling prefix is not rebased")
vixen_capture_runtime_rebase("/elsewhere/x" "${default_root}" foreign)
expect_equal("${foreign}" "/elsewhere/x" "a path outside the default root is not rebased")

# End to end: the wrapper rebases the listed variables, leaves others, and propagates failure.
file(WRITE "${VIXEN_CAPTURE_ROOT_TEST_DIR}/probe.cmake"
    "file(WRITE \"\${OUT}\" \"\$ENV{CAPTURE_A}|\$ENV{CAPTURE_B}|\$ENV{CAPTURE_C}\")\n"
    "if(DEFINED FAIL)\n    message(FATAL_ERROR \"probe failing on request\")\nendif()\n")
set(probe_out "${VIXEN_CAPTURE_ROOT_TEST_DIR}/probe.out")
set(ENV{VIXEN_CAPTURE_ROOT} "${VIXEN_CAPTURE_ROOT_TEST_DIR}/persist")
set(ENV{CAPTURE_A} "${default_root}/a")
set(ENV{CAPTURE_B} "${default_root}/b")
set(ENV{CAPTURE_C} "${default_root}/c")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_CAPTURE_DEFAULT_ROOT=${default_root}"
        "-DVIXEN_CAPTURE_REBASE_VARS=CAPTURE_A,CAPTURE_B"
        "-DVIXEN_CAPTURE_MKPARENT_VARS=CAPTURE_A"
        -P "${vixen_root}/cmake/run_with_capture_root.cmake"
        "${CMAKE_COMMAND}" "-DOUT=${probe_out}" -P "${VIXEN_CAPTURE_ROOT_TEST_DIR}/probe.cmake"
    RESULT_VARIABLE wrapper_result)
expect_equal("${wrapper_result}" "0" "wrapper exit code")
file(READ "${probe_out}" probe_text)
set(root "${VIXEN_CAPTURE_ROOT_TEST_DIR}/persist")
expect_equal("${probe_text}" "${root}/a|${root}/b|${default_root}/c" "wrapped environment")
if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "MKPARENT did not create ${root}")
endif()

# --name=<path> options are rebased too, and cmake's own --fresh is only reachable in that spelling (row 15).
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_CAPTURE_DEFAULT_ROOT=${default_root}"
        -P "${vixen_root}/cmake/run_with_capture_root.cmake"
        "${CMAKE_COMMAND}" -E echo "--fresh=${default_root}/cold"
    RESULT_VARIABLE option_result OUTPUT_VARIABLE option_text ERROR_QUIET)
expect_equal("${option_result}" "0" "wrapper passes a cmake-named --fresh=<dir> option through")
string(REGEX MATCH "--fresh=[^\n]*" option_text "${option_text}")
string(STRIP "${option_text}" option_text)
expect_equal("${option_text}" "--fresh=${root}/cold" "a --name=<path> option is rebased to the run-time root")

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_CAPTURE_DEFAULT_ROOT=${default_root}"
        -P "${vixen_root}/cmake/run_with_capture_root.cmake"
        "${CMAKE_COMMAND}" "-DOUT=${probe_out}" -DFAIL=1 -P "${VIXEN_CAPTURE_ROOT_TEST_DIR}/probe.cmake"
    RESULT_VARIABLE failing_result OUTPUT_QUIET ERROR_QUIET)
if(failing_result STREQUAL "0")
    message(FATAL_ERROR "wrapper swallowed a failing command")
endif()
message(STATUS "capture-root contract verified")
