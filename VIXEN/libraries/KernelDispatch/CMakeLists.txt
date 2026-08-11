cmake_minimum_required(VERSION 3.20...4.2)

project(KernelDispatch VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# KernelDispatch Static Library
# ============================================================================
# E7 dispatch D1 (native-Gaia/MCP cutover, spec 14): a renderer-independent native dispatch library.
# Tier-A scheduling primitives extracted from RenderGraph, decoupled from render identity (opaque
# owner ids + std::function work), plus the data-only Stage/Handle/VxArtifactHeader ABI. Domain-blind:
# depends only on TBB. It must NOT depend on RenderGraph's NodeInstance/Resource types (verified by
# the header grep in the D1 gates).

set(KERNELDISPATCH_HEADERS
    include/KernelDispatch/Abi.h                 # VxArtifactHeader / ScalePolicy / Stage / Handle
    include/KernelDispatch/VirtualTask.h         # opaque-id task (was NodeInstance*-keyed)
    include/KernelDispatch/TaskDependencyGraph.h # opaque-id DAG (sort / parallel levels)
    include/KernelDispatch/TaskExecutor.h        # TBB task-wave executor, worker-count control
    include/KernelDispatch/Dispatcher.h          # RunPerElementStage(stage) -> Handle
)

set(KERNELDISPATCH_SOURCES
    src/TaskDependencyGraph.cpp
    src/TaskExecutor.cpp
)

add_library(KernelDispatch STATIC
    ${KERNELDISPATCH_HEADERS}
    ${KERNELDISPATCH_SOURCES}
)

target_include_directories(KernelDispatch
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(KernelDispatch PUBLIC cxx_std_23)

set_target_properties(KernelDispatch PROPERTIES
    FOLDER "Libraries"
    POSITION_INDEPENDENT_CODE ON)

# TBB provides the task-wave parallelism (tbb::parallel_for_each + task_arena). Provisioned by
# dependencies/CMakeLists.txt (find_package(TBB) or the oneTBB FetchContent) as TBB::tbb -- the same
# target RenderGraph/SVO link.
target_link_libraries(KernelDispatch PUBLIC TBB::tbb)

# ============================================================================
# Precompiled Headers
# ============================================================================
if(USE_PRECOMPILED_HEADERS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h")
    target_precompile_headers(KernelDispatch PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h")
    message(STATUS "KernelDispatch: Precompiled headers enabled")
endif()

# ============================================================================
# IDE Support - Group files
# ============================================================================
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${KERNELDISPATCH_HEADERS}
    ${KERNELDISPATCH_SOURCES})

message(STATUS "KernelDispatch library configured (E7 dispatch D1: Tier-A extraction + data-only ABI)")
