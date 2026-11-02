if(NOT DEFINED LOOKDEV_CAPTURE_EXE OR NOT DEFINED LOOKDEV_CAPTURE_DIR)
    message(FATAL_ERROR "LOOKDEV_CAPTURE_EXE and LOOKDEV_CAPTURE_DIR are required")
endif()

if(NOT DEFINED LOOKDEV_CAPTURE_BUDGET_S)
    set(LOOKDEV_CAPTURE_BUDGET_S 900)
endif()
# Keep the child process guard aligned with the suite-level CMake budget.  The
# capture executable defaults to a shorter standalone budget when invoked
# directly, but this wrapper explicitly owns the longer suite budget.
set(ENV{VIXEN_LOOKDEV_CAPTURE_BUDGET_MS} "${LOOKDEV_CAPTURE_BUDGET_S}000")
string(TIMESTAMP _suite_start "%s")

# LOOKDEV_CAPTURE_REPEAT=OFF is the single-pass artifact mode (T-2621): each state is rendered once
# and the byte-identity witness is skipped, halving the wall time. The repeat gate is not weakened:
# the default (ON) and the registered per-state Application.LookdevCapture.State.* tests still run
# both fresh-process renders and compare them.
if(NOT DEFINED LOOKDEV_CAPTURE_REPEAT)
    set(LOOKDEV_CAPTURE_REPEAT ON)
endif()

set(_gtest_filter "--gtest_filter=LookdevCapture.RendersFourStatesAtFourAnglesDeterministically")
set(_states midday late-afternoon overcast night-service)
if(DEFINED LOOKDEV_CAPTURE_STATES AND NOT "${LOOKDEV_CAPTURE_STATES}" STREQUAL "")
    set(_states ${LOOKDEV_CAPTURE_STATES})
endif()
if(DEFINED ENV{VIXEN_LOOKDEV_CAPTURE_STATE} AND NOT "$ENV{VIXEN_LOOKDEV_CAPTURE_STATE}" STREQUAL "")
    set(_states "$ENV{VIXEN_LOOKDEV_CAPTURE_STATE}")
endif()
foreach(_state IN LISTS _states)
    set(_first_dir "${LOOKDEV_CAPTURE_DIR}/${_state}")
    set(_repeat_dir "${LOOKDEV_CAPTURE_DIR}/.repeat/${_state}")
    set(_run_logs)
    set(_run_dirs "${_first_dir}")
    if(LOOKDEV_CAPTURE_REPEAT)
        list(APPEND _run_dirs "${_repeat_dir}")
    endif()
    foreach(_run IN LISTS _run_dirs)
        set(ENV{VIXEN_LOOKDEV_CAPTURE_STATE} "${_state}")
        set(ENV{VIXEN_LOOKDEV_CAPTURE_DIR} "${_run}")
        string(TIMESTAMP _run_start "%s")
        # ECHO_*: the process output streams through this script to CTest while it runs, so
        # `ctest -V` and the queue log show progress instead of one buffered dump at the end.
        execute_process(
            COMMAND "${LOOKDEV_CAPTURE_EXE}" "${_gtest_filter}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _stdout
            ERROR_VARIABLE _stderr
            ECHO_OUTPUT_VARIABLE
            ECHO_ERROR_VARIABLE)
        if(NOT _result STREQUAL "0")
            message(FATAL_ERROR "Look-dev ${_state} capture failed (${_result})\n${_stdout}\n${_stderr}")
        endif()
        string(TIMESTAMP _run_end "%s")
        math(EXPR _run_s "${_run_end} - ${_run_start}")
        string(REGEX MATCHALL "\\[CAPTURE_TIMING\\][^\n]*" _timing_lines "${_stdout}")
        string(REPLACE ";" "\n    " _timing_text "${_timing_lines}")
        message(STATUS "Look-dev ${_state} -> ${_run}: wall ${_run_s} s\n    ${_timing_text}")
        string(REGEX MATCHALL "\\[HDRMeter\\][^\n]*" _meter_lines "${_stdout}")
        list(APPEND _run_logs "${_meter_lines}")
    endforeach()

    foreach(_angle RANGE 0 3)
        if(NOT LOOKDEV_CAPTURE_REPEAT)
            if(NOT EXISTS "${_first_dir}/${_state}-angle-${_angle}.png")
                message(FATAL_ERROR "Missing single-pass capture: ${_first_dir}/${_state}-angle-${_angle}.png (the capture exe exited 0 without writing it; check VIXEN_LOOKDEV_CAPTURE_DIR handling)")
            endif()
            continue()
        endif()
        set(_filename "${_state}-angle-${_angle}.png")
        set(_first "${_first_dir}/${_filename}")
        set(_repeat "${_repeat_dir}/${_filename}")
        if(NOT EXISTS "${_first}" OR NOT EXISTS "${_repeat}")
            message(FATAL_ERROR "Missing deterministic capture: ${_first} or ${_repeat}")
        endif()
        file(SHA256 "${_first}" _first_hash)
        file(SHA256 "${_repeat}" _repeat_hash)
        if(NOT _first_hash STREQUAL _repeat_hash)
            message(FATAL_ERROR "Repeated capture pixels differ for ${_filename}\nFirst run:\n${_run_logs}")
        endif()
    endforeach()
endforeach()

string(TIMESTAMP _suite_end "%s")
math(EXPR _suite_s "${_suite_end} - ${_suite_start}")
if(_suite_s GREATER LOOKDEV_CAPTURE_BUDGET_S)
    message(FATAL_ERROR "Look-dev capture exceeded its runtime budget: ${_suite_s} s > ${LOOKDEV_CAPTURE_BUDGET_S} s (-DLOOKDEV_CAPTURE_BUDGET_S)")
endif()
message(STATUS "Look-dev capture suite wall ${_suite_s} s (budget ${LOOKDEV_CAPTURE_BUDGET_S} s)")

list(LENGTH _states _state_count)
math(EXPR _capture_count "${_state_count} * 4")
if(LOOKDEV_CAPTURE_REPEAT)
    message(STATUS "All ${_capture_count} selected look-dev captures are byte-identical across fresh per-state process runs")
else()
    message(STATUS "${_capture_count} selected look-dev captures written once (single-pass artifact mode; repeat-pixel witness NOT run)")
endif()
