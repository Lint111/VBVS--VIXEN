# Native device-loss recovery producer (T-4238). Linux registers the same contract through bash;
# this script lets a Windows CTest COMMAND run it without lane-local glue.
# Usage: cmake -DAPP_EXE=<VIXEN> -DLOG=<log> -DWORKDIR=<dir> -P run_device_loss_recovery.cmake
# VIXEN_SIMULATE_DEVICE_LOSS / VIXEN_EXIT_AFTER_FRAMES come from the test's ENVIRONMENT property.
foreach(_var APP_EXE LOG WORKDIR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "run_device_loss_recovery.cmake: -D${_var}= is required. "
            "It is passed by the vixen_device_loss_recovery test registration in application/editor/CMakeLists.txt; reconfigure the build.")
    endif()
endforeach()
get_filename_component(_log_dir "${LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${_log_dir}")
execute_process(
    COMMAND "${APP_EXE}"
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_FILE "${LOG}"
    ERROR_FILE "${LOG}"
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "vixen_device_loss_recovery: ${APP_EXE} exited with ${_result}; log: ${LOG}. "
        "The simulated device loss must be recovered in-process; read the log tail for the failing frame.")
endif()
file(READ "${LOG}" _contents)
if(NOT _contents MATCHES "RECOVERY COMPLETE")
    message(FATAL_ERROR "vixen_device_loss_recovery: no 'RECOVERY COMPLETE' line in ${LOG}. "
        "The app exited 0 without finishing recovery; check VIXEN_SIMULATE_DEVICE_LOSS reached the app (test ENVIRONMENT) and the log tail.")
endif()
