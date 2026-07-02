# ProvisionGdb.cmake
#
# gdb is needed to get a real backtrace on the vixen-wsl-debug preset (Release builds elsewhere
# inline/optimize away the frames a debugger needs). It's a system package, not something VIXEN
# builds itself, so this only detects it by default; auto-provisioning (no sudo, via apt-get
# download) is opt-in since it fetches into a project-local cache rather than touching the system.
#
# Dependency scope: gdb's DIRECT package deps only (~18 packages: libbabeltrace1, libpython3.12t64,
# libreadline8t64, etc.) — NOT the full recursive closure (~70 packages once libdebuginfod1t64 drags
# in a curl+gnutls+krb5 stack for a debuginfo-server feature this project never uses). Direct deps
# are almost always already satisfied by a normal Ubuntu base install; a genuinely bare-minimum
# machine may still be missing one, in which case the extracted gdb binary fails to run (ldd
# reports "not found") and this falls back to "gdb not found" the same as if auto-provision were
# off, rather than silently producing a broken debugger.
#
# Adds a convenience target: `cmake --build <preset-build-dir> --target gdb-<exe-target>` runs that
# target's built executable under the provisioned (or system) gdb.
#
# Knobs:
#   -DVIXEN_AUTO_PROVISION_GDB=ON    auto-download gdb (no sudo) when not found on PATH
#   -DVIXEN_GDB_CACHE_DIR=<path>     where the provisioned gdb lives
#   build target  vixen-uninstall-gdb   remove the cache

include_guard(GLOBAL)

# gdb is a Linux/WSL debugging tool; Windows uses its own debugger (Visual Studio / windbg).
if(NOT (UNIX AND NOT APPLE))
    return()
endif()

option(VIXEN_AUTO_PROVISION_GDB "Auto-provision gdb (no sudo, direct deps only) when not found on PATH" OFF)
set(VIXEN_GDB_CACHE_DIR "${VIXEN_ROOT}/.gdb-deps" CACHE PATH "Provisioned gdb cache")

# 1) System gdb present? Use it directly.
find_program(VIXEN_GDB_EXECUTABLE gdb)
if(VIXEN_GDB_EXECUTABLE)
    message(STATUS "VIXEN: found gdb: ${VIXEN_GDB_EXECUTABLE}")
else()
    set(_vixen_gdb_cache_bin "${VIXEN_GDB_CACHE_DIR}/usr/bin/gdb")

    # 2) Not cached yet? auto-provision via apt-get download (Debian/Ubuntu; other distros: install
    #    gdb yourself, or accept no debugger integration).
    if(NOT EXISTS "${_vixen_gdb_cache_bin}" AND VIXEN_AUTO_PROVISION_GDB)
        find_program(_vixen_apt apt-get)
        find_program(_vixen_dpkg dpkg-deb)
        if(_vixen_apt AND _vixen_dpkg)
            message(STATUS "VIXEN: gdb not found — provisioning (no sudo) into ${VIXEN_GDB_CACHE_DIR} ...")
            file(MAKE_DIRECTORY "${VIXEN_GDB_CACHE_DIR}/_dl")
            execute_process(
                COMMAND ${_vixen_apt} download
                    gdb libbabeltrace1 libdebuginfod1t64 libipt2 libmpfr6 libncursesw6
                    libpython3.12t64 libreadline8t64 libsource-highlight4t64 libxxhash0 libzstd1
                    libgmp10 liblzma5 libexpat1 zlib1g libc6 libgcc-s1 libstdc++6
                WORKING_DIRECTORY "${VIXEN_GDB_CACHE_DIR}/_dl"
                RESULT_VARIABLE _vixen_gdb_dlrc OUTPUT_QUIET ERROR_QUIET)
            file(GLOB _vixen_gdb_debs "${VIXEN_GDB_CACHE_DIR}/_dl/*.deb")
            foreach(_deb ${_vixen_gdb_debs})
                # dpkg-deb shells out to tar internally — requires tar on PATH (present on any
                # normal Debian/Ubuntu install; absence is the one realistic way this step fails
                # even though the direct-deps download above succeeded).
                execute_process(COMMAND ${_vixen_dpkg} -x "${_deb}" "${VIXEN_GDB_CACHE_DIR}" OUTPUT_QUIET ERROR_QUIET)
            endforeach()
        else()
            message(STATUS "VIXEN: no apt-get/dpkg-deb — cannot auto-provision gdb. Install it yourself "
                           "(apt-get install gdb), or accept no debugger integration.")
        endif()
    endif()

    # 3) Use the cache if we have it; else no debugger target (build still succeeds).
    if(EXISTS "${_vixen_gdb_cache_bin}")
        set(VIXEN_GDB_EXECUTABLE "${_vixen_gdb_cache_bin}" CACHE FILEPATH "" FORCE)
        message(STATUS "VIXEN: gdb provisioned at ${VIXEN_GDB_EXECUTABLE}")
    elseif(VIXEN_AUTO_PROVISION_GDB)
        message(STATUS "VIXEN: gdb auto-provision failed (or a direct dep was missing on this "
                       "machine) — no debugger target available. Set VIXEN_AUTO_PROVISION_GDB=OFF "
                       "to silence this, or install gdb yourself.")
    else()
        message(STATUS "VIXEN: gdb not found (set -DVIXEN_AUTO_PROVISION_GDB=ON to auto-fetch, "
                       "no sudo required) — no debugger target available.")
    endif()
endif()

if(EXISTS "${VIXEN_GDB_CACHE_DIR}" AND NOT TARGET vixen-uninstall-gdb)
    add_custom_target(vixen-uninstall-gdb
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIXEN_GDB_CACHE_DIR}"
        COMMENT "VIXEN: removing provisioned gdb cache (${VIXEN_GDB_CACHE_DIR})")
endif()

# Convenience debug-run targets: `cmake --build <dir> --target gdb-<exe-target>` runs that
# executable under gdb. Call vixen_add_gdb_target(<exe-target>) after add_executable(<exe-target>);
# no-op (and no error) if gdb isn't available, so callers don't need to guard every call site.
function(vixen_add_gdb_target EXE_TARGET)
    if(NOT VIXEN_GDB_EXECUTABLE)
        return()
    endif()
    if(NOT TARGET ${EXE_TARGET})
        message(WARNING "VIXEN: vixen_add_gdb_target(${EXE_TARGET}) called before add_executable(${EXE_TARGET})")
        return()
    endif()
    add_custom_target(gdb-${EXE_TARGET}
        COMMAND ${VIXEN_GDB_EXECUTABLE} -q --args $<TARGET_FILE:${EXE_TARGET}> ${VIXEN_GDB_ARGS}
        DEPENDS ${EXE_TARGET}
        WORKING_DIRECTORY $<TARGET_FILE_DIR:${EXE_TARGET}>
        USES_TERMINAL
        COMMENT "VIXEN: launching ${EXE_TARGET} under gdb"
    )
endfunction()
