# ============================================================================
# Third-Party Dependencies
# ============================================================================
# All FetchContent declarations and external library setup
# Note: This is included AFTER project() in the root CMakeLists.txt

include(FetchContent)

# Native loader dependencies are shared with VIXEN_NATIVE_DEPENDENCIES_ONLY.
add_subdirectory(native)

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
# STB - Uncompressed Image Loading (PNG/JPG)
# ============================================================================

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    # stb_image v2.30 tree; pin the exact source revision for reproducible decodes.
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(stb)

# Create interface library for STB (header-only)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE $<BUILD_INTERFACE:${stb_SOURCE_DIR}> $<INSTALL_INTERFACE:include>)
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
    target_include_directories(ProjectHash INTERFACE $<BUILD_INTERFACE:${OPENSSL_INCLUDE_DIR}>)
    target_compile_definitions(ProjectHash INTERFACE HAS_OPENSSL)
elseif(TARGET stbrumme_hash OR DEFINED stbrumme_hash_SOURCE_DIR)
    target_include_directories(ProjectHash INTERFACE $<BUILD_INTERFACE:${stbrumme_hash_SOURCE_DIR}> $<INSTALL_INTERFACE:include>)
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

# STB is header-only
include_directories(BEFORE ${stb_SOURCE_DIR})

# ============================================================================
# nlohmann/json - JSON for Modern C++
# ============================================================================

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# Don't build tests or install for nlohmann_json
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json)

# Organize in Dependencies folder
if(TARGET nlohmann_json)
    set_target_properties(nlohmann_json PROPERTIES FOLDER "Dependencies")
endif()

message(STATUS "nlohmann/json fetched - available to all libraries")

# ============================================================================
# miniz - Single-file ZIP/deflate compression library
# ============================================================================

FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.0.2
    GIT_SHALLOW    TRUE
)

# miniz builds as a library by default
FetchContent_MakeAvailable(miniz)

# Organize in Dependencies folder
if(TARGET miniz)
    set_target_properties(miniz PROPERTIES FOLDER "Dependencies")
    # Suppress warnings from miniz
    target_compile_options(miniz PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W0>
        $<$<CXX_COMPILER_ID:GNU>:-w>
        $<$<CXX_COMPILER_ID:Clang>:-w>
    )
endif()

message(STATUS "miniz fetched - ZIP compression available to all libraries")

# ============================================================================
# VulkanMemoryAllocator (VMA) - GPU Memory Management
# ============================================================================

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(VulkanMemoryAllocator)

# Organize in Dependencies folder
if(TARGET VulkanMemoryAllocator)
    set_target_properties(VulkanMemoryAllocator PROPERTIES FOLDER "Dependencies")
endif()

message(STATUS "VulkanMemoryAllocator (VMA) fetched - GPU memory management available")

# ============================================================================
# GLFW - cross-platform window + Vulkan surface + input (replaces raw Win32 windowing)
# ============================================================================
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
# Self-provision GLFW's Linux windowing backend deps (X11 dev libs) into a cache and enable the X11
# backend; if unavailable, falls back to the null backend so the build never breaks. No-op on
# Windows (Win32) / macOS (Cocoa). Override with -DGLFW_BUILD_X11 / -DVIXEN_AUTO_PROVISION_WINDOWING.
include(${VIXEN_ROOT}/cmake/ProvisionWindowingDeps.cmake)
FetchContent_MakeAvailable(glfw)
if(TARGET glfw)
    set_target_properties(glfw PROPERTIES FOLDER "Dependencies")
endif()
message(STATUS "GLFW fetched - cross-platform window/surface/input available")

# ============================================================================
# FreeType + RmlUi - data-driven, moddable UI (RML/RCSS). Self-contained: both
# build from source via FetchContent (no system packages, cross-platform). RmlUi's
# RML/RCSS is the surface a mod re-skins through the content pipeline; the Vulkan
# render backend (VK) covers basic-render + transforms (the shader/filter tier is
# GL3/DX12-only and not needed by the legibility-first UI).
# ============================================================================
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-2
    GIT_SHALLOW    TRUE)
# Drop FreeType's optional codecs so it needs nothing off-system.
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)
if(TARGET freetype)
    set_target_properties(freetype PROPERTIES FOLDER "Dependencies")
    target_compile_options(freetype PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W0>
        $<$<CXX_COMPILER_ID:GNU>:-w>
        $<$<CXX_COMPILER_ID:Clang>:-w>)
endif()

# Multi-channel signed-distance glyph generation. The renderer only needs the
# dependency-free core; font loading remains owned by the existing FreeType
# target above, and the atlas packing/upload lives in VIXEN's Text module.
FetchContent_Declare(msdfgen
    GIT_REPOSITORY https://github.com/Chlumsky/msdfgen.git
    GIT_TAG        v1.13
    GIT_SHALLOW    TRUE)
set(MSDFGEN_CORE_ONLY        ON  CACHE BOOL "Build only msdfgen core" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "Do not build the msdfgen CLI" FORCE)
set(MSDFGEN_USE_VCPKG        OFF CACHE BOOL "Use VIXEN FetchContent dependencies" FORCE)
set(MSDFGEN_INSTALL          OFF CACHE BOOL "Do not install msdfgen" FORCE)
set(MSDFGEN_USE_SKIA         OFF CACHE BOOL "Do not build the Skia extension" FORCE)
set(MSDFGEN_USE_OPENMP       OFF CACHE BOOL "Keep glyph generation single-threaded" FORCE)
if(MSVC)
    # VIXEN's Windows targets use the dynamic CRT (/MD). msdfgen defaults to
    # the static CRT (/MT), which makes any final executable linking Text fail
    # with LNK2038 even though msdfgen-core itself compiles successfully.
    set(MSDFGEN_DYNAMIC_RUNTIME ON CACHE BOOL
        "Use the same dynamic MSVC runtime as VIXEN" FORCE)
endif()
FetchContent_MakeAvailable(msdfgen)
if(TARGET msdfgen-core)
    set_target_properties(msdfgen-core PROPERTIES FOLDER "Dependencies")
    target_compile_options(msdfgen-core PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W0>
        $<$<CXX_COMPILER_ID:GNU>:-w>
        $<$<CXX_COMPILER_ID:Clang>:-w>)
endif()
# RmlUi locates FreeType by the find_package target name Freetype::Freetype; FetchContent only
# provides the lowercase `freetype` target, so alias it before RmlUi is configured below.
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

# Text shaping is a VIXEN responsibility. FreeType deliberately has its own
# HarfBuzz integration disabled above so this stays the only shaping path.
set(HB_HAVE_FREETYPE ON CACHE BOOL "Enable HarfBuzz's FreeType font adapter" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "Keep ICU out of the VIXEN text runtime" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "Do not build HarfBuzz command-line tools" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "Do not build HarfBuzz subset tooling" FORCE)
set(HB_BUILD_RASTER OFF CACHE BOOL "Use VIXEN's own glyph atlas" FORCE)
set(HB_BUILD_VECTOR OFF CACHE BOOL "Do not build HarfBuzz vector output" FORCE)
set(HB_BUILD_GPU OFF CACHE BOOL "Do not build HarfBuzz GPU helpers" FORCE)
FetchContent_Declare(harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG        14.5.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(harfbuzz)
if(TARGET harfbuzz)
    set_target_properties(harfbuzz PROPERTIES FOLDER "Dependencies")
    target_compile_options(harfbuzz PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W0>
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<CXX_COMPILER_ID:GNU>:-w>
        $<$<C_COMPILER_ID:GNU>:-w>
        $<$<CXX_COMPILER_ID:Clang>:-w>
        $<$<C_COMPILER_ID:Clang>:-w>)
endif()
message(STATUS "HarfBuzz + FreeType configured for text shaping (MIT-style license)")

# SheenBidi implements UAX #9 and returns visual runs that VIXEN shapes with
# HarfBuzz. Its standalone tests and generator are not part of the product build.
set(SB_CONFIG_EXPERIMENTAL_TEXT_API OFF CACHE BOOL "Use the stable SheenBidi API" FORCE)
set(SB_CONFIG_UNITY ON CACHE BOOL "Build SheenBidi as its upstream unity source" FORCE)
set(BUILD_GENERATOR OFF CACHE BOOL "Do not build SheenBidi's Unicode generator" FORCE)
FetchContent_Declare(sheenbidi
    GIT_REPOSITORY https://github.com/Tehreer/SheenBidi.git
    GIT_TAG        v3.0.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(sheenbidi)
if(TARGET SheenBidi)
    set_target_properties(SheenBidi PROPERTIES FOLDER "Dependencies")
    target_compile_options(SheenBidi PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<C_COMPILER_ID:GNU>:-w>
        $<$<C_COMPILER_ID:Clang>:-w>)
endif()
message(STATUS "SheenBidi configured for Unicode bidirectional runs (Apache-2.0 license)")

# libunibreak has no CMake project. Fetch its tagged source with FetchContent's
# explicit-population form and build the same source list as upstream's
# Makefile.gcc; the boundary owns the language class and this provides UAX #14/#29.
FetchContent_Populate(libunibreak
    GIT_REPOSITORY https://github.com/adah1972/libunibreak.git
    GIT_TAG        libunibreak_5_1
    GIT_SHALLOW    TRUE)
# Read CFILES from the pinned upstream manifest so a tag bump cannot silently drop or add a file.
file(READ ${libunibreak_SOURCE_DIR}/src/Makefile.gcc _unibreak_makefile)
string(REPLACE "\\\n" " " _unibreak_makefile "${_unibreak_makefile}")
string(REGEX MATCH "CFILES[ \t]*:=[ \t]*([^\n]*)" _unibreak_cfiles_match "${_unibreak_makefile}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "libunibreak: CFILES not found in ${libunibreak_SOURCE_DIR}/src/Makefile.gcc")
endif()
separate_arguments(_unibreak_cfiles UNIX_COMMAND "${CMAKE_MATCH_1}")
list(TRANSFORM _unibreak_cfiles PREPEND "${libunibreak_SOURCE_DIR}/src/")
add_library(VixenLibunibreak STATIC ${_unibreak_cfiles})
unset(_unibreak_makefile)
unset(_unibreak_cfiles_match)
unset(_unibreak_cfiles)
target_include_directories(VixenLibunibreak PUBLIC ${libunibreak_SOURCE_DIR}/src)
set_target_properties(VixenLibunibreak PROPERTIES FOLDER "Dependencies")
target_compile_options(VixenLibunibreak PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W0>
    $<$<C_COMPILER_ID:GNU>:-w>
    $<$<C_COMPILER_ID:Clang>:-w>)
message(STATUS "libunibreak configured for Unicode line breaking (zlib license)")

FetchContent_Declare(rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.3
    GIT_SHALLOW    TRUE)
set(RMLUI_SAMPLES        OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS   OFF CACHE BOOL "" FORCE)
# Suppress RmlUi's own test suite without permanently clobbering the global
# BUILD_TESTING cache var: the root CMakeLists.txt forces it ON (for VIXEN's
# own libraries, e.g. GaiaVoxelWorld/GaiaArchetypes, which gate on the
# CTest-standard BUILD_TESTING rather than the project's BUILD_TESTS option)
# before this file runs, and CACHE FORCE writes here are global, not scoped
# to the RmlUi fetch.
set(_vixen_build_testing_saved ${BUILD_TESTING})
set(BUILD_TESTING        OFF CACHE BOOL "" FORCE)
# Hand RmlUi the FetchContent FreeType target rather than a system find_package.
set(RMLUI_FONT_ENGINE    none CACHE STRING "" FORCE)
FetchContent_MakeAvailable(rmlui)
set(BUILD_TESTING ${_vixen_build_testing_saved} CACHE BOOL "" FORCE)
unset(_vixen_build_testing_saved)
foreach(_rml RmlCore RmlUi RmlDebugger rmlui_core)
    if(TARGET ${_rml})
        set_target_properties(${_rml} PROPERTIES FOLDER "Dependencies")
        # Suppress warnings compiling RmlUi's OWN translation units (-Warray-bounds= et al,
        # e.g. Context.cpp/ElementForm.cpp/WidgetDropDown.cpp/WidgetSlider.cpp via inlined
        # libstdc++ char_traits through itlib::flat_map). A SYSTEM-include mark (the gaia
        # pattern, perf/fedwarn-vixen1-report.md §2) only suppresses warnings for CONSUMERS
        # including RmlUi's headers -- it does nothing here because these warnings fire while
        # compiling rmlui_core's own .cpp files, not a downstream include. Matches the
        # freetype PRIVATE -w treatment two blocks above -- same third-party exemption,
        # target-scoped since this is the library's own build, not an include leak.
        target_compile_options(${_rml} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W0>
            $<$<CXX_COMPILER_ID:GNU>:-w>
            $<$<CXX_COMPILER_ID:Clang>:-w>)
    endif()
endforeach()
message(STATUS "RmlUi + FreeType fetched - moddable UI available")

message(STATUS "Dependencies configured - all third-party libraries fetched")
