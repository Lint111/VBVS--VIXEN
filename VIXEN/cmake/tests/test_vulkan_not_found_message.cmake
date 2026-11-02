cmake_minimum_required(VERSION 3.21)
# T-4879: with auto-provision OFF and a private cache dir the configure must fail with a message that
# names the inputs (what/where), why the cache was ignored, and the fix, instead of the old generic hint.
# VIXEN_SMOKE_ROOT: scratch dir (also stands in for XDG_CACHE_HOME so no shared SDK is visible).
if(NOT VIXEN_SMOKE_ROOT)
    message(FATAL_ERROR "pass -DVIXEN_SMOKE_ROOT=<scratch dir>")
endif()
get_filename_component(_cmake_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(REMOVE_RECURSE "${VIXEN_SMOKE_ROOT}")
file(MAKE_DIRECTORY "${VIXEN_SMOKE_ROOT}/src")
file(WRITE "${VIXEN_SMOKE_ROOT}/src/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.21)
project(vulkan_not_found LANGUAGES NONE)
set(VIXEN_ROOT \"\${CMAKE_SOURCE_DIR}\")
include(\"${_cmake_dir}/ProvisionVulkan.cmake\")
")

function(configure_smoke _label)
    set(_xdg "${VIXEN_SMOKE_ROOT}/xdg-${_label}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=VULKAN_SDK "XDG_CACHE_HOME=${_xdg}"
            "${CMAKE_COMMAND}" -S "${VIXEN_SMOKE_ROOT}/src" -B "${VIXEN_SMOKE_ROOT}/b-${_label}"
            -DVIXEN_AUTO_PROVISION_VULKAN=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON
            "-DVIXEN_VULKAN_CACHE_DIR=${VIXEN_SMOKE_ROOT}/private-cache"
        RESULT_VARIABLE _rc ERROR_VARIABLE _err OUTPUT_QUIET)
    set(_rc "${_rc}" PARENT_SCOPE)
    set(_err "${_err}" PARENT_SCOPE)
endfunction()

function(expect_in _text _needle)
    string(FIND "${_text}" "${_needle}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "expected '${_needle}' in configure error:\n${_text}")
    endif()
endfunction()

configure_smoke(none)
if(_rc EQUAL 0)
    message(FATAL_ERROR "configure unexpectedly succeeded without any Vulkan SDK")
endif()
expect_in("${_err}" "VIXEN_AUTO_PROVISION_VULKAN=OFF")
expect_in("${_err}" "private-cache")
expect_in("${_err}" "env VULKAN_SDK=<unset>")
expect_in("${_err}" "auto-provisioning is OFF so the cache dir is never consulted")
expect_in("${_err}" "AUTO_PROVISION_VULKAN=ON and network access")
