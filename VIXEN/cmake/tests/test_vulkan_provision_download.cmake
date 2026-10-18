cmake_minimum_required(VERSION 3.15...4.2)

# Exercises _vixen_download_and_extract from ProvisionVulkan.cmake against local file:// sources.
# Including ProvisionVulkan.cmake would run the whole configure-time provisioning, so the function
# body is read from that file and included on its own.
if(NOT DEFINED VIXEN_DOWNLOAD_TEST_ROOT)
    message(FATAL_ERROR "VIXEN_DOWNLOAD_TEST_ROOT must be inside the caller's build tree")
endif()
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

file(READ "${vixen_root}/cmake/ProvisionVulkan.cmake" provision_source)
string(FIND "${provision_source}" "function(_vixen_download_and_extract" helper_start)
if(helper_start LESS 0)
    message(FATAL_ERROR "_vixen_download_and_extract not found in ProvisionVulkan.cmake")
endif()
string(SUBSTRING "${provision_source}" ${helper_start} -1 helper_tail)
string(FIND "${helper_tail}" "endfunction()" helper_end)
string(SUBSTRING "${helper_tail}" 0 ${helper_end} helper_body)
set(helper_file "${VIXEN_DOWNLOAD_TEST_ROOT}/vixen_download_and_extract.cmake")
file(WRITE "${helper_file}" "${helper_body}endfunction()\n")

# Child mode: run one scenario that is expected to pass or fail on its own.
if(DEFINED VIXEN_DOWNLOAD_TEST_CASE)
    include("${helper_file}")
    set(case_root "${VIXEN_DOWNLOAD_TEST_ROOT}/${VIXEN_DOWNLOAD_TEST_CASE}")
    file(MAKE_DIRECTORY "${case_root}/src")
    set(tarball "${case_root}/cache/sdk.tar.xz")
    set(marker "${case_root}/cache/include/vulkan/vulkan.h")

    if(VIXEN_DOWNLOAD_TEST_CASE STREQUAL "missing_source")
        _vixen_download_and_extract("file://${case_root}/src/absent.tar" "${tarball}" "${case_root}/cache" "${marker}")
    elseif(VIXEN_DOWNLOAD_TEST_CASE STREQUAL "empty_source")
        file(WRITE "${case_root}/src/empty.tar" "")
        _vixen_download_and_extract("file://${case_root}/src/empty.tar" "${tarball}" "${case_root}/cache" "${marker}")
    elseif(VIXEN_DOWNLOAD_TEST_CASE STREQUAL "good")
        file(WRITE "${case_root}/src/include/vulkan/vulkan.h" "// stub header\n")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${case_root}/src/sdk.tar" --format=gnutar include
            WORKING_DIRECTORY "${case_root}/src" RESULT_VARIABLE tar_rc)
        if(NOT tar_rc EQUAL 0)
            message(FATAL_ERROR "could not create the local SDK archive")
        endif()
        _vixen_download_and_extract("file://${case_root}/src/sdk.tar" "${tarball}" "${case_root}/cache" "${marker}")
        if(NOT EXISTS "${marker}")
            message(FATAL_ERROR "first provision did not extract the header")
        endif()
        # A finished tarball must be reused: this second source does not exist, so any download fails.
        file(REMOVE_RECURSE "${case_root}/cache/include")
        _vixen_download_and_extract("file://${case_root}/src/absent.tar" "${tarball}" "${case_root}/cache" "${marker}")
        if(EXISTS "${case_root}/src/absent.tar")
            message(FATAL_ERROR "unexpected source")
        endif()
        if(NOT EXISTS "${marker}")
            message(FATAL_ERROR "second provision did not reuse the finished tarball")
        endif()
    else()
        message(FATAL_ERROR "unknown VIXEN_DOWNLOAD_TEST_CASE=${VIXEN_DOWNLOAD_TEST_CASE}")
    endif()
    return()
endif()

# Parent mode: run each child scenario in its own process and check its outcome.
foreach(case_name IN ITEMS missing_source empty_source)
    set(case_root "${VIXEN_DOWNLOAD_TEST_ROOT}/${case_name}")
    file(REMOVE_RECURSE "${case_root}")
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DVIXEN_DOWNLOAD_TEST_ROOT=${VIXEN_DOWNLOAD_TEST_ROOT}"
        "-DVIXEN_DOWNLOAD_TEST_CASE=${case_name}" -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "${case_name}: a failed download was accepted")
    endif()
    if(EXISTS "${case_root}/cache/sdk.tar.xz")
        message(FATAL_ERROR "${case_name}: a failed download left a file at the final tarball name")
    endif()
    file(GLOB leftovers "${case_root}/cache/*.part")
    if(leftovers)
        message(FATAL_ERROR "${case_name}: a failed download left a temporary file: ${leftovers}")
    endif()
endforeach()

set(good_root "${VIXEN_DOWNLOAD_TEST_ROOT}/good")
file(REMOVE_RECURSE "${good_root}")
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DVIXEN_DOWNLOAD_TEST_ROOT=${VIXEN_DOWNLOAD_TEST_ROOT}"
    "-DVIXEN_DOWNLOAD_TEST_CASE=good" -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "good: provisioning failed: ${output}${error}")
endif()
message(STATUS "Vulkan provisioning download fails closed, leaves no partial file, and reuses finished tarballs")
