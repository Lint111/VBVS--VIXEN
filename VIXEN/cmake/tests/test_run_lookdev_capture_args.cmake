# Proves run_lookdev_capture.cmake forwards LOOKDEV_CAPTURE_RENDER_SCALE / _UPSCALER to the capture
# executable as --render-scale= / --upscaler= (T-6170), leaves the command line unchanged when they are
# unset, and rejects malformed values with a located message.  A fake POSIX capture executable records its
# arguments and writes identical PNG stand-ins, so no GPU or CTest configure is needed.
if(NOT DEFINED LOOKDEV_ARGS_TEST_ROOT OR NOT DEFINED LOOKDEV_WRAPPER)
    message(FATAL_ERROR "LOOKDEV_ARGS_TEST_ROOT and LOOKDEV_WRAPPER are required")
endif()
if(WIN32)
    message(STATUS "skipped: the fake capture executable is a POSIX shell script")
    return()
endif()

file(REMOVE_RECURSE "${LOOKDEV_ARGS_TEST_ROOT}")
file(MAKE_DIRECTORY "${LOOKDEV_ARGS_TEST_ROOT}")
set(_fake "${LOOKDEV_ARGS_TEST_ROOT}/fake_capture.sh")
file(WRITE "${_fake}" "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"${LOOKDEV_ARGS_TEST_ROOT}/args.txt\"\nmkdir -p \"$VIXEN_LOOKDEV_CAPTURE_DIR\"\nfor a in 0 1 2 3; do printf png > \"$VIXEN_LOOKDEV_CAPTURE_DIR/midday-angle-$a.png\"; done\n")
file(CHMOD "${_fake}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

function(run_wrapper _label)
    file(REMOVE "${LOOKDEV_ARGS_TEST_ROOT}/args.txt")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DLOOKDEV_CAPTURE_EXE=${_fake}"
                "-DLOOKDEV_CAPTURE_DIR=${LOOKDEV_ARGS_TEST_ROOT}/out-${_label}"
                -DLOOKDEV_CAPTURE_STATES=midday ${ARGN}
                -P "${LOOKDEV_WRAPPER}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    set(_rc "${_rc}" PARENT_SCOPE)
    set(_err "${_out}${_err}" PARENT_SCOPE)
endfunction()

run_wrapper(default)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "default wrapper run failed (${_rc}): ${_err}")
endif()
file(READ "${LOOKDEV_ARGS_TEST_ROOT}/args.txt" _args)
if(_args MATCHES "--render-scale|--upscaler")
    message(FATAL_ERROR "unset config must not forward render-scale/upscaler; got:\n${_args}")
endif()

run_wrapper(forward -DLOOKDEV_CAPTURE_RENDER_SCALE=0.5 -DLOOKDEV_CAPTURE_UPSCALER=fsr1)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "forwarding wrapper run failed (${_rc}): ${_err}")
endif()
file(READ "${LOOKDEV_ARGS_TEST_ROOT}/args.txt" _args)
foreach(_expected IN ITEMS "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically" "--render-scale=0.5" "--upscaler=fsr1")
    string(FIND "${_args}" "${_expected}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "wrapper did not forward ${_expected}; capture executable received:\n${_args}")
    endif()
endforeach()

run_wrapper(bad-scale -DLOOKDEV_CAPTURE_RENDER_SCALE=half)
if(_rc EQUAL 0 OR NOT _err MATCHES "LOOKDEV_CAPTURE_RENDER_SCALE='half'")
    message(FATAL_ERROR "malformed render scale must fail naming the value; rc=${_rc}: ${_err}")
endif()
run_wrapper(bad-upscaler -DLOOKDEV_CAPTURE_UPSCALER=dlss)
if(_rc EQUAL 0 OR NOT _err MATCHES "LOOKDEV_CAPTURE_UPSCALER='dlss'")
    message(FATAL_ERROR "unknown upscaler must fail naming the value; rc=${_rc}: ${_err}")
endif()
message(STATUS "run_lookdev_capture.cmake forwards render-scale/upscaler per config and rejects bad values")
