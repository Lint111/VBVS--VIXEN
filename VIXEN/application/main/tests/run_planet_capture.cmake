if(NOT DEFINED PLANET_CAPTURE_EXE OR NOT DEFINED PLANET_CAPTURE_DIR)
    message(FATAL_ERROR "PLANET_CAPTURE_EXE and PLANET_CAPTURE_DIR are required")
endif()

string(TIMESTAMP _suite_start "%s")
set(_gtest_filter "--gtest_filter=PlanetCapture.RendersStateDeterministically")
# The registered per-state tests pass -DPLANET_CAPTURE_STATES=<state>; the native-only aggregate
# passes nothing and runs every state. VIXEN_PLANET_CAPTURE_STATE (comma list) narrows the aggregate.
set(_states C1 C1-epic C2 C2-term C4a C4b C4c C5a-moon C7 C7-60 C7-60-1p5au C9)
if(DEFINED PLANET_CAPTURE_STATES AND NOT "${PLANET_CAPTURE_STATES}" STREQUAL "")
    string(REPLACE "," ";" _states "${PLANET_CAPTURE_STATES}")
elseif(DEFINED ENV{VIXEN_PLANET_CAPTURE_STATE} AND NOT "$ENV{VIXEN_PLANET_CAPTURE_STATE}" STREQUAL "")
    string(REPLACE "," ";" _states "$ENV{VIXEN_PLANET_CAPTURE_STATE}")
endif()
foreach(_state IN LISTS _states)
    set(_first_dir "${PLANET_CAPTURE_DIR}")
    set(_repeat_dir "${PLANET_CAPTURE_DIR}/.repeat")
    set(_run_logs)
    foreach(_run IN ITEMS "${_first_dir}" "${_repeat_dir}")
        set(ENV{VIXEN_PLANET_CAPTURE_STATE} "${_state}")
        set(ENV{VIXEN_PLANET_CAPTURE_DIR} "${_run}")
        set(ENV{VIXEN_PLANET_READBACK} "1")
        # Dozen loses the device under APU contention (D3D12 TDR); a lost process is retried, and the
        # byte comparison below still decides determinism.
        foreach(_attempt RANGE 1 3)
            message(STATUS "Planet ${_state} -> ${_run}: attempt ${_attempt} started")
            string(TIMESTAMP _run_start "%s")
            # ECHO_*: the process output streams through this script to CTest while it runs, so
            # `ctest -V` and the queue log show progress instead of one buffered dump at the end.
            execute_process(
                COMMAND "${PLANET_CAPTURE_EXE}" "${_gtest_filter}"
                RESULT_VARIABLE _result
                OUTPUT_VARIABLE _stdout
                ERROR_VARIABLE _stderr
                ECHO_OUTPUT_VARIABLE
                ECHO_ERROR_VARIABLE)
            string(TIMESTAMP _run_end "%s")
            math(EXPR _run_s "${_run_end} - ${_run_start}")
            if(_result STREQUAL "0")
                break()
            endif()
            message(STATUS "Planet ${_state} capture attempt ${_attempt} exited ${_result} after ${_run_s} s")
        endforeach()
        if(NOT _result STREQUAL "0")
            message(FATAL_ERROR "Planet ${_state} capture failed (${_result})\n${_stdout}\n${_stderr}")
        endif()
        string(REGEX MATCHALL "\\[PLANET_CAPTURE_TIMING\\][^\n]*" _timing_lines "${_stdout}")
        string(REPLACE ";" "\n    " _timing_text "${_timing_lines}")
        message(STATUS "Planet ${_state} -> ${_run}: wall ${_run_s} s\n    ${_timing_text}")
        string(REGEX MATCHALL "\\[HDRMeter\\][^\n]*" _meter_lines "${_stdout}")
        list(APPEND _run_logs "${_meter_lines}")
    endforeach()

    foreach(_suffix IN ITEMS ".png" ".hits.bin" ".lighting.bin" ".meter.bin" ".scene.json")
        set(_first "${_first_dir}/${_state}${_suffix}")
        set(_repeat "${_repeat_dir}/${_state}${_suffix}")
        if(NOT EXISTS "${_first}" OR NOT EXISTS "${_repeat}")
            message(FATAL_ERROR "Missing deterministic capture: ${_first} or ${_repeat}")
        endif()
        file(SHA256 "${_first}" _first_hash)
        file(SHA256 "${_repeat}" _repeat_hash)
        if(NOT _first_hash STREQUAL _repeat_hash)
            message(FATAL_ERROR "Repeated capture differs for ${_state}${_suffix}\nFirst run:\n${_run_logs}")
        endif()
    endforeach()
    message(STATUS "Planet ${_state}: first and repeat captures are byte-identical")
endforeach()

string(TIMESTAMP _suite_end "%s")
math(EXPR _suite_s "${_suite_end} - ${_suite_start}")
list(LENGTH _states _state_count)
message(STATUS "All ${_state_count} selected planet states are byte-identical across fresh per-state process runs (wall ${_suite_s} s)")
