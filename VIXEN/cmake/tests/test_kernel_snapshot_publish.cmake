# Writers publish a snapshot without file(LOCK), which hangs on Windows over a WSL-backed path.
if(NOT DEFINED VIXEN_SNAPSHOT_TEST_ROOT)
    message(FATAL_ERROR "VIXEN_SNAPSHOT_TEST_ROOT must be inside the caller's build tree")
endif()
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(repository "${VIXEN_SNAPSHOT_TEST_ROOT}/kernel")
set(cache "${VIXEN_SNAPSHOT_TEST_ROOT}/cache")

if(VIXEN_SNAPSHOT_TEST_WRITER)
    set(VIXEN_KERNEL_CACHE_DIR "${cache}")
    include("${vixen_root}/cmake/VixenKernelSnapshot.cmake")
    vixen_kernel_snapshot("${repository}" "${VIXEN_SNAPSHOT_TEST_SHA}" actual)
    return()
endif()

find_program(git NAMES git REQUIRED)
file(REMOVE_RECURSE "${VIXEN_SNAPSHOT_TEST_ROOT}")
file(MAKE_DIRECTORY "${repository}")
file(MAKE_DIRECTORY "${repository}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~")
set(long_input_dir "${repository}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/long-inputs")
file(MAKE_DIRECTORY "${long_input_dir}")
foreach(index RANGE 1 800)
    file(WRITE "${long_input_dir}/long-input-${index}-with-a-path-name-long-enough-to-overflow-windows-command-lines.cs"
        "// long archive path-list regression\n")
endforeach()
file(MAKE_DIRECTORY "${repository}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests")
file(WRITE "${repository}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/excluded.cs"
    "// excluded test input\n")
file(WRITE "${repository}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/excluded.meta"
    "excluded metadata\n")
file(WRITE "${repository}/Directory.Build.props" "<Project />\n")
file(WRITE "${repository}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj" "<Project />\n")
file(WRITE "${repository}/unused.txt" "must not be snapshotted\n")
include("${vixen_root}/cmake/VixenWindowsGitTrust.cmake")
vixen_trust_wsl_git_directory("${repository}")
foreach(step "init" "add;Directory.Build.props;Packages/com.yeroket.utility.kernel-framework/CodegenTool~;unused.txt" "-c;user.name=t;-c;user.email=t@example.com;commit;-m;kernel")
    execute_process(COMMAND "${git}" -C "${repository}" ${step} RESULT_VARIABLE result
        OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "git ${step} failed: ${error}")
    endif()
endforeach()
execute_process(COMMAND "${git}" -C "${repository}" rev-parse HEAD
    OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE)
set(snapshot "${cache}/sources/yeroket-${sha}")

if(UNIX)
    # On Linux, retain a witness for the pre-fix command so the bounded archive path is checked
    # against the historical file list and blob hashes.  This intentionally remains UNIX-only:
    # passing this list to Git is the Windows failure that the regression prevents.
    execute_process(COMMAND "${git}" -C "${repository}" ls-tree -r --name-only "${sha}" --
        Directory.Build.props Packages/com.yeroket.utility.kernel-framework/CodegenTool~
        OUTPUT_VARIABLE legacy_paths RESULT_VARIABLE legacy_listed
        ERROR_VARIABLE legacy_error OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT legacy_listed EQUAL 0)
        message(FATAL_ERROR "could not list legacy snapshot inputs: ${legacy_error}")
    endif()
    string(REPLACE "\n" ";" legacy_paths "${legacy_paths}")
    set(legacy_archive_paths)
    foreach(legacy_path IN LISTS legacy_paths)
        if(legacy_path AND NOT legacy_path MATCHES "(^|/)Tests(/|$)"
                AND NOT legacy_path MATCHES "\\.meta$")
            list(APPEND legacy_archive_paths "${legacy_path}")
        endif()
    endforeach()
endif()

# A held lock on the old writer lock path must not matter: nothing takes it any more. (Taking
# the lock here would itself hang on a WSL-backed path under Windows.)
file(MAKE_DIRECTORY "${cache}/sources")
if(NOT WIN32)
    file(LOCK "${snapshot}.lock" GUARD PROCESS TIMEOUT 0)
endif()

set(writer ${CMAKE_COMMAND} --log-level=WARNING -DVIXEN_SNAPSHOT_TEST_ROOT=${VIXEN_SNAPSHOT_TEST_ROOT}
    -DVIXEN_SNAPSHOT_TEST_WRITER=ON -DVIXEN_SNAPSHOT_TEST_SHA=${sha} -P ${CMAKE_CURRENT_LIST_FILE})
# Several writers race to publish the same snapshot; all must finish and agree.
execute_process(COMMAND ${writer} COMMAND ${writer} COMMAND ${writer} COMMAND ${writer}
    RESULTS_VARIABLE results ERROR_VARIABLE error OUTPUT_QUIET)
foreach(result IN LISTS results)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "a concurrent writer failed (${results}): ${error}")
    endif()
endforeach()
file(READ "${snapshot}/.archive-complete" stamp)
if(NOT stamp STREQUAL "${sha}\n"
        OR NOT EXISTS "${snapshot}/Directory.Build.props"
        OR NOT EXISTS "${snapshot}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/CodegenTool.csproj"
        OR EXISTS "${snapshot}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Tests/excluded.cs"
        OR EXISTS "${snapshot}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/excluded.meta"
        OR EXISTS "${snapshot}/unused.txt")
    message(FATAL_ERROR "snapshot was not published completely: ${snapshot}")
endif()
file(GLOB long_snapshot_files
    "${snapshot}/Packages/com.yeroket.utility.kernel-framework/CodegenTool~/long-inputs/*.cs")
list(LENGTH long_snapshot_files long_snapshot_count)
if(NOT long_snapshot_count EQUAL 800)
    message(FATAL_ERROR "long path-list inputs were not all archived: expected 800, got ${long_snapshot_count}")
endif()

if(UNIX)
    set(legacy_archive "${cache}/legacy.tar")
    set(legacy_stage "${cache}/legacy")
    execute_process(COMMAND "${git}" -c core.autocrlf=false -C "${repository}" archive
        --format=tar --output "${legacy_archive}" "${sha}" -- ${legacy_archive_paths}
        RESULT_VARIABLE legacy_archived ERROR_VARIABLE legacy_error)
    if(NOT legacy_archived EQUAL 0)
        message(FATAL_ERROR "legacy path-expanded archive failed: ${legacy_error}")
    endif()
    file(MAKE_DIRECTORY "${legacy_stage}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${legacy_archive}"
        WORKING_DIRECTORY "${legacy_stage}" RESULT_VARIABLE legacy_extracted ERROR_VARIABLE legacy_error)
    if(NOT legacy_extracted EQUAL 0)
        message(FATAL_ERROR "legacy archive extraction failed: ${legacy_error}")
    endif()
    file(GLOB_RECURSE snapshot_files LIST_DIRECTORIES false RELATIVE "${snapshot}" "${snapshot}/*")
    file(GLOB_RECURSE legacy_files LIST_DIRECTORIES false RELATIVE "${legacy_stage}" "${legacy_stage}/*")
    list(REMOVE_ITEM snapshot_files ".archive-complete")
    list(SORT snapshot_files)
    list(SORT legacy_files)
    if(NOT "${snapshot_files}" STREQUAL "${legacy_files}")
        message(FATAL_ERROR "bounded archive changed the file list")
    endif()
    foreach(snapshot_file IN LISTS snapshot_files)
        file(SHA256 "${snapshot}/${snapshot_file}" snapshot_hash)
        file(SHA256 "${legacy_stage}/${snapshot_file}" legacy_hash)
        if(NOT "${snapshot_hash}" STREQUAL "${legacy_hash}")
            message(FATAL_ERROR "bounded archive changed blob bytes for ${snapshot_file}")
        endif()
    endforeach()
    message(STATUS "Bounded archive matches the historical file list and blob hashes")
endif()
file(GLOB leftovers "${cache}/sources/*.partial-*")
if(leftovers)
    message(FATAL_ERROR "writers left staging data behind: ${leftovers}")
endif()
message(STATUS "Concurrent snapshot writers publish one complete entry without a lock")
