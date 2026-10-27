include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/VixenWriteIfDifferent.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VixenWindowsGitTrust.cmake")

# The default cache is outside source checkouts. Tests/isolated lanes can override
# it with an untracked directory inside their own worktree.
if(NOT DEFINED VIXEN_KERNEL_CACHE_DIR)
    if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(_vixen_kernel_cache "$ENV{XDG_CACHE_HOME}/vixen/kernel-codegen")
    elseif(WIN32 AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
        set(_vixen_kernel_cache "$ENV{LOCALAPPDATA}/VIXEN/kernel-codegen")
    elseif(NOT "$ENV{HOME}" STREQUAL "")
        set(_vixen_kernel_cache "$ENV{HOME}/.cache/vixen/kernel-codegen")
    else()
        message(FATAL_ERROR "[codegen] set VIXEN_KERNEL_CACHE_DIR: no user cache location was found")
    endif()
    set(VIXEN_KERNEL_CACHE_DIR "${_vixen_kernel_cache}" CACHE PATH
        "Content-addressed pinned kernel snapshots and built CodegenTool cache")
endif()

# Chooses the kernel SHA for this configure. The override lives in the build tree's cache, so
# it is recorded together with the tracked pin it was set against; once the tracked pin moves
# (a merge, a new lane base) a carried-over override is cleared instead of keeping the tree on a
# snapshot the source no longer pins.
function(vixen_select_kernel_sha tracked out_sha)
    set(VIXEN_YEROKET_KERNEL_SHA_OVERRIDE "" CACHE STRING
        "Optional per-build-tree override for VIXEN's tracked Yeroket kernel pin")
    set(_override "${VIXEN_YEROKET_KERNEL_SHA_OVERRIDE}")
    set(_recorded "${VIXEN_YEROKET_KERNEL_SHA_OVERRIDE_RECORDED}")
    if(_override AND _recorded MATCHES "^${_override}@" AND NOT _recorded STREQUAL "${_override}@${tracked}")
        message(STATUS "[codegen] clearing stale kernel pin override ${_override}: the tracked pin moved "
            "from ${_recorded} to ${tracked}")
        set(VIXEN_YEROKET_KERNEL_SHA_OVERRIDE "" CACHE STRING
            "Optional per-build-tree override for VIXEN's tracked Yeroket kernel pin" FORCE)
        set(_override "")
    endif()
    if(_override)
        set(VIXEN_YEROKET_KERNEL_SHA_OVERRIDE_RECORDED "${_override}@${tracked}" CACHE INTERNAL
            "Kernel pin override and the tracked pin it was set against" FORCE)
        message(STATUS "[codegen] using per-build-tree kernel pin override: ${_override}")
        set(${out_sha} "${_override}" PARENT_SCOPE)
    else()
        set(VIXEN_YEROKET_KERNEL_SHA_OVERRIDE_RECORDED "" CACHE INTERNAL
            "Kernel pin override and the tracked pin it was set against" FORCE)
        message(STATUS "[codegen] using tracked Yeroket kernel pin: ${tracked}")
        set(${out_sha} "${tracked}" PARENT_SCOPE)
    endif()
endfunction()

# Resolves the Yeroket kernel root from -DYEROKET_ROOT, then the YEROKET_ROOT environment variable,
# then the first home candidate holding the kernel's codegen tool. An explicit value must be a
# directory; a bad one is fatal and names its source, so configure never silently uses another clone.
function(vixen_resolve_yeroket_root home_candidates out_root)
    set(_root "")
    if(DEFINED YEROKET_ROOT AND NOT "${YEROKET_ROOT}" STREQUAL "")
        set(_root "${YEROKET_ROOT}")
        set(_source "-DYEROKET_ROOT")
    elseif(NOT "$ENV{YEROKET_ROOT}" STREQUAL "")
        set(_root "$ENV{YEROKET_ROOT}")
        set(_source "the YEROKET_ROOT environment variable")
    endif()
    if(NOT _root STREQUAL "")
        if(NOT IS_DIRECTORY "${_root}")
            message(FATAL_ERROR "[codegen] kernel root from ${_source} is not a directory: ${_root}")
        endif()
    else()
        foreach(_h IN LISTS home_candidates)
            if(_h AND EXISTS "${_h}/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj")
                set(_root "${_h}/Github/Yeroket-Fantasy")
                set(_source "the home-directory probe")
                break()
            endif()
        endforeach()
        if(_root STREQUAL "")
            set(_root "$ENV{HOME}/Github/Yeroket-Fantasy")
            set(_source "the home default (no checkout found)")
        endif()
    endif()
    message(STATUS "[codegen] kernel root from ${_source}: ${_root}")
    set(${out_root} "${_root}" PARENT_SCOPE)
endfunction()

function(vixen_kernel_snapshot repository sha out_directory)
    string(LENGTH "${sha}" _length)
    if(NOT _length EQUAL 40 OR NOT sha MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "[codegen] YEROKET_KERNEL_SHA must be a full commit SHA: ${sha}")
    endif()
    set(_directory "${VIXEN_KERNEL_CACHE_DIR}/sources/yeroket-${sha}")
    set(_stamp "${_directory}/.archive-complete")
    # Complete entries are immutable and atomically published, so neither readers nor writers take
    # a lock: Windows CMake's file(LOCK) blocks forever, ignoring TIMEOUT, on a WSL-backed path.
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _recorded_sha)
        if(_recorded_sha STREQUAL "${sha}\n")
            set(${out_directory} "${_directory}" PARENT_SCOPE)
            message(STATUS "[codegen] reusing pinned kernel snapshot: ${_directory}")
            return()
        endif()
        message(FATAL_ERROR "[codegen] invalid snapshot stamp: ${_stamp}")
    endif()
    file(MAKE_DIRECTORY "${VIXEN_KERNEL_CACHE_DIR}/sources")

    find_program(_vixen_kernel_git NAMES git REQUIRED)
    vixen_trust_wsl_git_directory("${repository}")
    execute_process(COMMAND "${_vixen_kernel_git}" -C "${repository}" cat-file -e "${sha}^{commit}"
        RESULT_VARIABLE _exists ERROR_VARIABLE _error OUTPUT_QUIET)
    if(NOT _exists EQUAL 0)
        message(FATAL_ERROR "[codegen] pinned kernel ${sha} unavailable from ${repository}: ${_error}")
    endif()
    # Each writer extracts into its own stage and publishes it with one directory rename, which
    # fails onto a populated directory. The writer that renames first owns the entry and every
    # later writer discards its stage. A failed extraction never publishes a complete directory.
    string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" _writer)
    set(_stage "${_directory}.partial-${_writer}")
    set(_archive "${_stage}.tar")
    file(MAKE_DIRECTORY "${_stage}")
    # Windows Git commonly has core.autocrlf=true.  `git archive` otherwise applies
    # that checkout policy while materializing the pinned source tree, so a native
    # codegen run can emit CRLF from the same commit whose WSL snapshot is LF.  The
    # snapshot is an immutable source artifact, not a working tree; archive the blob
    # bytes exactly and let the generated-file LF policy apply at the VIXEN boundary.
    execute_process(COMMAND "${_vixen_kernel_git}" -c core.autocrlf=false -C "${repository}" archive
        --format=tar --output "${_archive}" "${sha}"
        RESULT_VARIABLE _archived ERROR_VARIABLE _error)
    if(NOT _archived EQUAL 0)
        message(FATAL_ERROR "[codegen] cannot archive pinned kernel ${sha}: ${_error}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
        WORKING_DIRECTORY "${_stage}" RESULT_VARIABLE _extracted ERROR_VARIABLE _error)
    if(NOT _extracted EQUAL 0)
        message(FATAL_ERROR "[codegen] cannot extract pinned kernel ${sha}: ${_error}")
    endif()
    file(WRITE "${_stage}/.archive-complete" "${sha}\n")
    file(RENAME "${_stage}" "${_directory}" RESULT _published)
    file(REMOVE "${_archive}")
    if(_published EQUAL 0)
        set(${out_directory} "${_directory}" PARENT_SCOPE)
        message(STATUS "[codegen] cached pinned kernel snapshot: ${_directory}")
        return()
    endif()
    file(REMOVE_RECURSE "${_stage}")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _recorded_sha)
        if(_recorded_sha STREQUAL "${sha}\n")
            set(${out_directory} "${_directory}" PARENT_SCOPE)
            message(STATUS "[codegen] reusing pinned kernel snapshot published by another writer: ${_directory}")
            return()
        endif()
        message(FATAL_ERROR "[codegen] invalid snapshot stamp: ${_stamp}")
    endif()
    message(FATAL_ERROR "[codegen] incomplete unmanaged snapshot at ${_directory}; remove it before retrying (${_published})")
endfunction()

# CMake config scripts use bracket arguments so spaces/backslashes in paths and
# semicolons separating command arguments retain their meaning.
function(vixen_codegen_build_config output cache_dir cached_binary_dir local_binary_dir stamp)
    set(_content "set(VIXEN_CODEGEN_CACHE_DIR [==[${cache_dir}]==])\n")
    string(APPEND _content "set(VIXEN_CODEGEN_CACHED_BIN [==[${cached_binary_dir}]==])\n")
    string(APPEND _content "set(VIXEN_CODEGEN_LOCAL_BIN [==[${local_binary_dir}]==])\n")
    string(APPEND _content "set(VIXEN_CODEGEN_LOCAL_STAMP [==[${stamp}]==])\n")
    string(APPEND _content "set(VIXEN_CODEGEN_BUILD_COMMAND\n")
    foreach(_argument IN LISTS ARGN)
        string(APPEND _content "    [==[${_argument}]==]\n")
    endforeach()
    string(APPEND _content ")\n")
    vixen_write_if_different("${output}" "${_content}")
endfunction()
