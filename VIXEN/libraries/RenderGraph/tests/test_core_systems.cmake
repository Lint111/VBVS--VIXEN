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
#
# PDB-consolidation (Milestone 1, 2026-07): originally 9 separate gtest executables,
# each independently re-collating RenderGraph's ~150-160MB of /Z7 embedded debug
# info into its own .pdb. Merged into 3 grouped executables (3 files each) — same
# test coverage, far fewer redundant PDB collations. Split into 3 (not 2) because
# test_timer.cpp, test_loop_manager.cpp, and test_node_logging.cpp each define
# their own `int main()` (redundant boilerplate identical to what GTest::gtest_main
# already supplies — a leftover from when each was its own standalone target).
# Two of these landing in the same binary is a hard LNK2005 "main already defined"
# link error (caught by an actual Windows/MSVC build during this milestone, not
# just static review) — so each of the 3 main()-havers must anchor a different
# group. See
# Vixen-Docs/04-Development/RenderGraph-Test-PDB-Consolidation-Plan-2026-07.md.
# ===========================================================================

# ---------------------------------------------------------------------------
# Group A: Timer / ResourceDependencyTracker / RecompileDedup
# ---------------------------------------------------------------------------
# - Timer: High-resolution delta time measurement, elapsed time tracking, reset,
#   precision validation.
# - ResourceDependencyTracker: Resource-to-producer mapping (register, query, update),
#   producer-to-resources bidirectional mapping, nullptr handling and edge cases,
#   clear functionality, multiple resources per producer, resource reassignment,
#   performance characteristics (1000+ resources). NOTE: GetDependenciesForNode()
#   and BuildCleanupDependencies() require full NodeInstance integration and are
#   tested in integration test suites.
# - RenderGraph::RecompileDirtyNodes wave-cascade dedup: full NodeInstance/RenderGraph
#   integration (the case ResourceDependencyTracker's tests defer to an "integration
#   test suite") — builds a real diamond-shaped graph and asserts each node recompiles
#   exactly once per dirty wave, not once per incoming dependency path
#   (Widescreen-Perf-Sweep-Findings-2026-07.md rank 8).

add_executable(test_rendergraph_coresystems_a
    Core/test_timer.cpp
    Core/test_resource_dependency_tracker.cpp
    Core/test_recompile_dedup.cpp
)

target_include_directories(test_rendergraph_coresystems_a PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_rendergraph_coresystems_a PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_coresystems_a PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_coresystems_a)

message(STATUS "[RenderGraph Tests] Added: test_rendergraph_coresystems_a (Timer/ResourceDependencyTracker/RecompileDedup)")

# ---------------------------------------------------------------------------
# Group B: LoopManager / PerFrameResources / GPUQueryManager
# ---------------------------------------------------------------------------
# - LoopManager: Loop registration and ID management, variable/fixed timestep loops
#   (60Hz, 120Hz), three catchup modes (FireAndForget, SingleCorrectiveStep,
#   MultipleSteps), spiral-of-death protection (maxCatchupTime), frame index/step
#   count tracking, multiple independent loops.
# - PerFrameResources: Initialization and frame count management, descriptor set
#   get/set, command buffer get/set, frame data access/validation, ring buffer
#   pattern (2-frame, 3-frame wraparound), edge cases (invalid indices, uninitialized
#   state), cleanup. NOTE: CreateUniformBuffer(), GetUniformBuffer(), and
#   GetUniformBufferMapped() require actual Vulkan device operations and are tested
#   in integration suites.
# - GPUQueryManager (Sprint 6.3 - Phase 0.1): Slot allocation and deallocation,
#   multi-consumer coordination (ProfilerSystem, TimelineCapacityTracker, etc.),
#   per-frame query pool management, timestamp write tracking, result retrieval,
#   edge cases and error handling. NOTE: uses mock VulkanDevice with null handles.

add_executable(test_rendergraph_coresystems_b
    Core/test_loop_manager.cpp
    Core/test_per_frame_resources.cpp
    Core/test_gpu_query_manager.cpp
)

target_include_directories(test_rendergraph_coresystems_b PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_rendergraph_coresystems_b PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_coresystems_b PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_coresystems_b)

message(STATUS "[RenderGraph Tests] Added: test_rendergraph_coresystems_b (LoopManager/PerFrameResources/GPUQueryManager)")

# ---------------------------------------------------------------------------
# Group C: NodeLogging / GPUQueryManager Integration / TimelineCapacityTracker
# ---------------------------------------------------------------------------
# - NodeLogging (audit V-M26 gap): validates NODE_LOG_ERROR/CRITICAL (+ _OBJ
#   variants) reach the terminal through a disabled node logger, mirroring
#   Logger::Log()'s own Error/Critical bypass — the macros previously
#   short-circuited on nodeLogger->IsEnabled() before that bypass ever ran.
# - GPUQueryManager Integration (Sprint 6.3 - Phase 0.1+): integration tests with
#   real Vulkan device and GPU queries. Currently contains placeholder test - full
#   tests deferred to Phase 0.2 (real query pool allocation, timestamp writes to
#   command buffers, multi-consumer coordination with actual GPU, per-frame query
#   management with frames-in-flight, result retrieval with real GPU timing data).
# - TimelineCapacityTracker (Sprint 6.3 - Phase 1.1-1.4): frame lifecycle
#   (BeginFrame, EndFrame), GPU/CPU time recording and accumulation, utilization
#   computation, history tracking (60-300 frame deque), average utilization over N
#   frames, bottleneck identification, damped hysteresis (±10% max, 5% deadband),
#   adaptive task count scaling, configuration management. NOTE: mocks
#   GPUPerformanceLogger composition; full integration tests are in the integration
#   test suite.

add_executable(test_rendergraph_coresystems_c
    Core/test_node_logging.cpp
    Core/test_gpu_query_manager_integration.cpp
    Core/test_timeline_capacity_tracker.cpp
)

target_include_directories(test_rendergraph_coresystems_c PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_rendergraph_coresystems_c PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_coresystems_c PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_coresystems_c)

message(STATUS "[RenderGraph Tests] Added: test_rendergraph_coresystems_c (NodeLogging/GPUQueryManagerIntegration/TimelineCapacityTracker)")
