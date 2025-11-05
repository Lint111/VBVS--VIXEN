#
# Fetch Vulkan headers if Vulkan SDK not found
# This allows building tests without full Vulkan SDK installation
#

# Try to find Vulkan first
find_package(Vulkan QUIET)

if(Vulkan_FOUND)
    message(STATUS "Found Vulkan SDK: ${Vulkan_INCLUDE_DIRS}")
    # Use system Vulkan
    set(VULKAN_HEADERS_INCLUDE_DIR ${Vulkan_INCLUDE_DIRS})
else()
    message(STATUS "Vulkan SDK not found, fetching Vulkan-Headers from GitHub...")

    include(FetchContent)

    # Fetch official Vulkan-Headers (header-only)
    FetchContent_Declare(
        VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.290  # Latest stable as of Nov 2024
        GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(VulkanHeaders)

    # Set include directory
    FetchContent_GetProperties(VulkanHeaders SOURCE_DIR VULKAN_HEADERS_SOURCE_DIR)
    set(VULKAN_HEADERS_INCLUDE_DIR "${VULKAN_HEADERS_SOURCE_DIR}/include")

    message(STATUS "Vulkan headers fetched to: ${VULKAN_HEADERS_INCLUDE_DIR}")

    # Create imported target for compatibility
    if(NOT TARGET Vulkan::Headers)
        add_library(Vulkan::Headers INTERFACE IMPORTED)
        set_target_properties(Vulkan::Headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_HEADERS_INCLUDE_DIR}"
        )
    endif()
endif()

# Expose to parent scope (if possible)
if(CMAKE_PARENT_LIST_FILE)
    set(VULKAN_HEADERS_AVAILABLE TRUE PARENT_SCOPE)
    set(VULKAN_HEADERS_INCLUDE_DIR "${VULKAN_HEADERS_INCLUDE_DIR}" PARENT_SCOPE)
endif()

# Also set in current scope
set(VULKAN_HEADERS_AVAILABLE TRUE)

message(STATUS "Vulkan headers available at: ${VULKAN_HEADERS_INCLUDE_DIR}")
