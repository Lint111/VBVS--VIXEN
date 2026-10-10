# Resolve the declared Vulkan SDK setting and the shared capability floors.
# The plain key/value file is also read by the Windows and WSL provisioning
# entry points, which run before or outside CMake.
include_guard(GLOBAL)

set(VIXEN_VULKAN_SETTINGS_FILE "${CMAKE_CURRENT_LIST_DIR}/vulkan-sdk-settings.env")
if(NOT EXISTS "${VIXEN_VULKAN_SETTINGS_FILE}")
    message(FATAL_ERROR "VIXEN Vulkan settings are missing: ${VIXEN_VULKAN_SETTINGS_FILE}")
endif()

file(STRINGS "${VIXEN_VULKAN_SETTINGS_FILE}" _vixen_vulkan_setting_lines
     REGEX "^[A-Z0-9_]+=")
foreach(_vixen_vulkan_setting_line IN LISTS _vixen_vulkan_setting_lines)
    if(_vixen_vulkan_setting_line MATCHES "^([A-Z0-9_]+)=(.*)$")
        set("${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
    endif()
endforeach()
unset(_vixen_vulkan_setting_line)
unset(_vixen_vulkan_setting_lines)

if(WIN32)
    set(_vixen_vulkan_sdk_default "${VIXEN_VULKAN_SDK_VERSION_WINDOWS_DEFAULT}")
elseif(UNIX AND EXISTS "/dev/dxg")
    set(_vixen_vulkan_sdk_default "${VIXEN_VULKAN_SDK_VERSION_WSL_DEFAULT}")
elseif(UNIX)
    set(_vixen_vulkan_sdk_default "${VIXEN_VULKAN_SDK_VERSION_OTHER_DEFAULT}")
else()
    set(_vixen_vulkan_sdk_default "${VIXEN_VULKAN_SDK_VERSION_OTHER_DEFAULT}")
endif()

if(DEFINED ENV{VIXEN_VULKAN_SDK_VERSION} AND NOT "$ENV{VIXEN_VULKAN_SDK_VERSION}" STREQUAL "")
    set(VIXEN_VULKAN_SDK_VERSION "$ENV{VIXEN_VULKAN_SDK_VERSION}" CACHE STRING
        "Expected Vulkan SDK version; environment override is VIXEN_VULKAN_SDK_VERSION" FORCE)
elseif(NOT DEFINED VIXEN_VULKAN_SDK_VERSION OR VIXEN_VULKAN_SDK_VERSION STREQUAL "")
    set(VIXEN_VULKAN_SDK_VERSION "${_vixen_vulkan_sdk_default}" CACHE STRING
        "Expected Vulkan SDK version; environment override is VIXEN_VULKAN_SDK_VERSION")
endif()

message(STATUS "VIXEN: expected Vulkan SDK ${VIXEN_VULKAN_SDK_VERSION} "
               "(environment override: VIXEN_VULKAN_SDK_VERSION; shared settings: ${VIXEN_VULKAN_SETTINGS_FILE})")

unset(_vixen_vulkan_sdk_default)
