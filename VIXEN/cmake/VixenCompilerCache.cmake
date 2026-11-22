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
#   - base_dir is explicitly EMPTY. ccache 4.13 with MSVC records every include in the direct-mode
#     manifest as the absolute path of the tree that compiled it (cl /E prints full paths in #line),
#     while a base_dir makes the command-line arguments relative. With a common base_dir two trees
#     therefore computed the same manifest key, and the second tree validated the manifest against the
#     FIRST tree's headers: a tree whose header had changed received the other tree's object
#     (base-reds 141, 2026-11-22: RecipeEntry layout change, EditorDocumentModel.cpp.obj stale).
#     Without a base_dir the arguments keep the tree's own absolute paths, the key is per tree, and
#     the recorded include paths are the tree's own headers. The cache still serves rebuilds, fresh
#     build directories and re-clones at the same tree path; it does not serve a new tree path.
#     Sharing across trees needs identical tree paths, not a base_dir.
#   - hash_dir is off: the working directory is already part of every absolute argument.
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

# vixen_compiler_cache_launcher_env(<out> <kind> <cache_dir> <max_size_env> <sloppiness>)
# -> the NAME=value list for `cmake -E env`. A size the caller already set in the environment is
# left alone (build.bat exports CCACHE_MAXSIZE); otherwise the cap is the measured working set.
function(vixen_compiler_cache_launcher_env out kind cache_dir max_size_env sloppiness)
    if(kind STREQUAL "sccache")
        set(${out} "SCCACHE_DIR=${cache_dir}" PARENT_SCOPE)
        return()
    endif()
    # CCACHE_BASEDIR is set to empty on purpose, so a value inherited from the caller's environment
    # cannot bring back cross-tree manifest keys (see the header comment).
    set(_env "CCACHE_DIR=${cache_dir}" "CCACHE_BASEDIR=" "CCACHE_NOHASHDIR=1")
    if(max_size_env STREQUAL "")
        list(APPEND _env "CCACHE_MAXSIZE=20G")
    endif()
    if(NOT sloppiness STREQUAL "")
        list(APPEND _env "CCACHE_SLOPPINESS=${sloppiness}")
    endif()
    set(${out} "${_env}" PARENT_SCOPE)
endfunction()
