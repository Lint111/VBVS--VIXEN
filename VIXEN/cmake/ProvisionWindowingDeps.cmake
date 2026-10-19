# ProvisionWindowingDeps.cmake
#
# Self-contained windowing build-deps. GLFW needs a platform backend: Win32 on Windows and Cocoa on
# macOS need NOTHING extra, but on Linux its X11/Wayland backends need the X11 dev libraries. So a
# consumer can only-ever-build on Linux too, this auto-provisions the X11 dev chain (no sudo, via
# apt-get download) into a machine-wide cache shared by every worktree (see VixenSharedCache.cmake),
# keyed by the package set and distro release, and points CMake's find_package(X11) at it, then
# enables GLFW's X11 backend. Runtime display is whatever the system provides (a real X
# server, or WSLg on WSL).
#
# Precedence (mirrors ProvisionVulkan): a system X11 dev install wins; else the cache; else download.
# If nothing is available and auto-provision is off, GLFW falls back to the null backend (compiles,
# no window) so the build never breaks.
#
# Knobs:
#   -DVIXEN_AUTO_PROVISION_WINDOWING=OFF   don't auto-download; use system X11 or the null backend
#   -DVIXEN_WINDOWING_CACHE_DIR=<path>     the one dir (with usr/ inside) holding the provisioned X11 dev libs
#                                          (default ${XDG_CACHE_HOME:-~/.cache}/vixen/windowing/<key>, shared by
#                                          all worktrees; a dedicated dir the provisioner owns)
#   build target  vixen-uninstall-windowing   remove this cache entry

include_guard(GLOBAL)

# Only Linux needs this. Windows (Win32) and macOS (Cocoa) provide the backend natively.
if(NOT (UNIX AND NOT APPLE))
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/VixenSharedCache.cmake")

option(VIXEN_AUTO_PROVISION_WINDOWING "Auto-provision X11 dev libs for GLFW on Linux when absent" ON)
set(_arch "x86_64-linux-gnu")

# The package set is the single source for both the download and the cache key, so changing it
# selects a fresh entry instead of reusing one that lacks the new packages. The .debs also differ per
# distro release, so ID and VERSION_ID are part of the key.
set(_vixen_windowing_packages
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
    libxext-dev libxfixes-dev libxrender-dev libxau-dev libxdmcp-dev libxcb1-dev x11proto-dev
    libxcb-keysyms1-dev
    libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6 libxext6 libxfixes3 libxrender1
    libxau6 libxdmcp6 libxcb1 libxcb-keysyms1)
set(_vixen_windowing_distro "")
if(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" _vixen_os_release REGEX "^(ID|VERSION_ID)=")
    string(REPLACE ";" "," _vixen_windowing_distro "${_vixen_os_release}")
endif()
string(SHA256 _vixen_windowing_key
    "${_arch}|${CMAKE_HOST_SYSTEM_PROCESSOR}|${_vixen_windowing_distro}|${_vixen_windowing_packages}")
string(SUBSTRING "${_vixen_windowing_key}" 0 12 _vixen_windowing_key)
vixen_shared_cache_root(_vixen_shared_cache_root)
set(_vixen_windowing_cache_default "${_vixen_shared_cache_root}/windowing/${_vixen_windowing_key}")

# A build tree still carrying the old in-tree default, or an earlier computed default whose key has
# since changed, moves to the current shared entry; an explicit -DVIXEN_WINDOWING_CACHE_DIR is kept.
# The old <src>/.windowing-deps dir is left alone (remove it by hand).
if(DEFINED CACHE{VIXEN_WINDOWING_CACHE_DIR} AND
   ("${VIXEN_WINDOWING_CACHE_DIR}" STREQUAL "${VIXEN_ROOT}/.windowing-deps" OR
    "${VIXEN_WINDOWING_CACHE_DIR}" STREQUAL "${VIXEN_WINDOWING_CACHE_DEFAULT_DIR}"))
    if(NOT "${VIXEN_WINDOWING_CACHE_DIR}" STREQUAL "${_vixen_windowing_cache_default}")
        get_cmake_property(_vixen_cache_vars CACHE_VARIABLES)
        foreach(_vixen_cache_var IN LISTS _vixen_cache_vars)
            get_property(_vixen_cache_val CACHE "${_vixen_cache_var}" PROPERTY VALUE)
            string(FIND "${_vixen_cache_val}" "${VIXEN_WINDOWING_CACHE_DIR}/" _vixen_cache_pos)
            if(_vixen_cache_pos EQUAL 0)
                unset("${_vixen_cache_var}" CACHE)
            endif()
        endforeach()
        unset(_vixen_cache_vars)
        unset(_vixen_cache_var)
        unset(_vixen_cache_val)
        unset(_vixen_cache_pos)
        message(STATUS "VIXEN: moved this build tree's X11 dev cache to ${_vixen_windowing_cache_default}.")
        set(VIXEN_WINDOWING_CACHE_DIR "${_vixen_windowing_cache_default}" CACHE PATH
            "Provisioned X11 dev cache" FORCE)
    endif()
endif()
set(VIXEN_WINDOWING_CACHE_DIR "${_vixen_windowing_cache_default}" CACHE PATH "Provisioned X11 dev cache")
set(VIXEN_WINDOWING_CACHE_DEFAULT_DIR "${_vixen_windowing_cache_default}" CACHE INTERNAL
    "Last computed default for VIXEN_WINDOWING_CACHE_DIR")

# GLFW's Linux build defaults the Wayland backend ON, and that backend needs wayland-scanner at
# configure time. We provision the X11 backend (below), not Wayland — so on a fresh tree where the
# X11 branch fires directly (no prior null-backend configure to cache it OFF), GLFW would still try
# to build Wayland and fail on the missing wayland-scanner. Disable the Wayland backend up front when
# wayland-scanner is absent (build it can't run). If it IS present, leave GLFW's default so a
# Wayland-capable host still gets that backend. This keeps the X11 path self-consistent whether VIXEN
# is built standalone or add_subdirectory'd into a super-build.
find_program(_vixen_wayland_scanner wayland-scanner)
if(NOT _vixen_wayland_scanner)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    message(STATUS "VIXEN: wayland-scanner not found; GLFW Wayland backend disabled (X11 only).")
endif()

# 1) System X11 dev present? Just enable the X11 backend and let GLFW find it normally.
find_path(_vixen_x11_sys NAMES X11/Xlib.h PATHS /usr/include)
if(_vixen_x11_sys)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    message(STATUS "VIXEN: found system X11 dev (${_vixen_x11_sys}); GLFW X11 backend enabled.")
    return()
endif()

set(_vk_cache_hdr "${VIXEN_WINDOWING_CACHE_DIR}/usr/include/X11/Xlib.h")
set(_vk_cache_keysyms "${VIXEN_WINDOWING_CACHE_DIR}/usr/lib/${_arch}/libxcb-keysyms.so.1")

# 2) Not cached yet, or an unstamped/partial entry? Auto-provision via apt-get download
#    (Debian/Ubuntu; other distros: install the -dev packages yourself, or this silently leaves the
#    null backend). An entry is usable only with the stamp, written after a complete extract right
#    before the atomic rename into place, plus the files below.
set(_vixen_windowing_stamp "${VIXEN_WINDOWING_CACHE_DIR}/${VIXEN_SHARED_CACHE_STAMP}")
set(_vixen_windowing_ok FALSE)
if(EXISTS "${_vixen_windowing_stamp}" AND EXISTS "${_vk_cache_hdr}" AND EXISTS "${_vk_cache_keysyms}")
    set(_vixen_windowing_ok TRUE)
endif()

if(NOT _vixen_windowing_ok AND VIXEN_AUTO_PROVISION_WINDOWING)
    find_program(_vixen_apt apt-get)
    find_program(_vixen_dpkg dpkg-deb)
    if(_vixen_apt AND _vixen_dpkg)
        get_filename_component(_vixen_windowing_parent "${VIXEN_WINDOWING_CACHE_DIR}" DIRECTORY)
        get_filename_component(_vixen_windowing_leaf "${VIXEN_WINDOWING_CACHE_DIR}" NAME)
        file(MAKE_DIRECTORY "${_vixen_windowing_parent}")
        # Several worktrees may configure at once: one populates, the others wait here and then reuse.
        file(LOCK "${VIXEN_WINDOWING_CACHE_DIR}.lock" GUARD PROCESS TIMEOUT 1800 RESULT_VARIABLE _vixen_windowing_lock_rc)
        if(NOT _vixen_windowing_lock_rc EQUAL 0)
            message(FATAL_ERROR "VIXEN: could not lock the X11 dev cache entry ${VIXEN_WINDOWING_CACHE_DIR}: ${_vixen_windowing_lock_rc}")
        endif()
        if(EXISTS "${_vixen_windowing_stamp}" AND EXISTS "${_vk_cache_hdr}" AND EXISTS "${_vk_cache_keysyms}")
            set(_vixen_windowing_ok TRUE)  # another configure finished it while we waited
        else()
            message(STATUS "VIXEN: X11 dev not found — provisioning (no sudo) into ${VIXEN_WINDOWING_CACHE_DIR} ...")
            # We hold this entry's lock, so any ".tmp-<leaf>-*" dir is a dead populate attempt.
            file(GLOB _vixen_windowing_stale "${_vixen_windowing_parent}/.tmp-${_vixen_windowing_leaf}-*")
            foreach(_vixen_windowing_old IN LISTS _vixen_windowing_stale)
                file(REMOVE_RECURSE "${_vixen_windowing_old}")
            endforeach()
            string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" _vixen_windowing_suffix)
            set(_vixen_windowing_tmp "${_vixen_windowing_parent}/.tmp-${_vixen_windowing_leaf}-${_vixen_windowing_suffix}")
            file(MAKE_DIRECTORY "${_vixen_windowing_tmp}/_dl")
            execute_process(
                COMMAND ${_vixen_apt} download ${_vixen_windowing_packages}
                WORKING_DIRECTORY "${_vixen_windowing_tmp}/_dl"
                RESULT_VARIABLE _vixen_dlrc OUTPUT_QUIET ERROR_QUIET)
            file(GLOB _vixen_debs "${_vixen_windowing_tmp}/_dl/*.deb")
            foreach(_deb ${_vixen_debs})
                execute_process(COMMAND ${_vixen_dpkg} -x "${_deb}" "${_vixen_windowing_tmp}" OUTPUT_QUIET ERROR_QUIET)
            endforeach()
            file(REMOVE_RECURSE "${_vixen_windowing_tmp}/_dl")
            if(EXISTS "${_vixen_windowing_tmp}/usr/include/X11/Xlib.h" AND
               EXISTS "${_vixen_windowing_tmp}/usr/lib/${_arch}/libxcb-keysyms.so.1")
                file(WRITE "${_vixen_windowing_tmp}/${VIXEN_SHARED_CACHE_STAMP}" "X11 dev ${_vixen_windowing_key}\n")
                # An entry without a valid stamp is a corrupt or partial leftover of this same key.
                file(REMOVE_RECURSE "${VIXEN_WINDOWING_CACHE_DIR}")
                file(RENAME "${_vixen_windowing_tmp}" "${VIXEN_WINDOWING_CACHE_DIR}")
                set(_vixen_windowing_ok TRUE)
            else()
                file(REMOVE_RECURSE "${_vixen_windowing_tmp}")
                message(STATUS "VIXEN: X11 dev download/extract was incomplete (apt-get download rc=${_vixen_dlrc}); nothing was cached.")
            endif()
        endif()
        file(LOCK "${VIXEN_WINDOWING_CACHE_DIR}.lock" RELEASE)
    else()
        message(STATUS "VIXEN: no apt-get/dpkg-deb — cannot auto-provision X11. Install libx11-dev "
                       "libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev, or accept the null backend.")
    endif()
endif()

# 3) Use the cache if we have it; else fall back to the null backend (build still succeeds).
if(_vixen_windowing_ok)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    # include() runs in this (dependencies) scope, where the GLFW FetchContent below also runs, so a
    # plain set here is what GLFW's find_package(X11) sees — no PARENT_SCOPE needed.
    list(PREPEND CMAKE_PREFIX_PATH "${VIXEN_WINDOWING_CACHE_DIR}/usr")
    if(NOT CMAKE_LIBRARY_ARCHITECTURE)
        set(CMAKE_LIBRARY_ARCHITECTURE "${_arch}")
    endif()
    message(STATUS "VIXEN: X11 dev provisioned at ${VIXEN_WINDOWING_CACHE_DIR}; GLFW X11 backend enabled.")
else()
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    message(STATUS "VIXEN: X11 dev unavailable — GLFW null backend (engine compiles, no window).")
endif()

unset(_vk_cache_hdr)
unset(_vk_cache_keysyms)

if(NOT TARGET vixen-uninstall-windowing)
    add_custom_target(vixen-uninstall-windowing
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIXEN_WINDOWING_CACHE_DIR}"
        COMMENT "VIXEN: removing this X11 dev cache entry (${VIXEN_WINDOWING_CACHE_DIR})")
endif()
