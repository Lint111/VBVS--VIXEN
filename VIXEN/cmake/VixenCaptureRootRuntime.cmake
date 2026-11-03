# VixenCaptureRootRuntime.cmake
#
# Run-time half of the capture-root contract (T-6564). Included by -P scripts only; it needs no
# configure state. CTest registrations bake build-tree capture paths; at run time the environment
# variable VIXEN_CAPTURE_ROOT (exported by the box queue per GPU job, or set by hand for a persistent
# evidence directory) replaces the baked default root.

# Resolves the root a capture should land under: VIXEN_CAPTURE_ROOT when set, else the baked default.
function(vixen_capture_runtime_root default_root output_variable)
    set(_root "${default_root}")
    if(DEFINED ENV{VIXEN_CAPTURE_ROOT} AND NOT "$ENV{VIXEN_CAPTURE_ROOT}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{VIXEN_CAPTURE_ROOT}" _root)
    endif()
    set(${output_variable} "${_root}" PARENT_SCOPE)
endfunction()

# Moves a path that lives under the baked default root to the run-time root; other values pass through.
function(vixen_capture_runtime_rebase value default_root output_variable)
    vixen_capture_runtime_root("${default_root}" _root)
    set(_result "${value}")
    string(LENGTH "${default_root}" _default_length)
    if("${value}" STREQUAL "${default_root}")
        set(_result "${_root}")
    elseif(_default_length GREATER 0 AND "${value}" MATCHES "^${default_root}/")
        string(SUBSTRING "${value}" ${_default_length} -1 _tail)
        set(_result "${_root}${_tail}")
    endif()
    set(${output_variable} "${_result}" PARENT_SCOPE)
endfunction()
