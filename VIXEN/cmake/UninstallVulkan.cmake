# Uninstall one shared Vulkan SDK cache entry after taking its non-blocking entry lock.

if(NOT VIXEN_CONFIRM_UNINSTALL_VULKAN)
    message(FATAL_ERROR
        "VIXEN: refusing to remove shared Vulkan SDK; "
        "configure with -DVIXEN_CONFIRM_UNINSTALL_VULKAN=ON explicitly to confirm deletion.")
endif()

if(NOT DEFINED VIXEN_VULKAN_CACHE_DIR OR
   NOT DEFINED VIXEN_VULKAN_SDK_VERSION OR
   "${VIXEN_VULKAN_CACHE_DIR}" STREQUAL "" OR
   "${VIXEN_VULKAN_SDK_VERSION}" STREQUAL "")
    message(FATAL_ERROR "VIXEN: Vulkan uninstall is missing its cache directory or SDK version.")
endif()

set(_vixen_vulkan_entry "${VIXEN_VULKAN_CACHE_DIR}/${VIXEN_VULKAN_SDK_VERSION}")
set(_vixen_vulkan_lock "${_vixen_vulkan_entry}.lock")

# CMake's file(LOCK) and Linux flock(2) are different lock namespaces. Run the actual deletion
# under flock when available so this path also respects the shell-level lock used by cache tooling;
# the child then takes CMake's lock below, preserving compatibility with the existing provisioner.
if(UNIX AND NOT VIXEN_UNINSTALL_VULKAN_LOCKED)
    find_program(_vixen_flock flock)
    if(_vixen_flock)
        execute_process(
            COMMAND "${_vixen_flock}" -n "${_vixen_vulkan_lock}" "${CMAKE_COMMAND}"
                "-DVIXEN_VULKAN_CACHE_DIR=${VIXEN_VULKAN_CACHE_DIR}"
                "-DVIXEN_VULKAN_SDK_VERSION=${VIXEN_VULKAN_SDK_VERSION}"
                "-DVIXEN_CONFIRM_UNINSTALL_VULKAN=ON"
                -DVIXEN_UNINSTALL_VULKAN_LOCKED=ON
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _vixen_flock_rc
            OUTPUT_VARIABLE _vixen_flock_output
            ERROR_VARIABLE _vixen_flock_error)
        if(NOT _vixen_flock_rc EQUAL 0)
            message(FATAL_ERROR
                "VIXEN: refusing to remove shared Vulkan SDK ${VIXEN_VULKAN_SDK_VERSION}; "
                "the cache entry lock is held (flock exit ${_vixen_flock_rc}).")
        endif()
        if(NOT "${_vixen_flock_output}" STREQUAL "")
            message("${_vixen_flock_output}")
        endif()
        if(NOT "${_vixen_flock_error}" STREQUAL "")
            message("${_vixen_flock_error}")
        endif()
        return()
    endif()
endif()

file(MAKE_DIRECTORY "${VIXEN_VULKAN_CACHE_DIR}")
file(LOCK "${_vixen_vulkan_lock}" GUARD PROCESS TIMEOUT 0
     RESULT_VARIABLE _vixen_uninstall_lock_rc)
if(NOT _vixen_uninstall_lock_rc EQUAL 0)
    message(FATAL_ERROR
        "VIXEN: refusing to remove shared Vulkan SDK ${VIXEN_VULKAN_SDK_VERSION}; "
        "the cache entry lock is held (${_vixen_uninstall_lock_rc}).")
endif()

if(EXISTS "${_vixen_vulkan_entry}")
    file(REMOVE_RECURSE "${_vixen_vulkan_entry}")
    message(STATUS
        "VIXEN: removed provisioned Vulkan SDK ${VIXEN_VULKAN_SDK_VERSION} "
        "from ${VIXEN_VULKAN_CACHE_DIR}")
else()
    message(STATUS
        "VIXEN: Vulkan SDK ${VIXEN_VULKAN_SDK_VERSION} is not present in "
        "${VIXEN_VULKAN_CACHE_DIR}")
endif()

file(LOCK "${_vixen_vulkan_lock}" RELEASE)
