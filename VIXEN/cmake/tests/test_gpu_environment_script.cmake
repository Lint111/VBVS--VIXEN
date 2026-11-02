cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../VixenTesting.cmake")

# The script exports every KEY=VALUE entry CTest gives GPU tests, shell-quoted (T-6422).
set(_script "${CMAKE_CURRENT_BINARY_DIR}/gpu_environment_script/vixen-gpu-env.sh")
vixen_write_gpu_environment_script("${_script}" "VK_ICD_FILENAMES=/a b/dzn_icd.json" "LD_LIBRARY_PATH=/x:/y" "ODD=it's")
execute_process(COMMAND sh -c ". '${_script}' && printf '%s|%s|%s' \"$VK_ICD_FILENAMES\" \"$LD_LIBRARY_PATH\" \"$ODD\""
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out)
if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "/a b/dzn_icd.json|/x:/y|it's")
    message(FATAL_ERROR "gpu environment script: rc=${_rc} output='${_out}'")
endif()
message(STATUS "gpu environment script checks passed")
