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
file(WRITE "${repository}/kernel.txt" "kernel\n")
include("${vixen_root}/cmake/VixenWindowsGitTrust.cmake")
vixen_trust_wsl_git_directory("${repository}")
foreach(step "init" "add;kernel.txt" "-c;user.name=t;-c;user.email=t@example.com;commit;-m;kernel")
    execute_process(COMMAND "${git}" -C "${repository}" ${step} RESULT_VARIABLE result
        OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "git ${step} failed: ${error}")
    endif()
endforeach()
execute_process(COMMAND "${git}" -C "${repository}" rev-parse HEAD
    OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE)
set(snapshot "${cache}/sources/yeroket-${sha}")

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
if(NOT stamp STREQUAL "${sha}\n" OR NOT EXISTS "${snapshot}/kernel.txt")
    message(FATAL_ERROR "snapshot was not published completely: ${snapshot}")
endif()
file(GLOB leftovers "${cache}/sources/*.partial-*")
if(leftovers)
    message(FATAL_ERROR "writers left staging data behind: ${leftovers}")
endif()
message(STATUS "Concurrent snapshot writers publish one complete entry without a lock")
