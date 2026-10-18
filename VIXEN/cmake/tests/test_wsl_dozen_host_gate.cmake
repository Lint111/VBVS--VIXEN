cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED VIXEN_DOZEN_GATE_ROOT)
    message(FATAL_ERROR "VIXEN_DOZEN_GATE_ROOT must be a scratch directory inside the caller's build tree")
endif()
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(cache "${VIXEN_DOZEN_GATE_ROOT}/vixen/wsl-vulkan")

if(VIXEN_DOZEN_GATE_HOST)
    set(ENV{XDG_CACHE_HOME} "${VIXEN_DOZEN_GATE_ROOT}")
    if(VIXEN_DOZEN_GATE_HOST STREQUAL "windows")
        set(CMAKE_HOST_UNIX FALSE)
    endif()
    include("${vixen_root}/cmake/ProvisionWslVulkan.cmake")
    message(STATUS "DZN_ICD=[${VIXEN_WSL_DZN_ICD}]")
    return()
endif()

# A complete provisioned cache, so a run that does attempt provisioning takes the cheap cache-hit
# branch and never builds Mesa.
file(MAKE_DIRECTORY "${cache}/mesa/build/src/microsoft/vulkan")
file(WRITE "${cache}/dzn_icd.json" "{}\n")
file(WRITE "${cache}/mesa/build/src/microsoft/vulkan/libvulkan_dzn.so" "")

function(run_gate host out_var)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DVIXEN_DOZEN_GATE_ROOT=${VIXEN_DOZEN_GATE_ROOT}" "-DVIXEN_DOZEN_GATE_HOST=${host}"
        -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${host} gate run failed (${result}): ${output}${error}")
    endif()
    set(${out_var} "${output}${error}" PARENT_SCOPE)
endfunction()

# A Windows configure resolves "/dev/dxg" against the current drive, which is the WSL filesystem
# when the worktree is reached through a mapped WSL share. The host, not that path probe, decides.
run_gate(windows windows_output)
if(NOT windows_output MATCHES "DZN_ICD=\\[\\]")
    message(FATAL_ERROR "a Windows host provisioned WSL Dozen: ${windows_output}")
endif()

# Control: a Unix host with /dev/dxg (WSL itself) still takes the provisioned-cache branch.
run_gate(linux linux_output)
if(EXISTS "/dev/dxg")
    if(NOT linux_output MATCHES "DZN_ICD=\\[${cache}/dzn_icd.json\\]")
        message(FATAL_ERROR "WSL no longer picks up the provisioned Dozen cache: ${linux_output}")
    endif()
elseif(NOT linux_output MATCHES "DZN_ICD=\\[\\]")
    message(FATAL_ERROR "a host without /dev/dxg provisioned WSL Dozen: ${linux_output}")
endif()
message(STATUS "WSL Dozen provisioning is gated on a Unix host as well as /dev/dxg")
