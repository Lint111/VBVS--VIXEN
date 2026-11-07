if(NOT DEFINED VIXEN_STAGE_TEST_ROOT)
    message(FATAL_ERROR "VIXEN_STAGE_TEST_ROOT must be inside the caller's build tree")
endif()

get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(source_root "${VIXEN_STAGE_TEST_ROOT}/source")
set(output_root "${VIXEN_STAGE_TEST_ROOT}/staged")
file(REMOVE_RECURSE "${VIXEN_STAGE_TEST_ROOT}")
file(MAKE_DIRECTORY "${source_root}")
file(WRITE "${source_root}/FrameTargets.cs" "namespace Test; public sealed class FrameTargets {}\n")
file(WRITE "${source_root}/lineage.json" "{\"version\":1}\n")

set(stage_command
    "${CMAKE_COMMAND}"
    "-DSOURCE_ROOT=${source_root}"
    "-DOUTPUT_ROOT=${output_root}"
    -P "${vixen_root}/codegen/StageGpuSchemaSource.cmake")
execute_process(COMMAND ${stage_command} RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output ERROR_VARIABLE first_error)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "initial schema staging failed: ${first_output}${first_error}")
endif()

file(TIMESTAMP "${output_root}/FrameTargets.cs" first_timestamp "%s")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
execute_process(COMMAND ${stage_command} RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output ERROR_VARIABLE second_error)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "repeat schema staging failed: ${second_output}${second_error}")
endif()
file(TIMESTAMP "${output_root}/FrameTargets.cs" second_timestamp "%s")
if(NOT first_timestamp STREQUAL second_timestamp)
    message(FATAL_ERROR
        "identical schema staging changed the destination timestamp: ${first_timestamp} -> ${second_timestamp}")
endif()

file(REMOVE "${source_root}/FrameTargets.cs")
execute_process(COMMAND ${stage_command} RESULT_VARIABLE third_result
    OUTPUT_VARIABLE third_output ERROR_VARIABLE third_error)
if(NOT third_result EQUAL 0 OR EXISTS "${output_root}/FrameTargets.cs")
    message(FATAL_ERROR "deleted schema source was not removed from the staged tree: ${third_output}${third_error}")
endif()
message(STATUS "GPU schema staging preserves identical mtimes and removes deleted sources")
