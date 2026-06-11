# ProvisionVulkan.cmake
#
# Self-contained Vulkan provisioning. A consumer should only ever build VIXEN — never install the
# Vulkan SDK by hand. When no SDK is found, this downloads the prebuilt LunarG SDK (headers +
# loader + validation layers) into a persistent, gitignored cache and synthesises Vulkan::Vulkan,
# so everything downstream (find_package(Vulkan), Vulkan::Vulkan) works unchanged.
#
# Validation layers are gated by build type (Debug = on, Release = off) and surfaced as the
# VIXEN_VULKAN_VALIDATION compile symbol + a clear status line, so the active mode is obvious in
# the build output and to runtime code.
#
# Knobs:
#   -DVIXEN_AUTO_PROVISION_VULKAN=OFF   require a system SDK instead of auto-downloading
#   -DVIXEN_VULKAN_SDK_VERSION=X.Y.Z.W  pin the SDK version to provision
#   -DVIXEN_VULKAN_CACHE_DIR=<path>     where the provisioned SDK lives (default <src>/.vulkan-sdk)
#   -DVIXEN_VULKAN_VALIDATION=ON|OFF    force validation on/off (default: from build type)
#   -DVIXEN_UNINSTALL_VULKAN=ON         remove the provisioned cache and stop (the uninstall flag)
#   build target  vixen-uninstall-vulkan   same, as an explicit build step

include_guard(GLOBAL)

option(VIXEN_AUTO_PROVISION_VULKAN "Download a prebuilt Vulkan SDK into a cache when none is found" ON)
set(VIXEN_VULKAN_SDK_VERSION "1.4.350.1" CACHE STRING "LunarG Vulkan SDK version to auto-provision")
set(VIXEN_VULKAN_CACHE_DIR "${CMAKE_SOURCE_DIR}/.vulkan-sdk" CACHE PATH "Persistent Vulkan provisioning cache")
option(VIXEN_UNINSTALL_VULKAN "Remove the provisioned Vulkan cache and stop" OFF)

# Build-type-gated validation (Debug or any multi-config generator => on; Release => off), overridable.
if(NOT DEFINED VIXEN_VULKAN_VALIDATION)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
        set(VIXEN_VULKAN_VALIDATION ON)
    else()
        set(VIXEN_VULKAN_VALIDATION OFF)
    endif()
endif()

# --- uninstall (flag form; the target form is defined at the bottom) ---
if(VIXEN_UNINSTALL_VULKAN)
    if(EXISTS "${VIXEN_VULKAN_CACHE_DIR}")
        file(REMOVE_RECURSE "${VIXEN_VULKAN_CACHE_DIR}")
        message(STATUS "VIXEN: removed provisioned Vulkan cache: ${VIXEN_VULKAN_CACHE_DIR}")
    endif()
    message(FATAL_ERROR "VIXEN: Vulkan uninstalled. Re-run cmake without -DVIXEN_UNINSTALL_VULKAN=ON.")
endif()

# --- locate, or auto-provision ---
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND AND VIXEN_AUTO_PROVISION_VULKAN)
    set(_vk_root "${VIXEN_VULKAN_CACHE_DIR}/${VIXEN_VULKAN_SDK_VERSION}/x86_64")

    if(NOT EXISTS "${_vk_root}/include/vulkan/vulkan.h")
        set(_tarball "${VIXEN_VULKAN_CACHE_DIR}/vulkansdk-linux-x86_64-${VIXEN_VULKAN_SDK_VERSION}.tar.xz")
        set(_url "https://sdk.lunarg.com/sdk/download/${VIXEN_VULKAN_SDK_VERSION}/linux/vulkansdk-linux-x86_64-${VIXEN_VULKAN_SDK_VERSION}.tar.xz")
        file(MAKE_DIRECTORY "${VIXEN_VULKAN_CACHE_DIR}")
        if(NOT EXISTS "${_tarball}")
            message(STATUS "VIXEN: Vulkan SDK not found — downloading prebuilt LunarG SDK ${VIXEN_VULKAN_SDK_VERSION} (~320 MB, one-time, cached) ...")
            file(DOWNLOAD "${_url}" "${_tarball}" SHOW_PROGRESS TLS_VERIFY ON STATUS _dl)
            list(GET _dl 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE "${_tarball}")
                message(FATAL_ERROR "VIXEN: Vulkan SDK download failed (${_dl}). URL: ${_url}")
            endif()
        endif()
        message(STATUS "VIXEN: extracting Vulkan SDK into ${VIXEN_VULKAN_CACHE_DIR} ...")
        file(ARCHIVE_EXTRACT INPUT "${_tarball}" DESTINATION "${VIXEN_VULKAN_CACHE_DIR}")
    endif()

    if(EXISTS "${_vk_root}/include/vulkan/vulkan.h")
        # Point FindVulkan at the cached SDK and re-discover so Vulkan::Vulkan + the SDK tools resolve.
        set(ENV{VULKAN_SDK} "${_vk_root}")
        set(Vulkan_INCLUDE_DIR "${_vk_root}/include" CACHE PATH "" FORCE)
        set(Vulkan_LIBRARY "${_vk_root}/lib/libvulkan.so" CACHE FILEPATH "" FORCE)
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            set(VIXEN_VULKAN_LAYER_PATH "${_vk_root}/share/vulkan/explicit_layer.d"
                CACHE PATH "Provisioned Vulkan validation-layer manifests" FORCE)
            message(STATUS "VIXEN: using auto-provisioned Vulkan SDK at ${_vk_root}")
        endif()
    endif()
endif()

if(NOT Vulkan_FOUND)
    message(FATAL_ERROR
        "VIXEN: Vulkan not found and could not be auto-provisioned.\n"
        "  - Ensure network access, or install the Vulkan SDK, or\n"
        "  - re-run with -DVIXEN_AUTO_PROVISION_VULKAN=ON (default).")
endif()

# --- clear, build-type-aware validation symbol + status ---
if(VIXEN_VULKAN_VALIDATION)
    add_compile_definitions(VIXEN_VULKAN_VALIDATION=1)
else()
    add_compile_definitions(VIXEN_VULKAN_VALIDATION=0)
endif()
message(STATUS "VIXEN: Vulkan validation layers: ${VIXEN_VULKAN_VALIDATION} "
        "(build type: ${CMAKE_BUILD_TYPE}; layer path: ${VIXEN_VULKAN_LAYER_PATH})")

# --- uninstall as an explicit build step ---
if(NOT TARGET vixen-uninstall-vulkan)
    add_custom_target(vixen-uninstall-vulkan
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIXEN_VULKAN_CACHE_DIR}"
        COMMENT "VIXEN: removing provisioned Vulkan cache (${VIXEN_VULKAN_CACHE_DIR})")
endif()
