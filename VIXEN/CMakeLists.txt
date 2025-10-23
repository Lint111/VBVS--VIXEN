#
# Chapter 3 - Device Hand Shake CMake file.
#

cmake_minimum_required(VERSION 3.11..4.0 FATAL_ERROR)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(Recipe_Name "7e_ShadersWithSPIRV")

# AUTO_LOCATE_VULKAN - accepted value ON or OFF
# ON  - Use CMake to auto locate the Vulkan SDK.
# OFF - Vulkan SDK path can be specified manually. This is helpful to test the build on various Vulkan version.
option(AUTO_LOCATE_VULKAN "AUTO_LOCATE_VULKAN" ON)

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
			message("Error: Unable to locate Vulkan SDK. Please turn off auto locate option by specifying 'AUTO_LOCATE_VULKAN' as 'OFF'")
			message("and specify manually path using 'VULKAN_SDK' and 'VULKAN_VERSION' variables in the CMakeLists.txt.")
			return()
		endif()
	endif()
else()
	message(STATUS "Attempting to locate Vulkan SDK using manual path......")
	set(VULKAN_SDK "C:/VulkanSDK")
	set(VULKAN_VERSION "1.4.321.1")
	set(VULKAN_PATH "${VULKAN_SDK}/${VULKAN_VERSION}")
	message(STATUS "Using manual specified path: ${VULKAN_PATH}")

	# Check if manual set path exists
	if(NOT EXISTS "${VULKAN_PATH}")
		message("Error: Unable to locate this Vulkan SDK path VULKAN_PATH: ${VULKAN_PATH}, please specify correct path.
		For more information on correct installation process, please refer to subsection 'Getting started with Lunar-G SDK'
		and 'Setting up first project with CMake' in Chapter 3, 'Shaking hands with the device' in this book 'Learning Vulkan', ISBN - 9781786469809.")
	   return()
	endif()
endif()

# BUILD_SPV_ON_COMPILE_TIME - accepted value ON or OFF
# ON - Reads the GLSL shader file and auto convert in SPIR-V form (.spv).
# This requires additional libraries support from VulkanSDK like
# SPIRV glslang OGLCompiler OSDependent HLSL.
# OFF - Only reads .spv files, which need to be compiled offline using
# glslangValidator.exe.
# For example: glslangValidator.exe <GLSL file name> -V -o <output fileneame in SPIR-V(.spv) form>
option(BUILD_SPV_ON_COMPILE_TIME "BUILD_SPV_ON_COMPILE_TIME" ON)

if(BUILD_SPV_ON_COMPILE_TIME)
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

# GLM SETUP using FetchContent
include(FetchContent)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1 # Use tag name directly
)

FetchContent_MakeAvailable(glm)

# Fetch the latest Khronos Vulkan header files only if not already provided (e.g., by Vulkan SDK)
if(NOT TARGET Vulkan::Headers)
	FetchContent_Declare(
		VulkanHeaders
		GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
		GIT_TAG main  # or pick a specific release, e.g., v1.3.300
	)
	FetchContent_MakeAvailable(VulkanHeaders)
endif()

# GLI Setup - header-only library for compressed texture loading (DDS/KTX)
set(GLI_TEST_ENABLE OFF CACHE BOOL "" FORCE)  # Disable GLI tests
FetchContent_Declare(
	gli
	GIT_REPOSITORY https://github.com/g-truc/gli.git
	GIT_TAG        master
	GIT_SHALLOW    TRUE  # Shallow clone to reduce download size
)

FetchContent_MakeAvailable(gli)

# STB Setup - header-only library for uncompressed image loading (PNG/JPG/etc)
FetchContent_Declare(
	stb
	GIT_REPOSITORY https://github.com/nothings/stb.git
	GIT_TAG        master
	GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(stb)

# Specify a suitable project name
project(${Recipe_Name})

# Add any required preprocessor definitions here
add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
add_definitions(-DVK_ENABLE_BETA_EXTENSIONS -DVK_USE_PLATFORM_WIN32_KHR)

# vulkan-1 library for build Vulkan application.
# Note: Do NOT use set() here as it would overwrite glslang libraries if they were added above
if(NOT DEFINED VULKAN_LIB_LIST)
	set(VULKAN_LIB_LIST "")
endif()
list(APPEND VULKAN_LIB_LIST "vulkan-1")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	# Vulkan headers include path (only when fetched)
	if(DEFINED vulkanheaders_SOURCE_DIR)
		include_directories(BEFORE ${vulkanheaders_SOURCE_DIR}/include)
	endif()
	include_directories(BEFORE ${gli_SOURCE_DIR})  # GLI is header-only
	include_directories(BEFORE ${stb_SOURCE_DIR})  # STB is header-only

	# Include Vulkan header files from Vulkan SDK
	include_directories(AFTER ${VULKAN_PATH}/include)

	# Link directory for vulkan-1
	link_directories(${VULKAN_PATH}/Bin;${VULKAN_PATH}/Lib;)

endif()


# Define directories and the contained folder and files inside.
if(WIN32)
    source_group("include" REGULAR_EXPRESSION "include/*")
    source_group("source" REGULAR_EXPRESSION "source/*")
endif(WIN32)

# Define include path
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/logger)

# ============================================================================
# LIBRARY STRUCTURE FOR INCREMENTAL COMPILATION
# ============================================================================
# Each node is a separate OBJECT library - changing one node won't recompile others
# Core systems are static libraries with proper dependencies

# Logger library (used by everything)
file(GLOB LOGGER_CPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/logger/*.cpp)
file(GLOB LOGGER_HPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/logger/*.h)
add_library(Logger STATIC ${LOGGER_CPP_FILES} ${LOGGER_HPP_FILES})
target_include_directories(Logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/logger)
set_property(TARGET Logger PROPERTY CXX_STANDARD 23)

# EventBus library (message passing, worker thread integration)
add_subdirectory(EventBus)

# ResourceManagement library (RM<T> wrapper with state tracking)
add_subdirectory(ResourceManagement)

# ShaderManagement library (device-agnostic shader compilation and hot reload)
add_subdirectory(ShaderManagement)

# Vulkan Resources library (device, swapchain, descriptors, pipeline, shader, texture handling, time, error, utilities)
file(GLOB_RECURSE VULKAN_RESOURCES_CPP
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanResources/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/TextureHandling/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Time/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/error/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanDescriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanShader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanSwapChain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/wrapper.cpp
)
add_library(VulkanResources STATIC ${VULKAN_RESOURCES_CPP})
target_link_libraries(VulkanResources PUBLIC ${VULKAN_LIB_LIST} glm::glm Logger)
set_property(TARGET VulkanResources PROPERTY CXX_STANDARD 23)

# RenderGraph static library (replaces old inline definitions)
add_subdirectory(RenderGraph)

# ============================================================================
# MAIN EXECUTABLE
# ============================================================================
# Main application files
file(GLOB MAIN_CPP_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanApplicationBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanGraphApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanDrawable.cpp
)

# All headers (for IDE project generation)
file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.*)

# Build main executable
add_executable(${Recipe_Name}
    ${MAIN_CPP_FILES}
    ${HPP_FILES}
)

# Link libraries to main executable
target_link_libraries(${Recipe_Name}
    RenderGraph
    VulkanResources
    Logger
    ${VULKAN_LIB_LIST}
    glm::glm
)

# Define project properties
set_property(TARGET ${Recipe_Name} PROPERTY RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/binaries)
set_property(TARGET ${Recipe_Name} PROPERTY RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/binaries)
set_property(TARGET ${Recipe_Name} PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/binaries)
set_property(TARGET ${Recipe_Name} PROPERTY RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_CURRENT_SOURCE_DIR}/binaries)
set_property(TARGET ${Recipe_Name} PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/binaries)

# Define C++ version to be used for building the project
set_property(TARGET ${Recipe_Name} PROPERTY CXX_STANDARD 23)
set_property(TARGET ${Recipe_Name} PROPERTY CXX_STANDARD_REQUIRED ON)

# Define C version to be used for building the project
set_property(TARGET ${Recipe_Name} PROPERTY C_STANDARD 23)
set_property(TARGET ${Recipe_Name} PROPERTY C_STANDARD_REQUIRED ON)

# Add /FS flag for MSVC to allow multiple CL.EXE to write to the same PDB file
if(MSVC)
    target_compile_options(${Recipe_Name} PRIVATE /FS)
endif()