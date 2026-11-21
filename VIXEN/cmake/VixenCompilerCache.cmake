include_guard(GLOBAL)

# USE_CCACHE=OFF must mean "no compiler cache launcher", including one a previous configure left in
# CMakeCache.txt (-DCMAKE_<LANG>_COMPILER_LAUNCHER=ccache, or the environment variable of the same
# name, both of which persist in the cache). Only ccache/sccache launchers are cleared; any other
# launcher (distcc, a PCH wrapper) is the caller's explicit choice and is left alone.
function(vixen_clear_cached_compiler_cache_launchers)
    foreach(_language C CXX)
        set(_var CMAKE_${_language}_COMPILER_LAUNCHER)
        if(NOT DEFINED CACHE{${_var}})
            continue()
        endif()
        get_property(_value CACHE ${_var} PROPERTY VALUE)
        if(_value MATCHES "(^|[;/\\\\])(ccache|sccache)(\\.exe)?(;|$)")
            message(STATUS "USE_CCACHE=OFF: clearing cached ${_var} (was '${_value}')")
            set(${_var} "" CACHE STRING "Compiler launcher (cleared: USE_CCACHE=OFF)" FORCE)
            set(${_var} "" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------------------------
# Windows shared compiler cache (R873: the Windows-native side mirrors the WSL one).
#
# Every native tree (C:\vixen-native\<lane>) used to compile against its own
# <build>/_cache/compiler/ccache, so a fresh tree was always cold (measured 11-12: 53 per-tree
# caches, 32 GB, 0.4-1.7 GB each, the same objects over and over). One cache for all trees:
#   - the data dir is an explicit -DVIXEN_COMPILER_CACHE_DIR=<dir> (old layout <dir>/ccache),
#     else the caller's CCACHE_DIR / SCCACHE_DIR (build.bat sets C:\ccache), else <SystemDrive>\ccache;
#   - base_dir is the parent of the checkout root (C:/vixen-native for C:/vixen-native/<lane>/VIXEN),
#     so the absolute paths of two trees hash alike;
#   - hash_dir is off: ccache otherwise hashes the working directory into every debug-info compile,
#     and the working directory is the per-tree build directory.
# A cached VIXEN_COMPILER_CACHE_DIR equal to the legacy per-tree default (what every existing tree
# has in its CMakeCache.txt) counts as unset, so existing trees switch on their next configure.
# ---------------------------------------------------------------------------------------------

# vixen_resolve_windows_compiler_cache_dir(<kind: ccache|sccache> <out> <requested> <legacy_default>
#                                          <env_dir> <system_drive>)
function(vixen_resolve_windows_compiler_cache_dir kind out requested legacy_default env_dir system_drive)
    string(REPLACE "\\" "/" _requested "${requested}")
    string(REPLACE "\\" "/" _legacy "${legacy_default}")
    string(REPLACE "\\" "/" _env "${env_dir}")
    if(NOT _requested STREQUAL "" AND NOT _requested STREQUAL _legacy)
        set(_dir "${_requested}/${kind}")
    elseif(NOT _env STREQUAL "")
        set(_dir "${_env}")
    else()
        if(system_drive STREQUAL "")
            set(system_drive "C:")
        endif()
        string(REPLACE "\\" "/" _drive "${system_drive}")
        set(_dir "${_drive}/${kind}")
    endif()
    if(_dir MATCHES "^//wsl(\\.localhost|\\$)?/")
        message(FATAL_ERROR "VIXEN compiler cache: '${_dir}' is inside WSL; the Windows-native cache must live on a Windows drive "
                            "(set -DVIXEN_COMPILER_CACHE_DIR or CCACHE_DIR to a C:/ path).")
    endif()
    set(${out} "${_dir}" PARENT_SCOPE)
endfunction()

# vixen_compiler_cache_base_dir(<out> <vixen_root> <env_base_dir>): the common root of the trees.
function(vixen_compiler_cache_base_dir out vixen_root env_base_dir)
    string(REPLACE "\\" "/" _env "${env_base_dir}")
    if(NOT _env STREQUAL "")
        set(${out} "${_env}" PARENT_SCOPE)
        return()
    endif()
    get_filename_component(_checkout "${vixen_root}/.." ABSOLUTE)
    get_filename_component(_base "${_checkout}/.." ABSOLUTE)
    get_filename_component(_above "${_base}/.." ABSOLUTE)
    if(_above STREQUAL _base)
        set(_base "${_checkout}") # never make a drive root the base_dir
    endif()
    set(${out} "${_base}" PARENT_SCOPE)
endfunction()

# vixen_compiler_cache_launcher_env(<out> <kind> <cache_dir> <base_dir> <max_size_env> <sloppiness>)
# -> the NAME=value list for `cmake -E env`. A size the caller already set in the environment is
# left alone (build.bat exports CCACHE_MAXSIZE); otherwise the cap is the measured working set.
function(vixen_compiler_cache_launcher_env out kind cache_dir base_dir max_size_env sloppiness)
    if(kind STREQUAL "sccache")
        set(${out} "SCCACHE_DIR=${cache_dir}" PARENT_SCOPE)
        return()
    endif()
    set(_env "CCACHE_DIR=${cache_dir}" "CCACHE_BASEDIR=${base_dir}" "CCACHE_NOHASHDIR=1")
    if(max_size_env STREQUAL "")
        list(APPEND _env "CCACHE_MAXSIZE=20G")
    endif()
    if(NOT sloppiness STREQUAL "")
        list(APPEND _env "CCACHE_SLOPPINESS=${sloppiness}")
    endif()
    set(${out} "${_env}" PARENT_SCOPE)
endfunction()
