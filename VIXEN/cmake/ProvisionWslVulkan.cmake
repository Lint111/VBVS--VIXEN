# ProvisionWslVulkan.cmake
#
# Auto-provision Mesa Dozen (Vulkan-over-D3D12) so VIXEN renders on the GPU under WSL2.
# No-op off WSL (no /dev/dxg) or when disabled. Provisioning failure is non-fatal, but the runtime
# selector remains strict: use an explicit VK_ICD_FILENAMES or VIXEN_ALLOW_SOFTWARE_VULKAN=1 for
# software Vulkan.
#
# Knobs:
#   -DVIXEN_AUTO_PROVISION_WSL_VULKAN=OFF   skip Dozen provisioning (requires an explicit runtime
#                                          ICD choice or software opt-in on WSL)

include_guard(GLOBAL)

option(VIXEN_AUTO_PROVISION_WSL_VULKAN "Build Mesa Dozen for GPU Vulkan on WSL2" ON)

set(VIXEN_WSL_DZN_ICD "" CACHE INTERNAL "Path to the provisioned Dozen ICD json (empty = none)")

if(VIXEN_AUTO_PROVISION_WSL_VULKAN AND EXISTS "/dev/dxg")
    if(DEFINED ENV{XDG_CACHE_HOME})
        set(_wsl_cache "$ENV{XDG_CACHE_HOME}/vixen/wsl-vulkan")
    else()
        set(_wsl_cache "$ENV{HOME}/.cache/vixen/wsl-vulkan")
    endif()
    set(_dzn_icd "${_wsl_cache}/dzn_icd.json")
    set(_dzn_so  "${_wsl_cache}/mesa/build/src/microsoft/vulkan/libvulkan_dzn.so")

    if(EXISTS "${_dzn_so}" AND EXISTS "${_dzn_icd}")
        message(STATUS "[ProvisionWslVulkan] cache hit: ${_dzn_so}")
        set(VIXEN_WSL_DZN_ICD "${_dzn_icd}" CACHE INTERNAL "" FORCE)
    else()
        message(STATUS "[ProvisionWslVulkan] WSL2 GPU detected; building Mesa Dozen (first time, "
                       "a few minutes) into ${_wsl_cache} ...")
        execute_process(
            COMMAND bash "${CMAKE_CURRENT_LIST_DIR}/provision-wsl-vulkan.sh" "${_wsl_cache}"
            RESULT_VARIABLE _dzn_rc)
        if(_dzn_rc EQUAL 0 AND EXISTS "${_dzn_icd}")
            message(STATUS "[ProvisionWslVulkan] Dozen ready: ${_dzn_icd}")
            set(VIXEN_WSL_DZN_ICD "${_dzn_icd}" CACHE INTERNAL "" FORCE)
        else()
            message(WARNING "[ProvisionWslVulkan] Dozen provision failed (rc=${_dzn_rc}); the "
                            "runtime selector will require VK_ICD_FILENAMES or "
                            "VIXEN_ALLOW_SOFTWARE_VULKAN=1. Re-run configure to retry, or set "
                            "VIXEN_AUTO_PROVISION_WSL_VULKAN=OFF.")
        endif()
    endif()
endif()
