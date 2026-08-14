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
# Gaia ECS - Entity Component System
# ============================================================================

FetchContent_Declare(
    gaia
    GIT_REPOSITORY https://github.com/Lint111/gaia-ecs.git
    # Pinned to a specific commit rather than a moving ref.
    # GIT_TAG main previously froze at whatever commit each build dir first fetched
    # (FetchContent caches _deps and does not re-fetch on reconfigure), so the tree
    # silently drifted. A pinned SHA is reproducible across every build dir/worktree
    # and cannot silently drift; bump it deliberately.
    # Current: FORK-PIN (adm-s2 Shape A, 2026-08-13; re-pointed 2026-08-14 after
    # PR feedback) — Lint111/gaia-ecs branch PIN/parallel-observer-publication =
    # upstream main @ 93718a69 (0.9.3-dev)
    # + TWO commits: (1) the parallel observer-publication race fix (per-work-item
    # deferred OnSet queues drained by the coordinator) and (2) a regeneration of
    # single_include/gaia.h carrying that fix. WHY TWO BRANCHES: the maintainer asked
    # (PR #37, 2026-08-14) that the PR omit single_include/gaia.h — it is generated and
    # he regenerates it himself — so branch fix/... holds the reviewable sources ONLY,
    # while branch pin/... adds the regenerated amalgamation. WE PIN pin/... because
    # this build consumes single_include/gaia.h EXCLUSIVELY (target_include_directories
    # below points at single_include/, and every e7 test does #include <gaia.h>). Intake gate: the FETCHED tree must contain
    # DeferOnSetSlotScope (fix-unique symbol) — never trust a green reconfigure,
    # FetchContent caches _deps.
    # REVERT/FORWARD RECIPE: when upstream merges #37, point GIT_REPOSITORY back
    # to richardbiely/gaia-ecs and GIT_TAG to the upstream merge commit; to roll
    # back to pre-fix upstream, restore 93718a6938627cde50264049c6dcc933b966d16c.
    GIT_TAG        2b5b914  # fork pin/... = 93718a69 + fix + regenerated single header
    # SHALLOW disabled: a non-tag SHA can't be shallow-fetched on all git/server combos.
    GIT_SHALLOW    FALSE
    GIT_PROGRESS   TRUE
)

# Configure Gaia ECS options
set(GAIA_BUILD_UNITTEST OFF CACHE BOOL "Build Gaia unit tests" FORCE)
set(GAIA_BUILD_EXAMPLES OFF CACHE BOOL "Build Gaia examples" FORCE)
set(GAIA_PROFILER_CPU OFF CACHE BOOL "Enable Gaia CPU profiler" FORCE)
# The committed single_include/gaia.h is used as-is; do not regenerate it at build
# time. Regeneration runs a shell amalgamation script that is unavailable in this
# checkout (and needs clang-format), and is unnecessary since the header exists.
set(GAIA_GENERATE_SINGLE_HEADER OFF CACHE BOOL "Generate the single file header automatically." FORCE)

FetchContent_MakeAvailable(gaia)

# Organize Gaia in Dependencies folder
if(TARGET gaia)
    set_target_properties(gaia PROPERTIES FOLDER "Dependencies")
    # Upstream's own INTERFACE include dir is the repo root (gaia_SOURCE_DIR), which
    # matches its pre-v0.9 layout where the amalgamated header lived at the top level.
    # As of v0.9.2 the committed single header moved to single_include/gaia.h (and a
    # multi-header include/gaia.h also exists) — neither is at the root anymore, so
    # VIXEN's `#include <gaia.h>` consumers (VoxelComponents, GaiaVoxelWorld,
    # GaiaArchetypes) need single_include/ added explicitly.
    target_include_directories(gaia INTERFACE $<BUILD_INTERFACE:${gaia_SOURCE_DIR}/single_include>)
    # SYSTEM: the single_include/gaia.h amalgamation trips several first-party-visible warnings
    # (-Wunused-variable et al, hundreds of them) that are gaia's problem, not ours. Upstream's own
    # CMakeLists.txt (run by FetchContent_MakeAvailable, above) ALSO declares single_include as an
    # INTERFACE include -- non-SYSTEM -- so a second target_include_directories(... SYSTEM ...) call
    # here just appends a duplicate path entry rather than overriding upstream's; the compiler keeps
    # the first (non-SYSTEM) occurrence and the warnings still leak through. The actual override
    # mechanism is INTERFACE_SYSTEM_INCLUDE_DIRECTORIES, which retroactively marks an
    # ALREADY-DECLARED include path as SYSTEM for every consumer, without re-adding it -- the
    # standard third-party exemption mechanism (see perf/fedwarn-vixen1-report.md).
    set_property(TARGET gaia APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        $<BUILD_INTERFACE:${gaia_SOURCE_DIR}/single_include>)
endif()

message(STATUS "Gaia ECS fetched - available to all libraries")

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
# RmlUi locates FreeType by the find_package target name Freetype::Freetype; FetchContent only
# provides the lowercase `freetype` target, so alias it before RmlUi is configured below.
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

FetchContent_Declare(rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.0
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
set(RMLUI_FONT_ENGINE    freetype CACHE STRING "" FORCE)
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

# ============================================================================
# oneTBB - Intel Threading Building Blocks
# ============================================================================
# Used by: SVO (parallel octree), RenderGraph (TBBGraphExecutor)

find_package(TBB QUIET)
if(NOT TARGET TBB::tbb)
    FetchContent_Declare(
        TBB
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB
        GIT_TAG v2021.11.0
        GIT_SHALLOW TRUE
    )
    set(TBB_TEST OFF CACHE BOOL "" FORCE)
    set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(TBB)

    # Organize in Dependencies folder
    if(TARGET tbb)
        set_target_properties(tbb PROPERTIES FOLDER "Dependencies")
    endif()
    if(TARGET tbbmalloc)
        set_target_properties(tbbmalloc PROPERTIES FOLDER "Dependencies")
    endif()
    if(TARGET tbbmalloc_proxy)
        set_target_properties(tbbmalloc_proxy PROPERTIES FOLDER "Dependencies")
    endif()

    # gcc 13+ at -O2/-O3 emits a FALSE-POSITIVE -Wstringop-overflow inside TBB's lock-free atomics
    # ("__atomic_store_1 ... writing 1 byte into a region of size 0"). TBB builds with -Werror, so on
    # a gcc-13 Release build this breaks an otherwise-fine compile. Strip the error-promotion (and the
    # warning) on the TBB targets for GCC only — it's a known compiler/TBB interaction in third-party
    # code, not ours — so VIXEN builds in any configuration (Debug dodged it by being -O0).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        foreach(_tbb_tgt tbb tbbmalloc tbbmalloc_proxy)
            if(TARGET ${_tbb_tgt})
                target_compile_options(${_tbb_tgt} PRIVATE -Wno-error=stringop-overflow -Wno-stringop-overflow)
            endif()
        endforeach()
    endif()

    message(STATUS "oneTBB fetched - parallel execution available to all libraries")
else()
    message(STATUS "oneTBB found - parallel execution available to all libraries")
endif()

message(STATUS "Dependencies configured - all third-party libraries fetched")
