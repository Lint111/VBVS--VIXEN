# ===========================================================================
# Core Infrastructure Systems - Consolidated Test Configuration
# ===========================================================================
# This file consolidates all core infrastructure tests:
# - Timer: High-resolution delta time measurement
# - LoopManager: Fixed timestep accumulator with catchup modes
# - ResourceDependencyTracker: Resource-to-producer mapping
# - PerFrameResources: Ring buffer pattern for per-frame GPU resources
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).
# ===========================================================================

# ---------------------------------------------------------------------------
# Timer Tests
# ---------------------------------------------------------------------------
# Validates Timer class (Core infrastructure):
# - High-resolution delta time measurement
# - Elapsed time tracking
# - Reset functionality
# - Precision validation

add_executable(test_timer
    Core/test_timer.cpp
)

target_link_libraries(test_timer PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_timer PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_timer)

message(STATUS "[RenderGraph Tests] Added: test_timer")

# ---------------------------------------------------------------------------
# LoopManager Tests
# ---------------------------------------------------------------------------
# Validates LoopManager class (Core infrastructure):
# - Loop registration and ID management
# - Variable timestep loops
# - Fixed timestep loops (60Hz, 120Hz)
# - Three catchup modes: FireAndForget, SingleCorrectiveStep, MultipleSteps
# - Spiral of death protection (maxCatchupTime)
# - Frame index tracking
# - Step count tracking
# - Multiple independent loops

add_executable(test_loop_manager
    Core/test_loop_manager.cpp
)

target_include_directories(test_loop_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_loop_manager PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_loop_manager PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_loop_manager)

message(STATUS "[RenderGraph Tests] Added: test_loop_manager")

# ---------------------------------------------------------------------------
# ResourceDependencyTracker Tests
# ---------------------------------------------------------------------------
# Validates ResourceDependencyTracker class (Core infrastructure):
# - Resource-to-producer mapping (register, query, update)
# - Producer-to-resources bidirectional mapping
# - nullptr handling and edge cases
# - Clear functionality and state management
# - Multiple resources per producer
# - Resource reassignment
# - Performance characteristics (1000+ resources)
#
# NOTE: GetDependenciesForNode() and BuildCleanupDependencies() require
# full NodeInstance integration and are tested in integration test suites.

add_executable(test_resource_dependency_tracker
    Core/test_resource_dependency_tracker.cpp
)

target_include_directories(test_resource_dependency_tracker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_resource_dependency_tracker PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_resource_dependency_tracker PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_resource_dependency_tracker)

message(STATUS "[RenderGraph Tests] Added: test_resource_dependency_tracker")

# ---------------------------------------------------------------------------
# PerFrameResources Tests
# ---------------------------------------------------------------------------
# Validates PerFrameResources class (Core infrastructure):
# - Initialization and frame count management
# - Descriptor set get/set operations
# - Command buffer get/set operations
# - Frame data access and validation
# - Ring buffer pattern (2-frame, 3-frame wraparound)
# - Edge cases (invalid indices, uninitialized state)
# - Cleanup functionality
#
# NOTE: CreateUniformBuffer(), GetUniformBuffer(), and GetUniformBufferMapped()
# require actual Vulkan device operations and are tested in integration suites.

add_executable(test_per_frame_resources
    Core/test_per_frame_resources.cpp
)

target_include_directories(test_per_frame_resources PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_per_frame_resources PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_per_frame_resources PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_per_frame_resources)

message(STATUS "[RenderGraph Tests] Added: test_per_frame_resources")
