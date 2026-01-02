# ==============================================================================
# ResourceManagement Library
# Unified resource management: state tracking, memory allocation, lifetime scopes
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(ResourceManagement VERSION 2.0.0 LANGUAGES CXX)

# ------------------------------------------------------------------------------
# Library Target
# ------------------------------------------------------------------------------

add_library(ResourceManagement STATIC
    # State Management (template-based, header-only)
    include/State/ResourceState.h
    include/State/StatefulContainer.h
    include/State/RM.h

    # Memory Management
    include/Memory/IMemoryAllocator.h
    include/Memory/DirectAllocator.h
    include/Memory/VMAAllocator.h
    include/Memory/ResourceBudgetManager.h
    include/Memory/HostBudgetManager.h
    include/Memory/DeviceBudgetManager.h
    include/Memory/BudgetBridge.h

    src/Memory/DirectAllocator.cpp
    src/Memory/VMAAllocator.cpp
    src/Memory/ResourceBudgetManager.cpp
    src/Memory/HostBudgetManager.cpp
    src/Memory/DeviceBudgetManager.cpp
    src/Memory/BudgetBridge.cpp

    # Lifetime Management (header-only with inline implementations)
    include/Lifetime/DeferredDestruction.h
    include/Lifetime/SharedResource.h
    include/Lifetime/LifetimeScope.h
)

# ------------------------------------------------------------------------------
# Compiler Settings
# ------------------------------------------------------------------------------

target_compile_features(ResourceManagement PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(ResourceManagement PRIVATE /MP /W4)
else()
    target_compile_options(ResourceManagement PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ------------------------------------------------------------------------------
# Include Directories
# ------------------------------------------------------------------------------

target_include_directories(ResourceManagement
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# Vulkan SDK (required for memory allocator types)
find_package(Vulkan REQUIRED)
target_link_libraries(ResourceManagement PUBLIC Vulkan::Vulkan)

# VMA (Vulkan Memory Allocator) - provided by dependencies/CMakeLists.txt
if(TARGET VulkanMemoryAllocator)
    target_link_libraries(ResourceManagement PUBLIC VulkanMemoryAllocator)
else()
    message(FATAL_ERROR "VulkanMemoryAllocator target not found - ensure dependencies/CMakeLists.txt is included first")
endif()

# ------------------------------------------------------------------------------
# Visual Studio Organization
# ------------------------------------------------------------------------------

set_target_properties(ResourceManagement PROPERTIES FOLDER "Libraries")

# Source groups for IDE
source_group("State" FILES
    include/State/ResourceState.h
    include/State/StatefulContainer.h
    include/State/RM.h
)

source_group("Memory\\Headers" FILES
    include/Memory/IMemoryAllocator.h
    include/Memory/DirectAllocator.h
    include/Memory/VMAAllocator.h
    include/Memory/ResourceBudgetManager.h
    include/Memory/HostBudgetManager.h
    include/Memory/DeviceBudgetManager.h
    include/Memory/BudgetBridge.h
)

source_group("Memory\\Sources" FILES
    src/Memory/DirectAllocator.cpp
    src/Memory/VMAAllocator.cpp
    src/Memory/ResourceBudgetManager.cpp
    src/Memory/HostBudgetManager.cpp
    src/Memory/DeviceBudgetManager.cpp
    src/Memory/BudgetBridge.cpp
)

source_group("Lifetime" FILES
    include/Lifetime/DeferredDestruction.h
    include/Lifetime/SharedResource.h
    include/Lifetime/LifetimeScope.h
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

option(RESOURCEMANAGEMENT_BUILD_TESTS "Build ResourceManagement tests" ON)

if(RESOURCEMANAGEMENT_BUILD_TESTS AND BUILD_TESTS)
    add_subdirectory(tests)
endif()
