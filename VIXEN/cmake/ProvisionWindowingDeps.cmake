# ProvisionWindowingDeps.cmake
#
# Self-contained windowing build-deps. GLFW needs a platform backend: Win32 on Windows and Cocoa on
# macOS need NOTHING extra, but on Linux its X11/Wayland backends need the X11 dev libraries. So a
# consumer can only-ever-build on Linux too, this auto-provisions the X11 dev chain (no sudo, via
# apt-get download) into a project-local, gitignored cache and points CMake's find_package(X11) at
# it, then enables GLFW's X11 backend. Runtime display is whatever the system provides (a real X
# server, or WSLg on WSL).
#
# Precedence (mirrors ProvisionVulkan): a system X11 dev install wins; else the cache; else download.
# If nothing is available and auto-provision is off, GLFW falls back to the null backend (compiles,
# no window) so the build never breaks.
#
# Knobs:
#   -DVIXEN_AUTO_PROVISION_WINDOWING=OFF   don't auto-download; use system X11 or the null backend
#   -DVIXEN_WINDOWING_CACHE_DIR=<path>     where the provisioned X11 dev libs live
#   build target  vixen-uninstall-windowing   remove the cache

include_guard(GLOBAL)

# Only Linux needs this. Windows (Win32) and macOS (Cocoa) provide the backend natively.
if(NOT (UNIX AND NOT APPLE))
    return()
endif()

option(VIXEN_AUTO_PROVISION_WINDOWING "Auto-provision X11 dev libs for GLFW on Linux when absent" ON)
set(VIXEN_WINDOWING_CACHE_DIR "${CMAKE_SOURCE_DIR}/.windowing-deps" CACHE PATH "Provisioned X11 dev cache")
set(_arch "x86_64-linux-gnu")

# 1) System X11 dev present? Just enable the X11 backend and let GLFW find it normally.
find_path(_vixen_x11_sys NAMES X11/Xlib.h PATHS /usr/include)
if(_vixen_x11_sys)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    message(STATUS "VIXEN: found system X11 dev (${_vixen_x11_sys}); GLFW X11 backend enabled.")
    return()
endif()

set(_vk_cache_hdr "${VIXEN_WINDOWING_CACHE_DIR}/usr/include/X11/Xlib.h")

# 2) Not cached yet? auto-provision via apt-get download (Debian/Ubuntu; other distros: install the
#    -dev packages yourself, or this silently leaves the null backend).
if(NOT EXISTS "${_vk_cache_hdr}" AND VIXEN_AUTO_PROVISION_WINDOWING)
    find_program(_vixen_apt apt-get)
    find_program(_vixen_dpkg dpkg-deb)
    if(_vixen_apt AND _vixen_dpkg)
        message(STATUS "VIXEN: X11 dev not found — provisioning (no sudo) into ${VIXEN_WINDOWING_CACHE_DIR} ...")
        file(MAKE_DIRECTORY "${VIXEN_WINDOWING_CACHE_DIR}/_dl")
        execute_process(
            COMMAND ${_vixen_apt} download
                libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
                libxext-dev libxfixes-dev libxrender-dev libxau-dev libxdmcp-dev libxcb1-dev x11proto-dev
                libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6 libxext6 libxfixes3 libxrender1
                libxau6 libxdmcp6 libxcb1
            WORKING_DIRECTORY "${VIXEN_WINDOWING_CACHE_DIR}/_dl"
            RESULT_VARIABLE _vixen_dlrc OUTPUT_QUIET ERROR_QUIET)
        file(GLOB _vixen_debs "${VIXEN_WINDOWING_CACHE_DIR}/_dl/*.deb")
        foreach(_deb ${_vixen_debs})
            execute_process(COMMAND ${_vixen_dpkg} -x "${_deb}" "${VIXEN_WINDOWING_CACHE_DIR}" OUTPUT_QUIET ERROR_QUIET)
        endforeach()
    else()
        message(STATUS "VIXEN: no apt-get/dpkg-deb — cannot auto-provision X11. Install libx11-dev "
                       "libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev, or accept the null backend.")
    endif()
endif()

# 3) Use the cache if we have it; else fall back to the null backend (build still succeeds).
if(EXISTS "${_vk_cache_hdr}")
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

if(NOT TARGET vixen-uninstall-windowing)
    add_custom_target(vixen-uninstall-windowing
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIXEN_WINDOWING_CACHE_DIR}"
        COMMENT "VIXEN: removing provisioned X11 dev cache (${VIXEN_WINDOWING_CACHE_DIR})")
endif()
