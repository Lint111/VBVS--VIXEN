# ============================================================================
# Third-Party Dependencies
# ============================================================================
# All FetchContent declarations and external library setup
# Note: This is included AFTER project() in the root CMakeLists.txt

include(FetchContent)

# ============================================================================
# GLM - OpenGL Mathematics Library
# ============================================================================

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glm)

# Suppress warnings from glm
if(TARGET glm)
    target_compile_options(glm INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/W0>
        $<$<CXX_COMPILER_ID:GNU>:-w>
        $<$<CXX_COMPILER_ID:Clang>:-w>
    )
    set_target_properties(glm PROPERTIES FOLDER "Dependencies")
endif()

# ============================================================================
# Vulkan Headers (if not provided by SDK)
# ============================================================================

if(NOT TARGET Vulkan::Headers)
    FetchContent_Declare(
        VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(VulkanHeaders)
endif()

# ============================================================================
# GLI - Compressed Texture Loading (DDS/KTX)
# ============================================================================

set(GLI_TEST_ENABLE OFF CACHE BOOL "" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)  # Skip git update - use cached version
FetchContent_Declare(
    gli
    GIT_REPOSITORY https://github.com/g-truc/gli.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(gli)

# Suppress warnings from gli
if(TARGET gli)
    target_compile_options(gli INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/W0>
        $<$<CXX_COMPILER_ID:GNU>:-w>
        $<$<CXX_COMPILER_ID:Clang>:-w>
    )
    set_target_properties(gli PROPERTIES FOLDER "Dependencies")
endif()

# ============================================================================
# STB - Uncompressed Image Loading (PNG/JPG)
# ============================================================================

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(stb)

# Create interface library for STB (header-only)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
set_target_properties(stb PROPERTIES FOLDER "Dependencies")

# ============================================================================
# Hash Provider (OpenSSL or header-only fallback)
# ============================================================================

find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found - enabling global SHA256 support")
    add_compile_definitions(HAS_OPENSSL)
    include_directories(BEFORE ${OPENSSL_INCLUDE_DIR})
else()
    message(STATUS "OpenSSL not found - fetching header-only stbrumme/hash-library as fallback for SHA256")
    FetchContent_Declare(
        stbrumme_hash
        GIT_REPOSITORY https://github.com/stbrumme/hash-library.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(stbrumme_hash)
    add_compile_definitions(HAS_STBRUMME_HASH)
    include_directories(BEFORE ${stbrumme_hash_SOURCE_DIR})
endif()

# Project-level hash provider interface
add_library(ProjectHash INTERFACE)
if(OpenSSL_FOUND)
    target_link_libraries(ProjectHash INTERFACE OpenSSL::Crypto)
    target_include_directories(ProjectHash INTERFACE ${OPENSSL_INCLUDE_DIR})
    target_compile_definitions(ProjectHash INTERFACE HAS_OPENSSL)
elseif(TARGET stbrumme_hash OR DEFINED stbrumme_hash_SOURCE_DIR)
    target_include_directories(ProjectHash INTERFACE ${stbrumme_hash_SOURCE_DIR})
    target_compile_definitions(ProjectHash INTERFACE HAS_STBRUMME_HASH)
else()
    target_compile_definitions(ProjectHash INTERFACE HAS_HASH_FALLBACK)
endif()
set_target_properties(ProjectHash PROPERTIES FOLDER "Dependencies")

# ============================================================================
# GoogleTest - Testing Framework
# ============================================================================

if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # Don't install GTest when installing our project
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    # Create GTest:: namespace aliases for compatibility
    if(TARGET gtest AND NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
    endif()
    if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    if(TARGET gmock AND NOT TARGET GTest::gmock)
        add_library(GTest::gmock ALIAS gmock)
    endif()
    if(TARGET gmock_main AND NOT TARGET GTest::gmock_main)
        add_library(GTest::gmock_main ALIAS gmock_main)
    endif()

    # Organize GTest targets in Dependencies folder
    set_target_properties(gtest gtest_main gmock gmock_main PROPERTIES FOLDER "Dependencies")

    message(STATUS "GoogleTest fetched - test targets available to subdirectories")
endif()

# ============================================================================
# Setup Include Directories for Header-Only Libraries
# ============================================================================

# Vulkan headers include path (only when fetched)
if(DEFINED vulkanheaders_SOURCE_DIR)
    include_directories(BEFORE ${vulkanheaders_SOURCE_DIR}/include)
endif()

# GLI is header-only
include_directories(BEFORE ${gli_SOURCE_DIR})

# STB is header-only
include_directories(BEFORE ${stb_SOURCE_DIR})

message(STATUS "Dependencies configured - all third-party libraries fetched")
