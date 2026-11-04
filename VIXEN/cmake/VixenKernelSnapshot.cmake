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

set(VIXEN_KERNEL_CACHE_RETENTION_DAYS 7 CACHE STRING
    "Days since last use before an unpinned kernel-codegen cache entry is reclaimed")

set(_vixen_kernel_codegen_paths
    Directory.Build.props
    Packages/com.yeroket.utility.kernel-framework/Directory.Build.props
    Packages/com.yeroket.utility.kernel-framework/RoslynVersions.g.props
    Packages/com.yeroket.utility.kernel-framework/CodegenTool~
    Packages/com.yeroket.utility.kernel-framework/SourceGenerator~
    Packages/com.yeroket.utility.kernel-framework/Editor/KernelArtifactHeader.cs
    Packages/com.yeroket.utility.kernel-framework/Editor/HlslSourceFieldNames.cs
    Packages/com.yeroket.utility.kernel-framework/Attributes~
    Packages/com.yeroket.utility.kernel-framework/Schema~
    Packages/com.yeroket.utility.kernel-framework/ContentIds~
    Packages/com.yeroket.utility.kernel-framework/Analyzers~
    Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers
    Packages/com.yeroket.utility.kernel-framework/Runtime
    Packages/com.utility.sdf/Runtime/Kernels
    Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs)

function(_vixen_cache_entry_is_old path out_old)
    string(TIMESTAMP _now "%s" UTC)
    file(TIMESTAMP "${path}" _last "%s" UTC)
    if(NOT _last)
        set(${out_old} FALSE PARENT_SCOPE)
        return()
    endif()
    math(EXPR _age "${_now} - ${_last}")
    math(EXPR _retention_seconds "${VIXEN_KERNEL_CACHE_RETENTION_DAYS} * 86400")
    set(${out_old} FALSE PARENT_SCOPE)
    if(_age GREATER _retention_seconds)
        set(${out_old} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(vixen_kernel_cache_prune_sources selected_sha)
    set(_keep_shas "${selected_sha}")
    if(VIXEN_KERNEL_YEROKET_KERNEL_TRACKED_SHA)
        list(APPEND _keep_shas "${VIXEN_KERNEL_YEROKET_KERNEL_TRACKED_SHA}")
    endif()
    if(VIXEN_YEROKET_KERNEL_SHA_OVERRIDE)
        list(APPEND _keep_shas "${VIXEN_YEROKET_KERNEL_SHA_OVERRIDE}")
    endif()
    list(REMOVE_DUPLICATES _keep_shas)
    file(GLOB _entries LIST_DIRECTORIES true "${VIXEN_KERNEL_CACHE_DIR}/sources/yeroket-*")
    foreach(_entry IN LISTS _entries)
        get_filename_component(_name "${_entry}" NAME)
        set(_keep FALSE)
        foreach(_sha IN LISTS _keep_shas)
            if(_name STREQUAL "yeroket-${_sha}")
                set(_keep TRUE)
            endif()
        endforeach()
        if(_keep)
            continue()
        endif()
        set(_age_path "${_entry}")
        if(EXISTS "${_entry}/.archive-complete")
            set(_age_path "${_entry}/.archive-complete")
        endif()
        _vixen_cache_entry_is_old("${_age_path}" _old)
        if(_old)
            file(REMOVE_RECURSE "${_entry}")
        endif()
    endforeach()
endfunction()

function(vixen_kernel_cache_prune_tools selected_key tracked_key)
    file(GLOB _entries LIST_DIRECTORIES true "${VIXEN_KERNEL_CACHE_DIR}/tools/*")
    foreach(_entry IN LISTS _entries)
        get_filename_component(_name "${_entry}" NAME)
        if(_name STREQUAL "${selected_key}" OR _name STREQUAL "${tracked_key}")
            continue()
        endif()
        set(_age_path "${_entry}")
        if(EXISTS "${_entry}/.build-complete")
            set(_age_path "${_entry}/.build-complete")
        endif()
        _vixen_cache_entry_is_old("${_age_path}" _old)
        if(_old)
            file(REMOVE_RECURSE "${_entry}")
        endif()
    endforeach()
endfunction()

function(vixen_kernel_codegen_tool_key sha host_name host_processor needs_wsl dotnet sdk_list tfm recipe out_key)
    string(SHA256 _key
        "${sha}|${host_name}|${host_processor}|${needs_wsl}|${dotnet}|${sdk_list}|${tfm}|Release|invariant|${recipe}|artifacts-v1${ARGN}")
    set(${out_key} "${_key}" PARENT_SCOPE)
endfunction()

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
    # A short SHA (git's abbreviation minimum is 4 hex digits) is resolved against the kernel
    # repository first. Git's own rule decides it: a prefix naming several objects is refused, never guessed.
    string(LENGTH "${sha}" _length)
    if(_length LESS 40 AND _length GREATER_EQUAL 4 AND sha MATCHES "^[0-9a-fA-F]+$")
        find_program(_vixen_kernel_git NAMES git REQUIRED)
        vixen_trust_wsl_git_directory("${repository}")
        execute_process(COMMAND "${_vixen_kernel_git}" -C "${repository}" rev-parse --disambiguate=${sha}
            OUTPUT_VARIABLE _matches ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REPLACE "\n" ";" _matches "${_matches}")
        list(LENGTH _matches _count)
        if(_count EQUAL 0)
            message(FATAL_ERROR "[codegen] kernel SHA ${sha} names no object in ${repository}")
        elseif(_count GREATER 1)
            message(FATAL_ERROR "[codegen] kernel SHA ${sha} is ambiguous in ${repository}: "
                "${_count} objects match; give more hex digits")
        endif()
        set(sha "${_matches}")
        string(LENGTH "${sha}" _length)
    endif()
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
            file(TOUCH_NOCREATE "${_stamp}")
            vixen_kernel_cache_prune_sources("${sha}")
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
    set(_archive_paths)
    foreach(_path IN LISTS _vixen_kernel_codegen_paths)
        execute_process(COMMAND "${_vixen_kernel_git}" -C "${repository}" ls-tree -r --name-only
            "${sha}" -- "${_path}" OUTPUT_VARIABLE _path_files RESULT_VARIABLE _listed
            ERROR_VARIABLE _error OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _listed EQUAL 0)
            message(FATAL_ERROR "[codegen] cannot list codegen inputs for pinned kernel ${sha}: ${_error}")
        endif()
        string(REPLACE "\n" ";" _path_files "${_path_files}")
        foreach(_file IN LISTS _path_files)
            if(_file AND NOT _file MATCHES "(^|/)Tests(/|$)" AND NOT _file MATCHES "\\.meta$")
                list(APPEND _archive_paths "${_file}")
            endif()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _archive_paths)
    if(NOT _archive_paths)
        message(FATAL_ERROR "[codegen] pinned kernel ${sha} has no usable codegen inputs")
    endif()
    execute_process(COMMAND "${_vixen_kernel_git}" -c core.autocrlf=false -C "${repository}" archive
        --format=tar --output "${_archive}" "${sha}" -- ${_archive_paths}
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
        vixen_kernel_cache_prune_sources("${sha}")
        set(${out_directory} "${_directory}" PARENT_SCOPE)
        message(STATUS "[codegen] cached pinned kernel snapshot: ${_directory}")
        return()
    endif()
    file(REMOVE_RECURSE "${_stage}")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _recorded_sha)
        if(_recorded_sha STREQUAL "${sha}\n")
            file(TOUCH_NOCREATE "${_stamp}")
            vixen_kernel_cache_prune_sources("${sha}")
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
