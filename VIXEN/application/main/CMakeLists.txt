# ============================================================================
# VIXEN Main Application — driver library (VixenApp) + executable (VIXEN)
# ============================================================================
# The render driver (VulkanApplicationBase + VulkanGraphApplication + the graph build) is built as a
# LIBRARY (VixenApp) so more than one consumer can drive it: the VIXEN executable (main.cpp) and
# external render hosts (e.g. the UNDERTOW vixen/app host that boots the sim, then runs this loop).
# main.cpp is only the exe entry point.

cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

# All headers (for IDE project generation)
file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.*)

# Generated shader / SDI output directories (referenced by the driver's compile).
set(APP_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/application/main/generated/shaders")
set(APP_SDI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/application/main/generated/sdi")

# ----------------------------------------------------------------------------
# VixenApp: the engine's render driver, as a reusable static library.
# ----------------------------------------------------------------------------
add_library(VixenApp STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanApplicationBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanGraphApplication.cpp
    # M4: per-subgraph construction TUs (extracted from VulkanGraphApplication.cpp so a
    # node-config edit recompiles only the subgraph that wires it, not the lifecycle code).
    ${CMAKE_CURRENT_SOURCE_DIR}/source/graph/BuildRenderGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/graph/BuildUIGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/graph/BuildInstancingDemoGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/graph/BuildAutoSyncDemoGraph.cpp  # auto-sync P4 M4
    ${CMAKE_CURRENT_SOURCE_DIR}/source/graph/BuildFanInDemoGraph.cpp     # auto-sync P5b M2 fan-in
    ${HPP_FILES}
)

# Application include dir is PUBLIC so consumers get VulkanGraphApplication.h, Headers.h, etc.
target_include_directories(VixenApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# SDI / generated-header search paths used while compiling the driver (first found wins).
target_include_directories(VixenApp PRIVATE
    ${VIXEN_ROOT}/generated/sdi
    ${CMAKE_BINARY_DIR}/application/main/generated/sdi
    ${VIXEN_ROOT}/BuiltAssets/GeneratedIncludes
    ${APP_SDI_OUTPUT_DIR}
)

# Engine libraries are PUBLIC so consumers (VIXEN exe, render hosts) link them transitively.
target_link_libraries(VixenApp PUBLIC
    RenderGraph
    CashSystem
    VulkanResources
    Logger
    ${VULKAN_LIB_LIST}
    glm::glm
)

# VULKAN_LIB_LIST carries the glslang/SPIRV-Tools static libs BY NAME (e.g. -lSPIRV-Tools), and is
# PUBLIC above so it propagates to consumers — but their search directory is set at the engine root
# via directory-scoped link_directories(), which does NOT propagate to an external consumer (a render
# host in a sibling directory then can't resolve -lSPIRV-Tools). Expose it as a PUBLIC link directory
# on the target so any consumer of VixenApp inherits the -L. (The VIXEN exe, a subdirectory, already
# inherits the directory-scoped one; this is harmless there and load-bearing for external hosts.)
if(DEFINED VULKAN_PATH)
    if(CMAKE_SYSTEM_NAME MATCHES "Windows")
        target_link_directories(VixenApp PUBLIC ${VULKAN_PATH}/Bin ${VULKAN_PATH}/Lib)
    else()
        target_link_directories(VixenApp PUBLIC ${VULKAN_PATH}/lib)
    endif()
endif()

target_compile_definitions(VixenApp PRIVATE
    VIXEN_SHADER_SOURCE_DIR="${VIXEN_SHADER_SOURCE_DIR}"
    VIXEN_SHADER_OUTPUT_DIR="${APP_SHADER_OUTPUT_DIR}"
    VIXEN_SDI_OUTPUT_DIR="${APP_SDI_OUTPUT_DIR}"
)

# WSL2 GPU Vulkan: pass the provisioned Mesa Dozen ICD path (set by cmake/ProvisionWslVulkan.cmake at
# top-level configure, which runs before this subdirectory). Empty/undefined off WSL, in which case
# VulkanGraphApplication's SelectWslGpuIcd() helper compiles to a no-op.
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(VixenApp PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_property(TARGET VixenApp PROPERTY CXX_STANDARD 23)
set_property(TARGET VixenApp PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET VixenApp PROPERTY C_STANDARD 23)
set_property(TARGET VixenApp PROPERTY C_STANDARD_REQUIRED ON)
set_target_properties(VixenApp PROPERTIES FOLDER "Application")

if(MSVC)
    target_compile_options(VixenApp PRIVATE /FS)
endif()

if(USE_PRECOMPILED_HEADERS)
    # Headers.h moved to libraries/Core (AR#3/#4 header-leak severance); VixenApp gets it via
    # Core::Core (transitively linked through RenderGraph/VulkanResources/CashSystem).
    target_precompile_headers(VixenApp PRIVATE "${VIXEN_ROOT}/libraries/Core/include/Headers.h")
    message(STATUS "VixenApp: Precompiled headers enabled (using Core/Headers.h)")
endif()

if(WIN32)
    source_group("include" REGULAR_EXPRESSION "include/*")
    source_group("source" REGULAR_EXPRESSION "source/*")
endif()

# ----------------------------------------------------------------------------
# VIXEN: the executable entry point; links the shared driver.
# ----------------------------------------------------------------------------
add_executable(VIXEN ${CMAKE_CURRENT_SOURCE_DIR}/source/main.cpp)
target_link_libraries(VIXEN PRIVATE VixenApp)

set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY ${VIXEN_ROOT}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_DEBUG ${VIXEN_ROOT}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELEASE ${VIXEN_ROOT}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${VIXEN_ROOT}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${VIXEN_ROOT}/binaries)
set_property(TARGET VIXEN PROPERTY CXX_STANDARD 23)
set_property(TARGET VIXEN PROPERTY CXX_STANDARD_REQUIRED ON)
set_target_properties(VIXEN PROPERTIES FOLDER "Application")

if(MSVC)
    target_compile_options(VIXEN PRIVATE /FS)
endif()

message(STATUS "VIXEN: Shaders compile at runtime from ${VIXEN_SHADER_SOURCE_DIR}")
message(STATUS "VIXEN: Generated shader files output to ${APP_SHADER_OUTPUT_DIR}")
message(STATUS "VIXEN: SDI headers output to ${APP_SDI_OUTPUT_DIR}")

# ============================================================================
# Runtime DLL dependencies + assets (staged next to the executable)
# ============================================================================
# Copy TBB DLL to output directory (required by SVO library)
if(TARGET TBB::tbb)
    add_custom_command(TARGET VIXEN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:VIXEN>
        COMMENT "Copying TBB DLL for VIXEN"
    )
    message(STATUS "VIXEN: TBB DLL will be copied to output directory")
endif()

# Copy the UI assets next to the executable so the RmlUi demo is self-contained
# (the app loads them via the relative path "assets/ui/..."). Uses the shared
# vixen_stage_assets helper (cmake/VixenAssets.cmake) so consumers stage assets the same
# way without hand-writing a POST_BUILD copy [FR-10]. Path is rooted at VIXEN_ROOT (not
# CMAKE_SOURCE_DIR) so it resolves when the engine is add_subdirectory'd into a super-build.
vixen_stage_assets(VIXEN ${VIXEN_ROOT}/libraries/RenderGraph/assets DEST assets)
