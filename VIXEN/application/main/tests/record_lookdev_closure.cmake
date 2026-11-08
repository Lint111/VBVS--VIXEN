# Records one look-dev family's input closure: runs the family's ctest cases with the open recorder
# preloaded and a 1-frame capture (the loaded shaders, libraries and assets do not depend on the frame
# count; the pixel assertions are not the point of this run, so the ctest result is not the output).
# The output is the set of <pid>.paths recordings; no recording means the step failed.
foreach(_required IN ITEMS CLOSURE_FAMILY CLOSURE_RECORDER CLOSURE_OUT CLOSURE_BUILD_DIR CLOSURE_STAMP)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "record_lookdev_closure: -D${_required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${CLOSURE_OUT}")
file(MAKE_DIRECTORY "${CLOSURE_OUT}")
set(ENV{LD_PRELOAD} "${CLOSURE_RECORDER}")
set(ENV{VIXEN_CLOSURE_OUT} "${CLOSURE_OUT}")
set(ENV{VIXEN_LOOKDEV_CAPTURE_FRAMES} 1)
execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CLOSURE_BUILD_DIR}" -L "^gpu$" -L "^${CLOSURE_FAMILY}$"
            --no-tests=error --output-on-failure
    RESULT_VARIABLE _ctest_result)
file(GLOB _recordings "${CLOSURE_OUT}/*.paths")
list(LENGTH _recordings _count)
if(_count EQUAL 0)
    message(FATAL_ERROR "record_lookdev_closure: ${CLOSURE_FAMILY} produced no recordings (ctest exit ${_ctest_result})")
endif()
message(STATUS "[closure] ${CLOSURE_FAMILY}: ${_count} process recordings (ctest exit ${_ctest_result}, not the output)")
file(WRITE "${CLOSURE_STAMP}" "${_count}\n")
