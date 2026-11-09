# Cached merged-SDI drift witness.
# The first invocation runs the authoritative sdi_tool --check.  Later invocations compare a
# digest of the manifest/source closure and committed generated outputs and return immediately.

if(NOT DEFINED VIXEN_ROOT OR NOT DEFINED VIXEN_SDI_TOOL OR NOT DEFINED VIXEN_STAMP)
    message(FATAL_ERROR "check_sdi_merged_drift.cmake requires VIXEN_ROOT, VIXEN_SDI_TOOL and VIXEN_STAMP")
endif()

file(GLOB_RECURSE _inputs LIST_DIRECTORIES FALSE
    "${VIXEN_ROOT}/shaders/*.comp"
    "${VIXEN_ROOT}/shaders/*.glsl"
    "${VIXEN_ROOT}/shaders/*.h"
    "${VIXEN_ROOT}/shaders/sdi-variants.json"
    "${VIXEN_ROOT}/libraries/SVO/shaders/*.comp"
    "${VIXEN_ROOT}/libraries/SVO/shaders/*.glsl"
    "${VIXEN_ROOT}/libraries/SVO/shaders/*.h")
list(SORT _inputs)
set(_input_fingerprint)
foreach(_path IN LISTS _inputs)
    file(SHA256 "${_path}" _hash)
    string(APPEND _input_fingerprint "${_path}\t${_hash}\n")
endforeach()

file(GLOB _outputs LIST_DIRECTORIES FALSE "${VIXEN_ROOT}/generated/sdi/merged/*-SDI.g.h")
list(SORT _outputs)
set(_output_fingerprint)
foreach(_path IN LISTS _outputs)
    file(SHA256 "${_path}" _hash)
    string(APPEND _output_fingerprint "${_path}\t${_hash}\n")
endforeach()

string(SHA256 _key "${_input_fingerprint}\n${_output_fingerprint}")
if(EXISTS "${VIXEN_STAMP}")
    file(READ "${VIXEN_STAMP}" _stored)
    string(STRIP "${_stored}" _stored)
    if(_stored STREQUAL _key)
        message(STATUS "sdi_merged_drift_check: cached (input/output closure unchanged)")
        return()
    endif()
endif()

execute_process(
    COMMAND "${VIXEN_SDI_TOOL}" merge-variants "${VIXEN_ROOT}/shaders/sdi-variants.json" --check
    WORKING_DIRECTORY "${VIXEN_ROOT}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "sdi_merged_drift_check failed (sdi_tool exit ${_result})")
endif()
file(WRITE "${VIXEN_STAMP}" "${_key}\n")
message(STATUS "sdi_merged_drift_check: verified and cached")
