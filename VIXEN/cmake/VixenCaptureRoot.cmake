# VixenCaptureRoot.cmake
#
# One capture-root contract for look-dev, planet, cel and editor CTest registrations (T-6564).
#   configure time  VIXEN_CAPTURE_DEFAULT_ROOT (cache, default <build>/runtime-captures) is the root every
#                   registration bakes; it is the fallback outside the queue.
#   run time        VIXEN_CAPTURE_ROOT, when set, replaces that root. The box queue exports a retained
#                   per-run child of it for GPU jobs; set it by hand to keep captures in a stable directory.
#   legacy          Editor/HUD wrappers also accept UT_QUEUE_CAPTURE_ROOT when VIXEN_CAPTURE_ROOT is unset.
set(VIXEN_CAPTURE_DEFAULT_ROOT "${CMAKE_BINARY_DIR}/runtime-captures" CACHE PATH
    "Build-tree fallback root for CTest capture output; VIXEN_CAPTURE_ROOT in the test environment overrides it at run time")

# vixen_capture_command(<out> [REBASE <env var>...] [MKPARENT <env var>...] COMMAND <command> [args...])
# Sets <out> to an add_test COMMAND list that runs <command> through run_with_capture_root.cmake, moving
# the listed environment variables (those holding paths under VIXEN_CAPTURE_DEFAULT_ROOT) to
# VIXEN_CAPTURE_ROOT at run time. MKPARENT names variables (file or file-prefix paths) whose parent directory
# the wrapper creates first, because a moved root has not been created by configure.
function(vixen_capture_command output_variable)
    cmake_parse_arguments(PARSE_ARGV 1 VCC "" "" "REBASE;MKPARENT;COMMAND")
    if(NOT VCC_COMMAND)
        message(FATAL_ERROR "vixen_capture_command(${output_variable}) requires COMMAND")
    endif()
    string(JOIN "," _rebase_vars ${VCC_REBASE})
    string(JOIN "," _mkparent_vars ${VCC_MKPARENT})
    set(${output_variable}
        "${CMAKE_COMMAND}"
        "-DVIXEN_CAPTURE_DEFAULT_ROOT=${VIXEN_CAPTURE_DEFAULT_ROOT}"
        "-DVIXEN_CAPTURE_REBASE_VARS=${_rebase_vars}"
        "-DVIXEN_CAPTURE_MKPARENT_VARS=${_mkparent_vars}"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/run_with_capture_root.cmake"
        ${VCC_COMMAND}
        PARENT_SCOPE)
endfunction()
