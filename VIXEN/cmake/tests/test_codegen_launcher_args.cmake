cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../VixenChecks.cmake")

if(REJECT_PREFIX)
    set(_CODEGEN_RUNNER wsl.exe --cd / -e /home/test/dotnet)
    vixen_codegen_payload_arguments(_payload wrong-launcher /tmp/tool.dll)
    message(FATAL_ERROR "the invalid launcher prefix was accepted")
endif()

set(_CODEGEN_RUNNER wsl.exe --cd / -e /home/test/dotnet)
vixen_codegen_payload_arguments(_payload ${_CODEGEN_RUNNER}
    /home/test/tool.dll --schema "/home/test/source with spaces" --out-cpp /home/test/output.g.h --check)
set(_expected /home/test/tool.dll --schema "/home/test/source with spaces" --out-cpp /home/test/output.g.h --check)
if(NOT "${_payload}" STREQUAL "${_expected}")
    message(FATAL_ERROR "WSL launcher options leaked into the dependency inputs: ${_payload}")
endif()

# A root intentionally supplied to the tool remains a real payload argument.
vixen_codegen_payload_arguments(_payload ${_CODEGEN_RUNNER} /home/test/tool.dll --schema /)
set(_expected /home/test/tool.dll --schema /)
if(NOT "${_payload}" STREQUAL "${_expected}")
    message(FATAL_ERROR "a real payload root was removed")
endif()

set(_CODEGEN_RUNNER "C:/Program Files/dotnet/dotnet.exe")
vixen_codegen_payload_arguments(_payload ${_CODEGEN_RUNNER} C:/tool.dll --check)
if(NOT "${_payload}" STREQUAL "C:/tool.dll;--check")
    message(FATAL_ERROR "native launcher arguments were not separated")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -DREJECT_PREFIX=ON -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _rejected OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_rejected EQUAL 0 OR NOT _err MATCHES "does not start with the configured launcher")
    message(FATAL_ERROR "the invalid launcher-prefix check did not run: ${_err}")
endif()
message(STATUS "Codegen launcher/payload argument checks passed")
