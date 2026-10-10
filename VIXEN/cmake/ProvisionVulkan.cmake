# ProvisionVulkan.cmake
#
# Self-contained Vulkan provisioning. A consumer should only ever build VIXEN — never install the
# Vulkan SDK by hand. When no SDK is found, this downloads the prebuilt LunarG SDK (headers +
# loader + validation layers) into a persistent, gitignored cache and synthesises Vulkan::Vulkan,
# so everything downstream (find_package(Vulkan), Vulkan::Vulkan) works unchanged.
#
# Validation layers are gated by build type (Debug = on, Release = off) and surfaced as the
# VIXEN_VULKAN_VALIDATION compile symbol + a clear status line, so the active mode is obvious in
# the build output and to runtime code.
#
# Knobs:
#   -DVIXEN_AUTO_PROVISION_VULKAN=OFF   require a system SDK instead of auto-downloading
#   VIXEN_VULKAN_SDK_VERSION=X.Y.Z.W   expected SDK version; platform defaults and capability floors
#                                      are shared in cmake/vulkan-sdk-settings.env
#   -DVIXEN_VULKAN_CACHE_DIR=<path>     where the provisioned SDK lives (default <src>/.vulkan-sdk)
#   -DVIXEN_VULKAN_VALIDATION=ON|OFF    force validation on/off (default: from build type)
#   -DVIXEN_UNINSTALL_VULKAN=ON         remove the provisioned cache and stop (the uninstall flag)
#   build target  vixen-uninstall-vulkan   same, as an explicit build step

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/VulkanSdkSettings.cmake")

option(VIXEN_AUTO_PROVISION_VULKAN "Download a prebuilt Vulkan SDK into a cache when none is found" ON)
set(VIXEN_VULKAN_CACHE_DIR "${VIXEN_ROOT}/.vulkan-sdk" CACHE PATH "Persistent Vulkan provisioning cache")
option(VIXEN_UNINSTALL_VULKAN "Remove the provisioned Vulkan cache and stop" OFF)

function(_vixen_sdk_version_from_root _root _out)
    set(_version "")
    if(_root)
        get_filename_component(_leaf "${_root}" NAME)
        if(_leaf MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
            set(_version "${_leaf}")
        else()
            get_filename_component(_parent "${_root}" DIRECTORY)
            get_filename_component(_parent_leaf "${_parent}" NAME)
            if(_parent_leaf MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
                set(_version "${_parent_leaf}")
            endif()
        endif()
    endif()
    set(${_out} "${_version}" PARENT_SCOPE)
endfunction()

function(_vixen_sdk_same_release _actual _expected _out)
    set(_same FALSE)
    if(_actual MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$" AND
       _expected MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
        string(REPLACE "." ";" _actual_parts "${_actual}")
        string(REPLACE "." ";" _expected_parts "${_expected}")
        list(SUBLIST _actual_parts 0 3 _actual_release)
        list(SUBLIST _expected_parts 0 3 _expected_release)
        if(_actual_release STREQUAL _expected_release)
            set(_same TRUE)
        endif()
    endif()
    set(${_out} "${_same}" PARENT_SCOPE)
endfunction()

# Build-type-gated validation (Debug or any multi-config generator => on; Release => off), overridable.
if(NOT DEFINED VIXEN_VULKAN_VALIDATION)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
        set(VIXEN_VULKAN_VALIDATION ON)
    else()
        set(VIXEN_VULKAN_VALIDATION OFF)
    endif()
endif()

# --- uninstall (flag form; the target form is defined at the bottom) ---
if(VIXEN_UNINSTALL_VULKAN)
    if(EXISTS "${VIXEN_VULKAN_CACHE_DIR}")
        file(REMOVE_RECURSE "${VIXEN_VULKAN_CACHE_DIR}")
        message(STATUS "VIXEN: removed provisioned Vulkan cache: ${VIXEN_VULKAN_CACHE_DIR}")
    endif()
    message(FATAL_ERROR "VIXEN: Vulkan uninstalled. Re-run cmake without -DVIXEN_UNINSTALL_VULKAN=ON.")
endif()

# --- locate, or auto-provision ---
# Precedence: an already-installed Vulkan (system package or LunarG SDK via VULKAN_SDK) wins; only
# if nothing is found do we touch the project-local cache; only if the cache is empty do we download.
# We never install Vulkan system-wide — provisioning is a project-local cache.
# FindVulkan locates the LunarG 1.4.350 Linux loader under lib/VulkanLoader/lib rather than the
# older lib/ root. Resolve the externally selected SDK's paths before the initial package lookup,
# just as the auto-provisioned cache path does below, so a caller can validate/use an installed SDK.
if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}")
    set(_vixen_external_sdk_root "$ENV{VULKAN_SDK}")
    if(WIN32)
        set(_vixen_external_sdk_include "${_vixen_external_sdk_root}/Include")
        unset(_vixen_external_sdk_loader CACHE)
        find_library(_vixen_external_sdk_loader
            NAMES vulkan-1
            PATHS "${_vixen_external_sdk_root}/Lib"
            NO_DEFAULT_PATH)
    else()
        set(_vixen_external_sdk_include "${_vixen_external_sdk_root}/include")
        unset(_vixen_external_sdk_loader CACHE)
        find_library(_vixen_external_sdk_loader
            NAMES vulkan libvulkan
            PATHS "${_vixen_external_sdk_root}/lib"
                  "${_vixen_external_sdk_root}/lib/VulkanLoader/lib"
            NO_DEFAULT_PATH)
    endif()
    if(EXISTS "${_vixen_external_sdk_include}/vulkan/vulkan.h")
        set(Vulkan_INCLUDE_DIR "${_vixen_external_sdk_include}" CACHE PATH
            "Vulkan include directory from VULKAN_SDK" FORCE)
    endif()
    if(_vixen_external_sdk_loader)
        set(Vulkan_LIBRARY "${_vixen_external_sdk_loader}" CACHE FILEPATH
            "Vulkan loader from VULKAN_SDK" FORCE)
        message(STATUS "VIXEN: external Vulkan SDK loader: ${_vixen_external_sdk_loader}")
    endif()
endif()
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}")
        set(_vixen_found_sdk_root "$ENV{VULKAN_SDK}")
    else()
        if(Vulkan_INCLUDE_DIRS)
            list(GET Vulkan_INCLUDE_DIRS 0 _vixen_found_include)
        else()
            set(_vixen_found_include "${Vulkan_INCLUDE_DIR}")
        endif()
        if(_vixen_found_include MATCHES "/[Ii]nclude/?$")
            get_filename_component(_vixen_found_sdk_root "${_vixen_found_include}" DIRECTORY)
        else()
            set(_vixen_found_sdk_root "${_vixen_found_include}")
        endif()
    endif()
    _vixen_sdk_version_from_root("${_vixen_found_sdk_root}" _vixen_found_sdk_version)

    if(_vixen_found_sdk_version)
        _vixen_sdk_same_release("${_vixen_found_sdk_version}" "${VIXEN_VULKAN_SDK_VERSION}"
                                _vixen_sdk_release_compatible)
        if(_vixen_sdk_release_compatible)
            set(VIXEN_VULKAN_SDK_ROOT "${_vixen_found_sdk_root}")
            set(VIXEN_VULKAN_ACTUAL_SDK_VERSION "${_vixen_found_sdk_version}")
            if(_vixen_found_sdk_version STREQUAL VIXEN_VULKAN_SDK_VERSION)
                message(STATUS "VIXEN: found Vulkan SDK ${_vixen_found_sdk_version} — expected version matches.")
            else()
                message(STATUS "VIXEN: Vulkan SDK patch variation accepted: expected ${VIXEN_VULKAN_SDK_VERSION}, "
                               "found ${_vixen_found_sdk_version}; validating API and tool capabilities.")
            endif()
        elseif(VIXEN_AUTO_PROVISION_VULKAN AND NOT WIN32)
            message(STATUS "VIXEN: installed Vulkan SDK ${_vixen_found_sdk_version} does not match expected "
                           "${VIXEN_VULKAN_SDK_VERSION}; provisioning the declared version.")
            set(Vulkan_FOUND FALSE)
        else()
            message(FATAL_ERROR
                "VIXEN: Vulkan SDK version mismatch: expected ${VIXEN_VULKAN_SDK_VERSION}, "
                "found ${_vixen_found_sdk_version} at ${_vixen_found_sdk_root}. Set the process environment "
                "variable VIXEN_VULKAN_SDK_VERSION to the SDK version you intend to use, then rerun configure.")
        endif()
    else()
        set(VIXEN_VULKAN_SDK_ROOT "${_vixen_found_sdk_root}")
        set(VIXEN_VULKAN_ACTUAL_SDK_VERSION "unknown")
        message(STATUS "VIXEN: found Vulkan headers at ${Vulkan_INCLUDE_DIRS}; SDK package version is not encoded "
                       "in this system package, so validating its API and tool capabilities against the shared floors.")
    endif()
endif()

if(NOT Vulkan_FOUND AND VIXEN_AUTO_PROVISION_VULKAN)
    if(WIN32)
        message(FATAL_ERROR
            "VIXEN: Vulkan SDK ${VIXEN_VULKAN_SDK_VERSION} is missing. Run the existing Windows provisioning "
            "entry point first: build.bat provision (environment override: VIXEN_VULKAN_SDK_VERSION).")
    endif()

    set(_vk_root "${VIXEN_VULKAN_CACHE_DIR}/${VIXEN_VULKAN_SDK_VERSION}/x86_64")

    if(NOT EXISTS "${_vk_root}/include/vulkan/vulkan.h")
        set(_tarball "${VIXEN_VULKAN_CACHE_DIR}/vulkansdk-linux-x86_64-${VIXEN_VULKAN_SDK_VERSION}.tar.xz")
        set(_url "https://sdk.lunarg.com/sdk/download/${VIXEN_VULKAN_SDK_VERSION}/linux/vulkansdk-linux-x86_64-${VIXEN_VULKAN_SDK_VERSION}.tar.xz")
        file(MAKE_DIRECTORY "${VIXEN_VULKAN_CACHE_DIR}")
        if(NOT EXISTS "${_tarball}")
            message(STATUS "VIXEN: Vulkan SDK not found — downloading prebuilt LunarG SDK ${VIXEN_VULKAN_SDK_VERSION} (~320 MB, one-time, cached) ...")
            file(DOWNLOAD "${_url}" "${_tarball}" SHOW_PROGRESS TLS_VERIFY ON STATUS _dl)
            list(GET _dl 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE "${_tarball}")
                message(FATAL_ERROR "VIXEN: Vulkan SDK download failed (${_dl}). URL: ${_url}")
            endif()
        endif()
        message(STATUS "VIXEN: extracting Vulkan SDK into ${VIXEN_VULKAN_CACHE_DIR} ...")
        file(ARCHIVE_EXTRACT INPUT "${_tarball}" DESTINATION "${VIXEN_VULKAN_CACHE_DIR}")
    else()
        message(STATUS "VIXEN: reusing cached Vulkan SDK at ${_vk_root} (no download).")
    endif()

    if(EXISTS "${_vk_root}/include/vulkan/vulkan.h")
        # Point FindVulkan at the cached SDK and re-discover so Vulkan::Vulkan + the SDK tools resolve.
        set(ENV{VULKAN_SDK} "${_vk_root}")
        set(Vulkan_INCLUDE_DIR "${_vk_root}/include" CACHE PATH "" FORCE)
        # The loader location varies by SDK packaging: older SDKs ship it at lib/libvulkan.so,
        # newer ones (e.g. 1.4.350.1) under lib/VulkanLoader/lib/libvulkan.so. Resolve the real
        # path instead of assuming one layout, so the linker gets a file that actually exists.
        find_library(_vk_loader
            NAMES vulkan libvulkan
            PATHS "${_vk_root}/lib" "${_vk_root}/lib/VulkanLoader/lib"
            NO_DEFAULT_PATH)
        if(_vk_loader)
            set(Vulkan_LIBRARY "${_vk_loader}" CACHE FILEPATH "" FORCE)
        else()
            set(Vulkan_LIBRARY "${_vk_root}/lib/libvulkan.so" CACHE FILEPATH "" FORCE)
        endif()
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            set(VIXEN_VULKAN_SDK_ROOT "${_vk_root}")
            set(VIXEN_VULKAN_ACTUAL_SDK_VERSION "${VIXEN_VULKAN_SDK_VERSION}")
            set(VIXEN_VULKAN_LAYER_PATH "${_vk_root}/share/vulkan/explicit_layer.d"
                CACHE PATH "Provisioned Vulkan validation-layer manifests" FORCE)
            message(STATUS "VIXEN: using auto-provisioned Vulkan SDK ${VIXEN_VULKAN_ACTUAL_SDK_VERSION} at ${_vk_root}")
        endif()
    endif()
endif()

if(NOT Vulkan_FOUND)
    message(FATAL_ERROR
        "VIXEN: Vulkan not found and could not be auto-provisioned.\n"
        "  - Ensure network access, or install the Vulkan SDK, or\n"
        "  - re-run with -DVIXEN_AUTO_PROVISION_VULKAN=ON (default).")
endif()

# Static version capability checks use the same floors exported to CapabilityGraph. Vulkan API
# support comes from the SDK headers; the device's API version is supplied to the graph at runtime.
if(Vulkan_INCLUDE_DIRS)
    list(GET Vulkan_INCLUDE_DIRS 0 _vixen_vulkan_include_dir)
else()
    set(_vixen_vulkan_include_dir "${Vulkan_INCLUDE_DIR}")
endif()
set(_vixen_vulkan_core_header "${_vixen_vulkan_include_dir}/vulkan/vulkan_core.h")
if(NOT EXISTS "${_vixen_vulkan_core_header}")
    message(FATAL_ERROR "VIXEN: cannot inspect Vulkan API version; missing ${_vixen_vulkan_core_header}.")
endif()
file(READ "${_vixen_vulkan_core_header}" _vixen_vulkan_core_contents)
string(REGEX MATCH
    "#[ \t]*define[ \t]+VK_HEADER_VERSION_COMPLETE[ \t]+VK_MAKE_API_VERSION\\(0,[ \t]*([0-9]+),[ \t]*([0-9]+),"
    _vixen_api_macro "${_vixen_vulkan_core_contents}")
if(NOT _vixen_api_macro)
    message(FATAL_ERROR
        "VIXEN: could not read the Vulkan API version from ${_vixen_vulkan_core_header}; "
        "required capability VulkanApi>=${VIXEN_VULKAN_MIN_API_VERSION} has no capability-independent twin.")
endif()
set(VIXEN_VULKAN_ACTUAL_API_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.0")
if(VIXEN_VULKAN_ACTUAL_API_VERSION VERSION_LESS VIXEN_VULKAN_MIN_API_VERSION)
    message(FATAL_ERROR
        "VIXEN: required Vulkan capability unavailable: the SDK headers expose API "
        "${VIXEN_VULKAN_MIN_API_VERSION}, but the provisioned SDK headers expose ${VIXEN_VULKAN_ACTUAL_API_VERSION}. "
        "The renderer has no capability-independent twin for Vulkan API ${VIXEN_VULKAN_MIN_API_VERSION}. "
        "Set VIXEN_VULKAN_SDK_VERSION to a compatible "
        "SDK version and rerun configure.")
endif()
if(VIXEN_VULKAN_ACTUAL_API_VERSION VERSION_LESS VIXEN_VULKAN_SYNCHRONIZATION2_CORE_VERSION AND
   NOT _vixen_vulkan_core_contents MATCHES "#[ \t]*define[ \t]+VK_KHR_synchronization2([ \t]|$)")
    message(FATAL_ERROR
        "VIXEN: the provisioned Vulkan SDK exposes API ${VIXEN_VULKAN_ACTUAL_API_VERSION} but not "
        "VK_KHR_synchronization2. The renderer requires synchronization2; this requirement has no "
        "capability-independent twin. Set VIXEN_VULKAN_SDK_VERSION to an SDK with the extension or Vulkan 1.3 core.")
endif()

if(WIN32)
    set(_vixen_glslang_names glslangValidator.exe glslangValidator)
    set(_vixen_glslang_hints "${VIXEN_VULKAN_SDK_ROOT}/Bin" "${VIXEN_VULKAN_SDK_ROOT}/bin")
else()
    set(_vixen_glslang_names glslangValidator)
    set(_vixen_glslang_hints "${VIXEN_VULKAN_SDK_ROOT}/bin")
endif()
unset(_vixen_glslang_validator CACHE)
find_program(_vixen_glslang_validator NAMES ${_vixen_glslang_names}
             HINTS ${_vixen_glslang_hints})
if(NOT _vixen_glslang_validator)
    message(FATAL_ERROR
        "VIXEN: glslangValidator was not found in the provisioned Vulkan SDK or PATH. "
        "Shader compilation has no capability-independent twin; install the declared SDK "
        "${VIXEN_VULKAN_SDK_VERSION} or set VIXEN_VULKAN_SDK_VERSION to a compatible SDK.")
endif()
execute_process(COMMAND "${_vixen_glslang_validator}" --version
    RESULT_VARIABLE _vixen_glslang_result
    OUTPUT_VARIABLE _vixen_glslang_output
    ERROR_VARIABLE _vixen_glslang_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _vixen_glslang_result EQUAL 0 OR
   NOT _vixen_glslang_output MATCHES "Glslang Version: ([0-9]+:)?([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR
        "VIXEN: could not read the glslang version from ${_vixen_glslang_validator}. "
        "Output: ${_vixen_glslang_output}${_vixen_glslang_error}")
endif()
set(VIXEN_GLSLANG_VERSION "${CMAKE_MATCH_2}")
if(NOT _vixen_glslang_output MATCHES "SPIR-V Version 0x([0-9A-Fa-f]+)")
    message(FATAL_ERROR "VIXEN: could not read glslang's SPIR-V target from ${_vixen_glslang_validator}.")
endif()
set(_vixen_spirv_hex "${CMAKE_MATCH_1}")
math(EXPR _vixen_spirv_major "(0x${_vixen_spirv_hex} >> 16) & 255")
math(EXPR _vixen_spirv_minor "(0x${_vixen_spirv_hex} >> 8) & 255")
set(VIXEN_SPIRV_TARGET_VERSION "${_vixen_spirv_major}.${_vixen_spirv_minor}.0")
if(VIXEN_SPIRV_TARGET_VERSION VERSION_LESS VIXEN_VULKAN_MIN_SPIRV_TARGET_VERSION)
    message(STATUS
        "[VulkanCapability] Fallback in use: RayQueryLighting needs SPIR-V target "
        "${VIXEN_VULKAN_MIN_SPIRV_TARGET_VERSION}, but glslang provides ${VIXEN_SPIRV_TARGET_VERSION}; "
        "using CapabilityIndependent.")
else()
    message(STATUS "[VulkanCapability] RayQueryLighting SPIR-V requirement available: "
                   "${VIXEN_SPIRV_TARGET_VERSION} >= ${VIXEN_VULKAN_MIN_SPIRV_TARGET_VERSION}.")
endif()

message(STATUS "VIXEN: Vulkan toolchain — expected SDK ${VIXEN_VULKAN_SDK_VERSION}; "
               "found SDK ${VIXEN_VULKAN_ACTUAL_SDK_VERSION}; API headers ${VIXEN_VULKAN_ACTUAL_API_VERSION}; "
               "glslang ${VIXEN_GLSLANG_VERSION}; SPIR-V target ${VIXEN_SPIRV_TARGET_VERSION}.")

set(_vixen_vulkan_capability_header "${CMAKE_BINARY_DIR}/generated/VulkanCapabilityConfig.h")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_LIST_DIR}/VulkanCapabilityConfig.h.in"
               "${_vixen_vulkan_capability_header}" @ONLY)

# --- clear, build-type-aware validation symbol + status ---
if(VIXEN_VULKAN_VALIDATION)
    add_compile_definitions(VIXEN_VULKAN_VALIDATION=1)
    # Self-contained validation: pass the provisioned validation-layer manifest dir to the app so it can
    # point the Vulkan loader at it (VK_LAYER_PATH) at startup — no manual env, works on GCC/WSL where the
    # SDK was auto-provisioned. Empty when Vulkan came from the system (the loader already knows its layers).
    if(VIXEN_VULKAN_LAYER_PATH)
        add_compile_definitions(VIXEN_VK_LAYER_PATH="${VIXEN_VULKAN_LAYER_PATH}")
    endif()
else()
    add_compile_definitions(VIXEN_VULKAN_VALIDATION=0)
endif()
message(STATUS "VIXEN: Vulkan validation layers: ${VIXEN_VULKAN_VALIDATION} "
        "(build type: ${CMAKE_BUILD_TYPE}; layer path: ${VIXEN_VULKAN_LAYER_PATH})")

# --- uninstall as an explicit build step ---
if(NOT TARGET vixen-uninstall-vulkan)
    add_custom_target(vixen-uninstall-vulkan
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIXEN_VULKAN_CACHE_DIR}"
        COMMENT "VIXEN: removing provisioned Vulkan cache (${VIXEN_VULKAN_CACHE_DIR})")
endif()
