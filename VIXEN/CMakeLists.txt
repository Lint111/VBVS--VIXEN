#
# VIXEN - Vulkan Interactive eXample Engine
# Graph-Based Rendering Environment
#

cmake_minimum_required(VERSION 3.11..4.2 FATAL_ERROR)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

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

# Ccache/sccache support for faster rebuilds
option(USE_CCACHE "Use ccache/sccache for compilation caching" ON)
if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    find_program(SCCACHE_PROGRAM sccache)

    if(SCCACHE_PROGRAM)
        message(STATUS "Found sccache: ${SCCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
    elseif(CCACHE_PROGRAM)
        message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
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

# Parallel compilation (MSVC)
if(MSVC)
    add_compile_options(/MP)  # Multi-processor compilation

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
# Keep final binaries in project directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# Note: RUNTIME_OUTPUT_DIRECTORY is overridden per-target below to use binaries/

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

	# Generate the list of files to link, per flavor
	foreach(x ${GLSLANG_LIBS})
		list (APPEND VULKAN_LIB_LIST debug ${x}d optimized ${x})

	endforeach()

	# Modern Vulkan SDK has libraries in Lib directory (not glslang/build subdirectories)
	# No additional link_directories needed - already configured at line 125
endif()

# Specify a suitable project name
project(${Recipe_Name})

# MSVC parallel build PDB fix (must be right after project())
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FS")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /FS")
endif()

# ============================================================================
# Third-Party Dependencies
# ============================================================================
# All FetchContent and external library setup moved to dependencies/
add_subdirectory(dependencies)

# Suppress warnings from external dependencies
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Suppress deprecation warnings" FORCE)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "Suppress developer warnings" FORCE)

# Add any required preprocessor definitions here
add_definitions(-DVK_ENABLE_BETA_EXTENSIONS)

# Platform-specific Vulkan defines
if(WIN32)
    add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
elseif(APPLE)
    add_definitions(-DVK_USE_PLATFORM_MACOS_MVK)
endif()

# vulkan-1 library for build Vulkan application.
# Note: Do NOT use set() here as it would overwrite glslang libraries if they were added above
if(NOT DEFINED VULKAN_LIB_LIST)
	set(VULKAN_LIB_LIST "")
endif()
list(APPEND VULKAN_LIB_LIST "vulkan-1")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	# Include Vulkan header files from Vulkan SDK
	include_directories(AFTER ${VULKAN_PATH}/include)

	# Link directory for vulkan-1
	link_directories(${VULKAN_PATH}/Bin;${VULKAN_PATH}/Lib;)

endif()

# ============================================================================
# SHADER ASSETS
# ============================================================================
# Global shader source directory - GLSL files compiled by applications
add_subdirectory(Shaders)

# ============================================================================
# LIBRARY STRUCTURE FOR INCREMENTAL COMPILATION
# ============================================================================
# Each component is a separate static library with proper CMake target-based dependencies
# Changing one component won't force recompilation of others

add_subdirectory(libraries)

# ============================================================================
# APPLICATION EXECUTABLES
# ============================================================================
# All application entry points and executables
add_subdirectory(application)

# ============================================================================
# TESTING INFRASTRUCTURE
# ============================================================================
# Note: GoogleTest is fetched early (near top of file) so all subdirectories can use it.
# This section only enables CTest and adds root-level tests directory.

if(BUILD_TESTS)
    # Enable testing with CTest
    enable_testing()

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
# Benchmark Packaging
# ============================================================================
# Creates portable benchmark distribution folder
include(cmake/BenchmarkPackage.cmake)

# ============================================================================
# Visual Studio Solution Organization
# ============================================================================
# Set VIXEN as the startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VIXEN)
