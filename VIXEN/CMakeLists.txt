#
# VIXEN - Vulkan Interactive eXample Engine
# Graph-Based Rendering Environment
#

cmake_minimum_required(VERSION 3.11..4.2 FATAL_ERROR)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Root of the VIXEN engine tree. Use this (NOT CMAKE_SOURCE_DIR) for engine-relative source paths so
# VIXEN works both standalone AND when add_subdirectory'd into a super-build (e.g. the UNDERTOW
# vixen/ build that also builds the render host). Standalone, this equals CMAKE_SOURCE_DIR.
set(VIXEN_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(Recipe_Name "VIXEN")

# ============================================================================
# Build Performance Optimizations
# ============================================================================

# Use Ninja generator for faster builds (if available)
# Run: cmake -G Ninja -B build
if(NOT CMAKE_GENERATOR)
    find_program(NINJA_EXECUTABLE ninja)
    if(NINJA_EXECUTABLE)
        set(CMAKE_GENERATOR "Ninja" CACHE STRING "CMake generator" FORCE)
        message(STATUS "Ninja found, using Ninja generator for faster builds")
    endif()
endif()

# Ccache/sccache support for faster rebuilds.
#
# ccache is preferred over sccache here specifically because MSVC precompiled headers
# (/Yc, /Yu, /Fp — used by target_precompile_headers() on most of this project's core
# libraries: Core, EventBus, CashSystem, GaiaArchetypes, GaiaVoxelWorld, Logger,
# RenderGraphCore) are UNCONDITIONALLY uncacheable under sccache (upstream: sccache
# routes /Fp and /Yc as TooHardPath in src/compiler/msvc.rs — open issue
# mozilla/sccache#978, unresolved since 2021, no roadmap commitment). Measured impact:
# a fresh worktree's clean build hit only 38.9% cache-hit rate under sccache, with
# /Fp (310 occurrences) as the single largest non-cacheable-reason bucket — i.e. every
# fresh worktree on this multi-worktree-agent machine was paying full uncached
# PCH-compile cost for 7 core libraries regardless of the shared FetchContent/sccache
# cache-dir config already being correct. ccache added real MSVC /Yc PCH support in
# 2024 (refined through 2025 point releases) and is the documented fix other CMake
# projects (e.g. Qt) use for this exact gap. If ccache isn't installed, fall back to
# sccache (better than nothing on the non-PCH majority of the build) rather than
# failing the configure.
option(USE_CCACHE "Use ccache/sccache for compilation caching" ON)
if(USE_CCACHE)
    # Self-provisioning (Windows only — see ProvisionCcache.cmake): auto-downloads ccache into a
    # project-local cache if not already on PATH, same contract as Vulkan/gdb/windowing-deps
    # provisioning. Sets VIXEN_PROVISIONED_CCACHE_PROGRAM when a usable ccache is available
    # (system or provisioned); a no-op on non-Windows or if provisioning is disabled/fails.
    include("${VIXEN_ROOT}/cmake/ProvisionCcache.cmake" OPTIONAL)
    find_program(SCCACHE_PROGRAM sccache)

    if(VIXEN_PROVISIONED_CCACHE_PROGRAM)
        message(STATUS "Using ccache: ${VIXEN_PROVISIONED_CCACHE_PROGRAM} (preferred over sccache - caches MSVC PCH)")
        set(CMAKE_C_COMPILER_LAUNCHER ${VIXEN_PROVISIONED_CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${VIXEN_PROVISIONED_CCACHE_PROGRAM})
    elseif(SCCACHE_PROGRAM)
        message(STATUS "Found sccache: ${SCCACHE_PROGRAM} (ccache not found/provisioned - note: sccache cannot cache MSVC PCH compiles)")
        set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
    else()
        message(STATUS "ccache/sccache not found - install for faster rebuilds")
    endif()
endif()

# Unity builds - combine source files for faster compilation
option(USE_UNITY_BUILD "Use Unity builds for faster compilation" OFF)
if(USE_UNITY_BUILD)
    message(STATUS "Unity builds enabled - faster initial compilation")
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
endif()

# NOTE: the MSVC-specific block that used to live here (parallel compilation, linker warning
# suppression, job-pool cap) was DEAD CODE — `if(MSVC)` is never true before project() runs,
# since CMake hasn't done compiler detection yet at this point in the file. Confirmed live: a
# `message(STATUS "DEBUG_MARKER...")` placed at the very top of that block never printed in
# any configure, fresh or cached, direct `cmake --preset` or through build.bat. Moved into the
# existing post-project() `if(MSVC)` block below (same one Fix 4's /INCREMENTAL:NO fix already
# had to live in, for the identical reason) — see Worktree-Build-Artifact-Accumulation-Audit-
# 2026-07.md Fix 10 for the full story, including why this one had gone unnoticed even though
# it looked like it was working (CMake's own MSVC+Ninja default flags already include an
# equivalent of /MP, which is why compiles were still parallel despite this block never
# running — the /MP here was pure no-op duplication, not a broken feature).

# Generate compile_commands.json for better IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Precompiled headers option
option(USE_PRECOMPILED_HEADERS "Use precompiled headers" ON)

# Link-time optimization option (disable by default for faster debug builds)
option(USE_LTO "Enable link-time optimization (slower builds, faster runtime)" OFF)
if(USE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        message(STATUS "Link-time optimization enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO not supported: ${ipo_error}")
    endif()
endif()

# ============================================================================
# Build Configuration
# ============================================================================

# Configure build output directories to use temp for intermediate files
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# Final exes (and their runtime-adjacent files: PDB/ILK, logs, generated/sdi — all written
# CWD-relative to wherever the exe runs from, so they follow this directory automatically)
# live under the build dir, NOT the source tree, so `rm -rf build` reclaims everything in one
# shot instead of leaving multi-GB PDB/ILK files behind (see Worktree-Build-Artifact-
# Accumulation-Audit-2026-07.md Fix 2). RUNTIME_OUTPUT_DIRECTORY is set per-target below.
set(VIXEN_BINARIES_DIR ${CMAKE_BINARY_DIR}/binaries)

# AUTO_LOCATE_VULKAN - accepted value ON or OFF
# ON  - Use CMake to auto locate the Vulkan SDK.
# OFF - Vulkan SDK path can be specified manually. This is helpful to test the build on various Vulkan version.
option(AUTO_LOCATE_VULKAN "AUTO_LOCATE_VULKAN" ON)

# VULKAN_TRIMMED_BUILD - Build without full Vulkan SDK (headers only)
# ON  - Auto-fetch Vulkan headers if SDK not found, build only header-only components
# OFF - Require full Vulkan SDK with runtime libraries
option(VULKAN_TRIMMED_BUILD "Build with Vulkan headers only (no runtime libs)" OFF)

# Enable testing early so subprojects can use BUILD_TESTING
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    set(BUILD_TESTING ON CACHE BOOL "Build tests" FORCE)
endif()

# Enable code coverage (requires Debug build and GCC/Clang)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Code coverage enabled")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    else()
        message(WARNING "Coverage requires Debug build type. Set CMAKE_BUILD_TYPE=Debug")
    endif()
endif()

# Centralized debug build macro
# Automatically defined for Debug builds, can be used consistently across all code
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    add_compile_definitions(VIXEN_DEBUG_BUILD=1)
else()
    add_compile_definitions(VIXEN_DEBUG_BUILD=0)
endif()

# Fail-scenario simulation (Inc 1): declarations, injection seams, and the live sweep harness.
# OFF by default — zero footprint in real builds (everything guarded compiles out). ON must be a
# global, consistent view (nodes, app, and tests all see the same macro expansion).
option(VIXEN_FAIL_SCENARIOS "Compile fail-scenario declarations, injection seams, and harness" OFF)
if(VIXEN_FAIL_SCENARIOS)
    add_compile_definitions(VIXEN_FAIL_SCENARIOS=1)
endif()

# Descriptor resource lifecycle tracking (Debug/DescriptorResourceTracker.h) - records every
# DescriptorResourceEntry creation/extraction/bind event (source location, handle value, node
# name) into a thread-safe registry, with optional real-time console printing. Used to diagnose
# descriptor-write bugs (stale/null handles, binding mismatches) that are otherwise very hard to
# localize from a Release stack trace. Debug-only; compiles to zero-overhead no-ops otherwise, so
# it can never leak perf/verbosity into Release/RelWithDebInfo. Off by default even in Debug -
# opt in per debugging session (it's tens of thousands of events/run - see the header's own note).
option(VIXEN_ENABLE_DESCRIPTOR_TRACKING "Track DescriptorSetNode resource lifecycle events (Debug builds only)" OFF)
if(VIXEN_ENABLE_DESCRIPTOR_TRACKING)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Descriptor resource tracking enabled (VIXEN_DEBUG_DESCRIPTOR_TRACKING)")
        add_compile_definitions(VIXEN_DEBUG_DESCRIPTOR_TRACKING=1)
    else()
        message(WARNING "VIXEN_ENABLE_DESCRIPTOR_TRACKING requires Debug build type; ignoring "
                         "(current CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}). Set CMAKE_BUILD_TYPE=Debug.")
    endif()
endif()

# Self-provision Vulkan: download the prebuilt SDK into a cache if none is found, so a consumer only
# ever builds VIXEN (never installs Vulkan by hand). The legacy locate logic below then simply finds
# the provisioned SDK and sets VULKAN_PATH / VULKAN_TRIMMED_BUILD_ACTIVE as before.
include(cmake/ProvisionVulkan.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProvisionWslVulkan.cmake)

# Detect (and optionally auto-provision) gdb — needed to get a real backtrace on the
# vixen-wsl-debug preset. Sets VIXEN_GDB_EXECUTABLE and the vixen_add_gdb_target() function.
include(cmake/ProvisionGdb.cmake)

# Reusable runtime-asset staging helper (vixen_stage_assets) for apps + consumers.
include(cmake/VixenAssets.cmake)

# Configure-time AddNode<T>-call-site scan (vixen_generate_node_manifest) — drives scoped
# per-app RenderGraph node linkage (Graph-Derived-Node-Linkage-Spec-2026-07).
include(cmake/VixenNodeManifest.cmake)

if(AUTO_LOCATE_VULKAN)
	message(STATUS "Attempting auto locate Vulkan using CMake......")
	
	# Find Vulkan Path using CMake's Vulkan Module
	# This will return Boolean 'Vulkan_FOUND' indicating the status of find as success(ON) or fail(OFF).
	# Include directory path - 'Vulkan_INCLUDE_DIRS' and 'Vulkan_LIBRARY' with required libraries.
	find_package(Vulkan)
	
	# Try extracting VulkanSDK path from ${Vulkan_INCLUDE_DIRS}
	if (NOT ${Vulkan_INCLUDE_DIRS} STREQUAL "")
		set(VULKAN_PATH ${Vulkan_INCLUDE_DIRS})
		STRING(REGEX REPLACE "/[Ii]nclude" "" VULKAN_PATH ${VULKAN_PATH})
	endif()
		 
	if(NOT Vulkan_FOUND)
		# CMake may fail to locate the libraries but could be able to
		# provide some path in Vulkan SDK include directory variable
		# 'Vulkan_INCLUDE_DIRS', try to extract path from this.
		message(STATUS "Failed to locate Vulkan SDK, retrying again...")
		if(EXISTS "${VULKAN_PATH}")
			message(STATUS "Successfully located the Vulkan SDK: ${VULKAN_PATH}")
		else()
			# Vulkan SDK not found - offer trimmed build option
			if(VULKAN_TRIMMED_BUILD)
				message(STATUS "==========================================================")
				message(STATUS "Vulkan SDK not found, enabling TRIMMED BUILD mode")
				message(STATUS "Fetching Vulkan headers (no runtime libraries)")
				message(STATUS "Only header-only components will be built")
				message(STATUS "==========================================================")
				include(cmake/VulkanHeaders.cmake)
				set(VULKAN_TRIMMED_BUILD_ACTIVE TRUE)
			else()
				message(WARNING "Unable to locate Vulkan SDK.")
				message(WARNING "Options:")
				message(WARNING "  1. Install Vulkan SDK")
				message(WARNING "  2. Use -DVULKAN_TRIMMED_BUILD=ON to fetch headers only")
				message(WARNING "  3. Turn off auto locate: -DAUTO_LOCATE_VULKAN=OFF")
				message(FATAL_ERROR "Vulkan SDK required or use VULKAN_TRIMMED_BUILD")
			endif()
		endif()
	else()
		message(STATUS "Vulkan SDK found: ${VULKAN_PATH}")
		set(VULKAN_TRIMMED_BUILD_ACTIVE FALSE)
	endif()
else()
	message(STATUS "Attempting to locate Vulkan SDK using manual path......")
	set(VULKAN_SDK "C:/VulkanSDK")
	set(VULKAN_VERSION "1.4.321.1")
	set(VULKAN_PATH "${VULKAN_SDK}/${VULKAN_VERSION}")
	message(STATUS "Using manual specified path: ${VULKAN_PATH}")

	# Check if manual set path exists
	if(NOT EXISTS "${VULKAN_PATH}")
		if(VULKAN_TRIMMED_BUILD)
			message(STATUS "==========================================================")
			message(STATUS "Manual Vulkan path not found, enabling TRIMMED BUILD mode")
			message(STATUS "Fetching Vulkan headers (no runtime libraries)")
			message(STATUS "Only header-only components will be built")
			message(STATUS "==========================================================")
			include(cmake/VulkanHeaders.cmake)
			set(VULKAN_TRIMMED_BUILD_ACTIVE TRUE)
		else()
			message(WARNING "Unable to locate this Vulkan SDK path VULKAN_PATH: ${VULKAN_PATH}")
			message(WARNING "Options:")
			message(WARNING "  1. Install Vulkan SDK at correct path")
			message(WARNING "  2. Use -DVULKAN_TRIMMED_BUILD=ON to fetch headers only")
			message(WARNING "  3. Use AUTO_LOCATE_VULKAN=ON for automatic detection")
			message(FATAL_ERROR "Vulkan SDK required or use VULKAN_TRIMMED_BUILD")
		endif()
	else()
		set(VULKAN_TRIMMED_BUILD_ACTIVE FALSE)
	endif()
endif()

# BUILD_SPV_ON_COMPILE_TIME - accepted value ON or OFF
# ON - Reads the GLSL shader file and auto convert in SPIR-V form (.spv).
# This requires additional libraries support from VulkanSDK like
# SPIRV glslang OGLCompiler OSDependent HLSL.
# OFF - Only reads .spv files, which need to be compiled offline using
# glslangValidator.exe.
# For example: glslangValidator.exe <GLSL file name> -V -o <output fileneame in SPIR-V(.spv) form>

# Disable SPV compile in trimmed mode (requires glslang libraries)
if(VULKAN_TRIMMED_BUILD_ACTIVE)
	set(BUILD_SPV_ON_COMPILE_TIME OFF)
	message(STATUS "BUILD_SPV_ON_COMPILE_TIME disabled (trimmed build mode)")
else()
	option(BUILD_SPV_ON_COMPILE_TIME "BUILD_SPV_ON_COMPILE_TIME" ON)
endif()

if(BUILD_SPV_ON_COMPILE_TIME AND NOT VULKAN_TRIMMED_BUILD_ACTIVE)
	# Preprocessor flag allows the solution to use glslang library functions
	add_definitions(-DAUTO_COMPILE_GLSL_TO_SPV)

	# GLSL - use vulkan SDK's glslang library for compiling GLSL to SPIR-V
	# This does not require offline conversion of GLSL shader to SPIR-V(.spv) form.
	# Modern Vulkan SDK has glslang in Include directory
	set(GLSLANGDIR "${VULKAN_PATH}/Include/glslang")

	get_filename_component(GLSLANG_PREFIX "${GLSLANGDIR}" ABSOLUTE)

	# Check if glslang directory exists
	if(NOT EXISTS "${GLSLANG_PREFIX}")
		message ("Fatal Error: glslang directory not found in Vulkan SDK path: ${GLSLANG_PREFIX}.")
		message ("Note: glslang headers are expected in ${VULKAN_PATH}/Include/glslang")
	endif()

	# Include the glslang directory (the parent Include directory is already included below)
	# No need to include_directories here as it's already done at line 115

	# If compiling GLSL to SPV using we need the following libraries
	# Modern Vulkan SDK includes these in Lib directory
	set (GLSLANG_LIBS SPIRV glslang OSDependent glslang-default-resource-limits SPIRV-Tools SPIRV-Tools-opt)

	# Generate the list of files to link, per flavor. The "d"-suffixed debug
	# libraries (e.g. SPIRVd) only exist in the LunarG *Windows* SDK; the Linux
	# SDK ships a single unsuffixed library per name.
	foreach(x ${GLSLANG_LIBS})
		if(WIN32)
			list (APPEND VULKAN_LIB_LIST debug ${x}d optimized ${x})
		else()
			list (APPEND VULKAN_LIB_LIST ${x})
		endif()

	endforeach()

	# Modern Vulkan SDK has libraries in Lib directory (not glslang/build subdirectories)
	# No additional link_directories needed - already configured at line 125
endif()

# Specify a suitable project name.
# VERSION is the single source of truth for VIXEN's version (AR#13): it populates
# PROJECT_VERSION*, which feeds both the generated <VixenVersion.h> (cmake/VixenVersion.h.in)
# and the find_package(VIXEN) package-version file (cmake/VixenInstall.cmake).
project(${Recipe_Name} VERSION 0.1.0)

# MSVC parallel build PDB fix (must be right after project())
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /FS")

    # Disable incremental linking (must ALSO be right after project() — CMake only sets its
    # own /INCREMENTAL default into CMAKE_*_LINKER_FLAGS_DEBUG inside project() itself, so an
    # earlier set() here gets silently clobbered; confirmed live, .ilk kept generating until
    # this moved below project()). This is a Ninja + sccache workflow, not MSBuild/Visual-
    # Studio-IDE incremental rebuild, so /INCREMENTAL's only benefit (fast re-link after
    # touching one .cpp in an active IDE session) doesn't apply; the actual iteration-speed
    # lever here is sccache's object cache (see USE_CCACHE above). Cost was real: ~350MB .ilk
    # per exe, x3 exes, x every worktree/preset that got built (Worktree-Build-Artifact-
    # Accumulation-Audit-2026-07.md Fix 4). /INCREMENTAL:NO also implies no .ilk is written at
    # all, so this is a pure win, not a size/speed tradeoff.
    foreach(_vixen_linker_flags_var
            CMAKE_EXE_LINKER_FLAGS_DEBUG
            CMAKE_SHARED_LINKER_FLAGS_DEBUG
            CMAKE_MODULE_LINKER_FLAGS_DEBUG)
        string(REGEX REPLACE "/INCREMENTAL[^ ]*" "" ${_vixen_linker_flags_var} "${${_vixen_linker_flags_var}}")
        set(${_vixen_linker_flags_var} "${${_vixen_linker_flags_var}} /INCREMENTAL:NO")
    endforeach()

    add_compile_options(/MP)  # Multi-processor compilation

    # Cap concurrent LINK jobs separately from compile jobs (Ninja job pool). Link is the
    # RSS-heavy step for this project's debug builds — measured 400-500MB per link.exe with
    # /Z7 embedded debug info (Fix 4) — so running as many links in parallel as cores (ninja's
    # default, matching -j/--parallel) is what actually made the whole machine stutter under
    # load, not the compile step. VIXEN_MAX_PARALLEL_LINKS overrides; default is a conservative
    # fraction of logical cores (see Worktree-Build-Artifact-Accumulation-Audit-2026-07.md
    # Fix 10 for the reasoning and measurements this is based on).
    if(DEFINED ENV{VIXEN_MAX_PARALLEL_LINKS})
        set(_vixen_max_parallel_links $ENV{VIXEN_MAX_PARALLEL_LINKS})
    else()
        cmake_host_system_information(RESULT _vixen_logical_cores QUERY NUMBER_OF_LOGICAL_CORES)
        math(EXPR _vixen_max_parallel_links "(${_vixen_logical_cores} + 3) / 4")
        if(_vixen_max_parallel_links LESS 1)
            set(_vixen_max_parallel_links 1)
        endif()
    endif()
    set_property(GLOBAL PROPERTY JOB_POOLS "link_pool=${_vixen_max_parallel_links}")
    set(CMAKE_JOB_POOL_LINK "link_pool")
    message(STATUS "Capping concurrent link jobs at ${_vixen_max_parallel_links} (override with VIXEN_MAX_PARALLEL_LINKS)")

    # Suppress warnings from third-party libraries
    # Use CMAKE_*_LINKER_FLAGS for compatibility with CMake < 3.13
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /ignore:4099 /NOLOGO")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /ignore:4099 /NOLOGO")
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /ignore:4099 /NOLOGO")

    # Suppress warnings from external headers (angle-bracket includes)
    add_compile_options(
        /external:W0           # Suppress all warnings from external headers
        /external:anglebrackets # Treat <> includes as external
    )

    # Suppress common third-party compile warnings
    add_compile_options(
        /wd4100  # Unreferenced formal parameter
        /wd4189  # Local variable initialized but not referenced
        /wd4244  # Conversion warnings
        /wd4267  # Size_t conversion warnings
    )

    message(STATUS "MSVC multi-processor compilation enabled")
    message(STATUS "MSVC linker warnings suppressed (LNK4099: missing PDB files)")
    message(STATUS "MSVC compile warnings suppressed for third-party libs (4100, 4189, 4244, 4267)")
endif()

# ============================================================================
# Third-Party Dependencies
# ============================================================================
# Share FetchContent's clone+build output across all worktrees/presets of this repo instead of
# each one re-cloning/rebuilding the same ~12 third-party libs (several built from source: TBB,
# RmlUi, freetype, glfw, googletest) into its own build/<preset>/_deps. This is the single
# largest driver of disk growth in a multi-worktree workflow — see Worktree-Build-Artifact-
# Accumulation-Audit-2026-07.md Fix 1. Only set it when VIXEN is the top-level project: a parent
# super-build (e.g. UNDERTOW's vixen/ add_subdirectory) already controls this cache-wide setting
# and must not be overridden by an inner add_subdirectory. Override with VIXEN_FETCHCONTENT_CACHE
# env var for machines where C:/vixen-fetchcontent-cache isn't appropriate (e.g. non-Windows).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(DEFINED ENV{VIXEN_FETCHCONTENT_CACHE})
        set(FETCHCONTENT_BASE_DIR "$ENV{VIXEN_FETCHCONTENT_CACHE}" CACHE PATH "Shared FetchContent cache" FORCE)
    elseif(WIN32)
        set(FETCHCONTENT_BASE_DIR "C:/vixen-fetchcontent-cache" CACHE PATH "Shared FetchContent cache" FORCE)
    else()
        set(FETCHCONTENT_BASE_DIR "$ENV{HOME}/.cache/vixen-fetchcontent-cache" CACHE PATH "Shared FetchContent cache" FORCE)
    endif()
    message(STATUS "FetchContent shared cache: ${FETCHCONTENT_BASE_DIR}")
endif()

# All FetchContent and external library setup moved to dependencies/
add_subdirectory(dependencies)

# enable_testing() must precede any add_subdirectory() that calls add_test(),
# or CMake silently drops those tests from CTest — libraries/ and application/
# below both register tests, so this has to run before them, not in the
# TESTING INFRASTRUCTURE section near the bottom of this file.
if(BUILD_TESTS)
    enable_testing()
endif()

# Suppress warnings from external dependencies
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Suppress deprecation warnings" FORCE)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "Suppress developer warnings" FORCE)

# Add any required preprocessor definitions here
add_definitions(-DVK_ENABLE_BETA_EXTENSIONS)

# Platform surface handling is cross-platform via GLFW (glfwCreateWindowSurface +
# glfwGetRequiredInstanceExtensions select the platform surface at build/runtime), so no
# VK_USE_PLATFORM_* surface define is set here.

# vulkan-1 library for build Vulkan application.
# Note: Do NOT use set() here as it would overwrite glslang libraries if they were added above
if(NOT DEFINED VULKAN_LIB_LIST)
	set(VULKAN_LIB_LIST "")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	# "vulkan-1" is the Windows import-lib name for the loader. On other
	# platforms the loader (libvulkan.so) is provided via the Vulkan::Vulkan
	# target, linked transitively by the core libraries, so no by-name entry.
	list(APPEND VULKAN_LIB_LIST "vulkan-1")

	# Include Vulkan header files from Vulkan SDK
	include_directories(AFTER ${VULKAN_PATH}/include)

	# Link directory for vulkan-1 and the glslang libraries
	link_directories(${VULKAN_PATH}/Bin;${VULKAN_PATH}/Lib;)
else()
	# Ensure the loader resolves for executables that consume VULKAN_LIB_LIST
	# even if they don't otherwise pull a Vulkan::Vulkan-linked library.
	if(TARGET Vulkan::Vulkan)
		list(APPEND VULKAN_LIB_LIST Vulkan::Vulkan)
	endif()

	# The glslang/SPIRV-Tools static libraries (added by-name to VULKAN_LIB_LIST
	# above) live in <sdk>/lib on the Linux SDK; make them findable by the linker.
	link_directories(${VULKAN_PATH}/lib)
endif()

# ============================================================================
# SHADER ASSETS
# ============================================================================
# Global shader source directory - GLSL files compiled by applications
# (dir is lowercase 'shaders'; capital 'Shaders' broke case-sensitive filesystems, e.g. Linux/WSL)
add_subdirectory(shaders)

# ============================================================================
# LIBRARY STRUCTURE FOR INCREMENTAL COMPILATION
# ============================================================================
# Each component is a separate static library with proper CMake target-based dependencies
# Changing one component won't force recompilation of others

add_subdirectory(libraries)

# Config-struct codegen golden gate (no-ops without dotnet). Opt-out with -DVIXEN_CODEGEN_GATE=OFF.
option(VIXEN_CODEGEN_GATE "Run the config-struct codegen golden check" ON)
if(VIXEN_CODEGEN_GATE)
    add_subdirectory(codegen)
endif()

# ============================================================================
# GENERATED VERSION HEADER (AR#13)
# ============================================================================
# <VixenVersion.h> generated from project()'s VERSION (single source of truth) and put on
# RenderGraphCore's PUBLIC include path, so VIXEN's own TUs and any consumer linking
# Vixen::RenderGraph (the INTERFACE facade → RenderGraphCore) can #include <VixenVersion.h>.
# (RenderGraph is now an INTERFACE library and cannot take PUBLIC include dirs directly;
# Core's PUBLIC include propagates through the facade to every consumer and to Nodes.)
# The matching install() (into the SDK include dir) lives in cmake/VixenInstall.cmake;
# INSTALL_INTERFACE include is already the flattened include/ injected there, so the
# installed header resolves for consumers.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VixenVersion.h.in
    ${CMAKE_BINARY_DIR}/generated/include/VixenVersion.h
    @ONLY)
target_include_directories(RenderGraphCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated/include>)

# ============================================================================
# CONSUMABLE ARTIFACT (AR#2)
# ============================================================================
# Unified install(EXPORT VixenTargets) + VIXENConfig.cmake so an external host
# (UNDERTOW) can find_package(VIXEN) and link Vixen::* . Must follow the
# libraries subdir so every exported target exists. Standalone builds only —
# when VIXEN is add_subdirectory'd into a super-build the host owns install.
option(VIXEN_INSTALL_EXPORT "Generate the install(EXPORT)/find_package(VIXEN) consumable artifact (AR#2)" OFF)
if(VIXEN_INSTALL_EXPORT AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(cmake/VixenInstall.cmake)
endif()

# ============================================================================
# APPLICATION EXECUTABLES
# ============================================================================
# All application entry points and executables
add_subdirectory(application)

# ============================================================================
# TESTING INFRASTRUCTURE
# ============================================================================
# Note: GoogleTest is fetched early (near top of file) so all subdirectories can use it.
# enable_testing() itself now runs right after add_subdirectory(dependencies)
# above (it must precede libraries/application so their add_test() calls
# register). This section just adds the root-level tests directory.

if(BUILD_TESTS)
    # Add root-level tests subdirectory (if it exists)
    # Note: Subdirectories (RenderGraph, ShaderManagement, etc.) add their own tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_subdirectory(tests)
    endif()

    message(STATUS "Unit tests enabled - use 'ctest' or 'make test' to run")
else()
    message(STATUS "Unit tests disabled - use -DBUILD_TESTS=ON to enable")
endif()
# ============================================================================
# Code Coverage Target (for VS Code Coverage Gutters)
# ============================================================================
if(ENABLE_COVERAGE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E echo "Running tests with coverage..."
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose --output-on-failure
        COMMAND ${CMAKE_COMMAND} -E echo "Generating coverage report..."
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' '*/build/_deps/*' --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_html
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in coverage_html/"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating code coverage report"
    )
    
    add_custom_target(coverage-clean
        COMMAND find . -name "*.gcda" -delete
        COMMAND find . -name "*.gcno" -delete
        COMMAND rm -f coverage.info
        COMMAND rm -rf coverage_html
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Cleaning coverage data"
    )
endif()

# CompileTimeResourceSystem tests moved to RenderGraph/tests/
# (See RenderGraph/tests/CMakeLists.txt for test configuration)

# ============================================================================
# Visual Studio Solution Organization
# ============================================================================
# Set VIXEN as the startup project in Visual Studio
set_property(DIRECTORY ${VIXEN_ROOT} PROPERTY VS_STARTUP_PROJECT VIXEN)
