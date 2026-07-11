# ProvisionCcache.cmake
#
# Self-contained ccache provisioning for Windows. ccache is preferred over sccache for this
# project specifically because MSVC precompiled headers (/Yc, /Yu, /Fp — used by
# target_precompile_headers() on most of VIXEN's core libraries) are UNCONDITIONALLY
# uncacheable under sccache (sccache routes /Fp and /Yc as TooHardPath in its own source; open
# upstream issue mozilla/sccache#978, unresolved since 2021). ccache added real MSVC PCH
# support in 2024 (refined through 2025 point releases) and is the documented fix other CMake
# projects use for this exact gap. See CMakeLists.txt's USE_CCACHE block for the measured
# impact (38.9% sccache hit rate on a fresh worktree, /Fp as the largest non-cacheable bucket).
#
# A consumer should never need to hand-install ccache — when it's not found on PATH, this
# downloads the official prebuilt Windows binary (a portable, statically-linked ccache.exe with
# no bundled DLLs — extract and run, no installer) into a persistent, gitignored project-local
# cache, matching ProvisionVulkan.cmake's precedent (system tool wins; only if absent do we
# touch the cache; only if the cache is empty do we download).
#
# Windows-only: this project's Linux/WSL side doesn't use MSVC, so the PCH-uncacheable problem
# this file exists to solve doesn't apply there (sccache is fine for GCC/Clang PCH). Non-Windows
# platforms just skip this file entirely and CMakeLists.txt's existing sccache path is used.
#
# Knobs:
#   -DVIXEN_AUTO_PROVISION_CCACHE=OFF   require a system ccache instead of auto-downloading
#   -DVIXEN_CCACHE_VERSION=X.Y.Z        pin the ccache version to provision
#   -DVIXEN_CCACHE_CACHE_DIR=<path>     where the provisioned ccache lives (default <src>/.ccache-deps)
#   build target  vixen-uninstall-ccache   remove the provisioned cache

include_guard(GLOBAL)

# ccache's MSVC /Yc support (and this project's whole reason for preferring it) is Windows-only.
if(NOT WIN32)
    return()
endif()

option(VIXEN_AUTO_PROVISION_CCACHE "Download a prebuilt ccache into a cache when none is found (Windows only)" ON)
set(VIXEN_CCACHE_VERSION "4.13.6" CACHE STRING "ccache version to auto-provision")
set(VIXEN_CCACHE_CACHE_DIR "${VIXEN_ROOT}/.ccache-deps" CACHE PATH "Persistent ccache provisioning cache")

# --- locate, or auto-provision ---
# Precedence: an already-installed system ccache wins; only if absent do we touch the
# project-local cache; only if the cache is empty do we download. We never install ccache
# system-wide — provisioning is a project-local cache, same contract as ProvisionVulkan.cmake.
find_program(VIXEN_PROVISIONED_CCACHE_PROGRAM ccache)
if(VIXEN_PROVISIONED_CCACHE_PROGRAM)
    message(STATUS "VIXEN: found system ccache: ${VIXEN_PROVISIONED_CCACHE_PROGRAM} — skipping auto-provision.")
elseif(VIXEN_AUTO_PROVISION_CCACHE)
    set(_ccache_dir_name "ccache-${VIXEN_CCACHE_VERSION}-windows-x86_64")
    set(_ccache_root "${VIXEN_CCACHE_CACHE_DIR}/${_ccache_dir_name}")
    set(_ccache_exe "${_ccache_root}/ccache.exe")

    if(NOT EXISTS "${_ccache_exe}")
        set(_zip "${VIXEN_CCACHE_CACHE_DIR}/${_ccache_dir_name}.zip")
        set(_url "https://github.com/ccache/ccache/releases/download/v${VIXEN_CCACHE_VERSION}/${_ccache_dir_name}.zip")
        file(MAKE_DIRECTORY "${VIXEN_CCACHE_CACHE_DIR}")
        if(NOT EXISTS "${_zip}")
            message(STATUS "VIXEN: ccache not found — downloading prebuilt ccache ${VIXEN_CCACHE_VERSION} (~4 MB, one-time, cached) ...")
            file(DOWNLOAD "${_url}" "${_zip}" SHOW_PROGRESS TLS_VERIFY ON STATUS _dl)
            list(GET _dl 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE "${_zip}")
                message(WARNING "VIXEN: ccache download failed (${_dl}). URL: ${_url}\n"
                                 "  Falling back to sccache (MSVC PCH will be uncacheable — see CMakeLists.txt USE_CCACHE comment).")
            endif()
        endif()
        if(EXISTS "${_zip}")
            message(STATUS "VIXEN: extracting ccache into ${VIXEN_CCACHE_CACHE_DIR} ...")
            file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${VIXEN_CCACHE_CACHE_DIR}")
        endif()
    else()
        message(STATUS "VIXEN: reusing cached ccache at ${_ccache_exe} (no download).")
    endif()

    if(EXISTS "${_ccache_exe}")
        set(VIXEN_PROVISIONED_CCACHE_PROGRAM "${_ccache_exe}" CACHE FILEPATH "" FORCE)
        message(STATUS "VIXEN: using auto-provisioned ccache at ${_ccache_exe}")
    endif()
else()
    message(STATUS "VIXEN: ccache not found (set -DVIXEN_AUTO_PROVISION_CCACHE=ON to auto-fetch) — "
                    "falling back to sccache if present (MSVC PCH will be uncacheable).")
endif()

# --- uninstall as an explicit build step ---
if(EXISTS "${VIXEN_CCACHE_CACHE_DIR}" AND NOT TARGET vixen-uninstall-ccache)
    add_custom_target(vixen-uninstall-ccache
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIXEN_CCACHE_CACHE_DIR}"
        COMMENT "VIXEN: removing provisioned ccache cache (${VIXEN_CCACHE_CACHE_DIR})")
endif()
