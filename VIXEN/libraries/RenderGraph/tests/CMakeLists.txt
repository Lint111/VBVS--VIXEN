cmake_minimum_required(VERSION 3.15)
project(RenderGraphTests)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

add_executable(test_rendergraph_basic
    test_rendergraph_basic.cpp
    test_rendergraph_dependency.cpp
)

# Ensure tests can include generated headers and centralized test mocks
# (Allows includes like <generated/sdi/Name.h> and <RenderGraph/tests/TestMocks.h>)
include_directories(${VIXEN_ROOT})

# Link against the RenderGraph library target - includes propagate automatically
# RenderGraph target exposes its include directory as PUBLIC, no manual include_directories needed
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_basic PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_basic)

add_executable(test_rendergraph_dependency
    test_rendergraph_dependency.cpp
    test_array_slot_dedup.cpp
    test_multiple_producers.cpp
)

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_dependency PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_dependency)

add_executable(test_typednode_helpers test_typednode_helpers.cpp)

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_typednode_helpers PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_typednode_helpers)

# ===========================================================================
# LightingConfig Parity Test (Sampled Lighting Inc0 M1 — generated Light /
# LightingConfig [GpuStruct] drift-guard; see codegen/config-schemas/LightingConfig.cs)
# ===========================================================================
add_executable(test_lightingconfig_parity test_lightingconfig_parity.cpp)

if(TARGET RenderGraph)
    target_link_libraries(test_lightingconfig_parity PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_lightingconfig_parity PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_lightingconfig_parity PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_lightingconfig_parity)

# ===========================================================================
# Brdf CPU Mirror Test (Sampled Lighting Inc0 M2 — gpu-shader-debug mirror of
# shaders/Brdf.glsl's evalBRDF, verified against an independent scalar reference)
# ===========================================================================
add_executable(test_brdf_mirror test_brdf_mirror.cpp)

if(TARGET RenderGraph)
    target_link_libraries(test_brdf_mirror PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_brdf_mirror PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_brdf_mirror PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_brdf_mirror)

# ===========================================================================
# TraceWorld CPU Mirror Test (Sampled Lighting Inc1 M2 — gpu-shader-debug mirror of
# shaders/TraceWorld.glsl's TraceWorldShadow any-hit occlusion test, verified against
# an independent AABB-list reference using Data/VoxelTraversal.h's Ray/AABB/IntersectRayAABB)
# ===========================================================================
add_executable(test_traceworld_mirror test_traceworld_mirror.cpp)

if(TARGET RenderGraph)
    target_link_libraries(test_traceworld_mirror PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_traceworld_mirror PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_traceworld_mirror PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_traceworld_mirror)

# ===========================================================================
# VNDF CPU Mirror Test (Sampled Lighting Inc0 M4 — gpu-shader-debug mirror of
# shaders/Sampling.glsl's sampleGGXVNDF/vndfPdf, verified against two
# independent references: a from-scratch analytic VNDF density and the
# original Heitz 2018 sampler construction)
# ===========================================================================
add_executable(test_vndf_mirror test_vndf_mirror.cpp)

if(TARGET RenderGraph)
    target_link_libraries(test_vndf_mirror PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_vndf_mirror PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_vndf_mirror PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_vndf_mirror)

# ===========================================================================
# Sampling.glsl SPIR-V Compile Gate (Sampled Lighting Inc0 M4 — proves
# shaders/Sampling.glsl #includes and compiles cleanly through the real
# runtime shader pipeline; ShaderManagement's ShaderBundleBuilder resolves
# #include via AddIncludePath(VIXEN_SHADER_SOURCE_DIR), same wiring
# BuildRenderGraph.cpp uses for the live app's .comp shaders)
# ===========================================================================
add_executable(test_sampling_compile_gate test_sampling_compile_gate.cpp)

if(TARGET RenderGraph)
    target_link_libraries(test_sampling_compile_gate PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_sampling_compile_gate PRIVATE GTest::gtest_main ShaderManagement)
endif()

target_compile_definitions(test_sampling_compile_gate PRIVATE
    VIXEN_SHADER_SOURCE_DIR="${VIXEN_SHADER_SOURCE_DIR}"
)

set_target_properties(test_sampling_compile_gate PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_sampling_compile_gate)

add_executable(test_node_parameter_manager Core/test_node_parameter_manager.cpp)

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_node_parameter_manager PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_node_parameter_manager PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_node_parameter_manager PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_node_parameter_manager)

# Typed runtime accumulation-gather (InAll<T> fan-in) — CPU-only, no device
add_executable(test_accumulation_gather test_accumulation_gather.cpp)

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_accumulation_gather PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_accumulation_gather PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_accumulation_gather PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_accumulation_gather)

# UI hit-mask math (Ui/UiHitMask.h) — pure CPU, no RmlUi/Vulkan/GPU. The pngwrite TU compiles
# stb_image_write for the Image round-trip (the read side comes from RenderGraph's link closure).
if(TARGET RenderGraph)
    add_executable(test_ui_hit_mask
        test_ui_hit_mask.cpp
        test_ui_hit_mask_pngwrite.cpp
    )

    target_link_libraries(test_ui_hit_mask PRIVATE GTest::gtest_main RenderGraph)
    if(TARGET stb)
        target_link_libraries(test_ui_hit_mask PRIVATE stb)  # stb_image_write.h for the test writer
    endif()

    set_target_properties(test_ui_hit_mask PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_ui_hit_mask)

    message(STATUS "✓ test_ui_hit_mask configured (UI hit-mask math)")
else()
    message(STATUS "⊗ test_ui_hit_mask skipped (RenderGraph target not available)")
endif()

# Additional typed-node tests
target_sources(test_typednode_helpers PRIVATE test_executeonly_behavior.cpp)

# ===========================================================================
# Type System Tests - CONSOLIDATED
# ===========================================================================
# Validates compile-time type system features:
# - Array type validation, field extraction, resource gathering
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_type_system.cmake)

# ===========================================================================
# Graph Systems Tests - CONSOLIDATED
# ===========================================================================
# Validates graph-related systems:
# - Graph topology, resource management
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_graph_systems.cmake)

# ===========================================================================
# Core Infrastructure Tests - CONSOLIDATED
# ===========================================================================
# Validates core infrastructure classes:
# - Timer, LoopManager, ResourceDependencyTracker, PerFrameResources
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_core_systems.cmake)

# ===========================================================================
# Critical Nodes Tests (Priority 3)
# ===========================================================================
# These test suites validate critical infrastructure and synchronization nodes:
# - DeviceNode: Vulkan device initialization
# - WindowNode: Window and surface creation
# - CommandPoolNode: Command buffer management
# - SwapChainNode: Swapchain and presentation
# - FrameSyncNode: Frame synchronization primitives
# Unit tests compatible with VULKAN_TRIMMED_BUILD.
# Integration tests require full Vulkan SDK.

include(test_critical_nodes.cmake)

# ===========================================================================
# Fail-Scenario Simulation Tests
# ===========================================================================
# Validates the fail-scenario declaration schema, self-registering
# ScenarioRegistry, and (later milestones) the live sweep harness.
# Gated on VIXEN_FAIL_SCENARIOS (returns early when OFF).

include(test_fail_scenarios.cmake)

# ===========================================================================
# Descriptor Resource Gatherer - Comprehensive Test Suite
# ===========================================================================
# This test suite validates DescriptorResourceGathererNode (Phase G):
# - Order-agnostic binding connections
# - SDI naming.h integration
# - Expected failures and edge cases
# - Full validation coverage
# Requires GoogleTest framework.
#
# NOTE: This test has outdated API calls and requires refactoring to match
# the current system architecture. Temporarily disabled.
# TODO: Refactor test to use current RenderGraph and SDI APIs

if(FALSE)  # Disabled due to API incompatibilities
if(TARGET GTest::gtest_main)
    add_executable(test_descriptor_gatherer_comprehensive
        test_descriptor_gatherer_comprehensive.cpp
    )

    # Link against library targets - includes propagate automatically via VIXEN_ROOT
    target_include_directories(test_descriptor_gatherer_comprehensive PRIVATE
        ${VIXEN_ROOT}/application/main/include
    )

    target_link_libraries(test_descriptor_gatherer_comprehensive PRIVATE
        GTest::gtest_main
        RenderGraph
        ShaderManagement
    )

    if(TARGET Vulkan::Vulkan)
        target_link_libraries(test_descriptor_gatherer_comprehensive PRIVATE Vulkan::Vulkan)
    endif()

    gtest_discover_tests(test_descriptor_gatherer_comprehensive)

    message(STATUS "✓ test_descriptor_gatherer_comprehensive configured")
else()
    message(STATUS "⊗ test_descriptor_gatherer_comprehensive skipped (GoogleTest not available)")
endif()
endif()  # Disabled due to API incompatibilities

message(STATUS "⊗ test_descriptor_gatherer_comprehensive skipped (outdated API - requires refactoring)")

# ===========================================================================
# PassThroughStorage Handle Types Test
# ===========================================================================
# This test validates PassThroughStorage accepts platform handle types used by nodes.
# Tests compile-time type registration (no Vulkan runtime required).

if(TARGET GTest::gtest_main)
    add_executable(test_passthroughstorage_handles
        test_passthroughstorage_handles.cpp
    )

    # Link against the RenderGraph library target - includes propagate automatically
    target_link_libraries(test_passthroughstorage_handles PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Visual Studio solution folder organization
    set_target_properties(test_passthroughstorage_handles PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_passthroughstorage_handles)

    message(STATUS "✓ test_passthroughstorage_handles configured")
else()
    message(STATUS "⊗ test_passthroughstorage_handles skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Connection Concepts Test (Sprint 6.0.1)
# ===========================================================================
# This test validates C++20 concepts for the unified connection system:
# - SlotReference, BindingReference, AccumulationSlot concepts
# - ConnectionMetadata concept and helper types
# - Legacy type trait wrappers
# Tests are primarily compile-time (static_assert).

if(TARGET GTest::gtest_main)
    add_executable(test_connection_concepts
        test_connection_concepts.cpp
    )

    # Link against the RenderGraph library target - includes propagate automatically
    target_link_libraries(test_connection_concepts PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Visual Studio solution folder organization
    set_target_properties(test_connection_concepts PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_connection_concepts)

    message(STATUS "✓ test_connection_concepts configured (Sprint 6.0.1)")
else()
    message(STATUS "⊗ test_connection_concepts skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Connection Rule Test (Sprint 6.0.1)
# ===========================================================================
# This test validates the ConnectionRule system:
# - SlotDescriptor and BindingDescriptor creation
# - ConnectionRuleRegistry rule matching
# - DirectConnectionRule validation logic
# Tests use GoogleTest framework.

if(TARGET GTest::gtest_main)
    add_executable(test_connection_rule
        test_connection_rule.cpp
    )

    # Link against the RenderGraph library target
    target_link_libraries(test_connection_rule PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Visual Studio solution folder organization
    set_target_properties(test_connection_rule PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_connection_rule)

    message(STATUS "✓ test_connection_rule configured (Sprint 6.0.1)")
else()
    message(STATUS "⊗ test_connection_rule skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Sprint 6.1: Group-Based Dispatch Tests
# ===========================================================================
# Tests for MultiDispatchNode GROUP_INPUTS and GroupKeyModifier
# - GroupKeyModifier validation and metadata storage
# - DispatchPass validation (IsValid())
# - Group partitioning logic
# - Deterministic ordering (std::map)
# - Backward compatibility with QueueDispatch()

if(TARGET GTest::gtest_main)
    add_executable(test_group_dispatch
        test_group_dispatch.cpp
    )

    target_link_libraries(test_group_dispatch PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_group_dispatch PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_group_dispatch)

    message(STATUS "✓ test_group_dispatch configured (Sprint 6.1)")
else()
    message(STATUS "⊗ test_group_dispatch skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Sprint 6.2: TaskQueue Budget-Aware System Tests
# ===========================================================================
# Tests for TaskQueue template and MultiDispatchNode integration
# - Budget enforcement (strict/lenient modes)
# - Priority-based scheduling
# - Overflow protection
# - TaskBudget configuration and presets
# - Integration with MultiDispatchNode
# - Backward compatibility with Sprint 6.1

if(TARGET GTest::gtest_main)
    # Unit tests for TaskQueue template
    add_executable(test_task_queue
        test_task_queue.cpp
    )

    target_link_libraries(test_task_queue PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_task_queue PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_task_queue)

    message(STATUS "✓ test_task_queue configured (Sprint 6.2)")

    # Integration tests for MultiDispatchNode + TaskQueue
    add_executable(test_multidispatch_integration
        test_multidispatch_integration.cpp
    )

    target_link_libraries(test_multidispatch_integration PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_multidispatch_integration PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_multidispatch_integration)

    message(STATUS "✓ test_multidispatch_integration configured (Sprint 6.2)")
else()
    message(STATUS "⊗ Sprint 6.2 tests skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Sprint 6.3: Phase 3.1 - PredictionErrorTracker Tests
# ===========================================================================
# Tests for prediction error tracking system
# - Error ratio computation
# - Rolling statistics (mean, variance, bias)
# - Correction factor generation
# - TimelineCapacityTracker integration
# NOTE: Other Sprint 6.3 tests (GPUQueryManager, TimelineCapacityTracker)
#       are defined in test_core_systems.cmake

if(TARGET GTest::gtest_main)
    add_executable(test_prediction_error_tracker
        test_prediction_error_tracker.cpp
    )

    target_link_libraries(test_prediction_error_tracker PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_prediction_error_tracker PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_prediction_error_tracker)

    message(STATUS "✓ test_prediction_error_tracker configured (Sprint 6.3 Phase 3.1)")

    # TaskProfile unit tests (Phase 3.2a)
    add_executable(test_task_profile
        test_task_profile.cpp
    )

    target_link_libraries(test_task_profile PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_task_profile PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_task_profile)

    message(STATUS "✓ test_task_profile configured (Sprint 6.3 Phase 3.2a)")
else()
    message(STATUS "⊗ Sprint 6.3 Phase 3 tests skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Sprint 6.4: ResourceAccessTracker Tests (Phase 0)
# ===========================================================================
# Tests for resource access conflict detection
# - Read/write access tracking
# - Conflict detection (write-write, write-read)
# - Multi-resource scenarios
# - GraphTopology integration

if(TARGET GTest::gtest_main)
    add_executable(test_resource_access_tracker
        test_resource_access_tracker.cpp
    )

    target_link_libraries(test_resource_access_tracker PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_resource_access_tracker PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_resource_access_tracker)

    message(STATUS "✓ test_resource_access_tracker configured (Sprint 6.4 Phase 0)")

    # WaveScheduler Tests (Phase 1)
    add_executable(test_wave_scheduler
        test_wave_scheduler.cpp
    )

    target_link_libraries(test_wave_scheduler PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_wave_scheduler PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_wave_scheduler)

    message(STATUS "✓ test_wave_scheduler configured (Sprint 6.4 Phase 1)")

    # Note: test_tbb_graph_executor removed - functionality merged into TBBVirtualTaskExecutor

    # ===========================================================================
    # VirtualTask Tests (Sprint 6.5 - Task-Level Parallelism)
    # ===========================================================================

    add_executable(test_virtual_task
        test_virtual_task.cpp
    )

    target_link_libraries(test_virtual_task PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_virtual_task PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_virtual_task)

    message(STATUS "✓ test_virtual_task configured (Sprint 6.5 Phase 0)")

    # VirtualResourceAccessTracker Tests (Sprint 6.5 - Phase 1)
    add_executable(test_virtual_resource_access_tracker
        test_virtual_resource_access_tracker.cpp
    )

    target_link_libraries(test_virtual_resource_access_tracker PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_virtual_resource_access_tracker PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_virtual_resource_access_tracker)

    message(STATUS "✓ test_virtual_resource_access_tracker configured (Sprint 6.5 Phase 1)")

    # TaskDependencyGraph Tests (Sprint 6.5 - Phase 2)
    add_executable(test_task_dependency_graph
        test_task_dependency_graph.cpp
    )

    target_link_libraries(test_task_dependency_graph PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_task_dependency_graph PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_task_dependency_graph)

    message(STATUS "✓ test_task_dependency_graph configured (Sprint 6.5 Phase 2)")

    # TBBVirtualTaskExecutor Tests (Sprint 6.5 - Phase 4)
    add_executable(test_tbb_virtual_task_executor
        test_tbb_virtual_task_executor.cpp
    )

    target_link_libraries(test_tbb_virtual_task_executor PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Copy TBB DLL for test execution
    if(TARGET TBB::tbb)
        add_custom_command(TARGET test_tbb_virtual_task_executor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_tbb_virtual_task_executor>
        )
    endif()

    set_target_properties(test_tbb_virtual_task_executor PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_tbb_virtual_task_executor)

    message(STATUS "✓ test_tbb_virtual_task_executor configured (Sprint 6.5 Phase 4)")

    # VirtualTask Integration Tests (Sprint 6.5 - Phase 6)
    add_executable(test_virtual_task_integration
        test_virtual_task_integration.cpp
    )

    target_link_libraries(test_virtual_task_integration PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Copy TBB DLL for test execution
    if(TARGET TBB::tbb)
        add_custom_command(TARGET test_virtual_task_integration POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_virtual_task_integration>
        )
    endif()

    set_target_properties(test_virtual_task_integration PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_virtual_task_integration)

    message(STATUS "✓ test_virtual_task_integration configured (Sprint 6.5 Phase 6)")
else()
    message(STATUS "⊗ Sprint 6.4 tests skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Voxel Systems Tests - CONSOLIDATED (Phase H)
# ===========================================================================
# Validates voxel infrastructure:
# - Octree construction, scene generation, ray traversal
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_voxel_systems.cmake)

# ===========================================================================
# S1: UIRenderNode HUD data model smoke test (headless — no Vulkan required)
# ===========================================================================
# Exercises:
#   - RmlUi init with VixenRmlSystemInterface + NullRenderInterface
#   - Context::CreateDataModel("hud") + Bind("tick") + Bind("bodyCount")
#   - LoadDocument("hud.rml") parses without error
#   - SetHudData-equivalent (struct update + DirtyVariable + Update())
#   - GetInnerRML() contains the substituted values 42 and 7
#   - Rml::Shutdown() cleans up cleanly
# Assets (font + hud.rml/hud.rcss) are staged next to the binary by POST_BUILD.
#
# View Contract Inc-2 (Task 6): the HUD data-model + faction/event projection now
# lives in the main app's Vixen::App::HudView consumer, not UIRenderNode. This test
# compiles HudView.cpp directly (not the VixenApp library, to avoid pulling in the
# app's gaia-touching TUs) and adds application/main/include so "graph/HudView.h" /
# "Generated/Hud.g.h" resolve — mirrors test_view_hud_golden's identical include dir.
if(TARGET GTest::gtest_main)
    add_executable(test_ui_hud_smoke
        test_ui_hud_smoke.cpp
        ${CMAKE_SOURCE_DIR}/application/main/source/graph/HudView.cpp
    )

    target_link_libraries(test_ui_hud_smoke PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    target_include_directories(test_ui_hud_smoke PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)

    # Stage the UI assets (font + rml + rcss) next to the test binary so
    # hud.rml / font paths resolve at test runtime.
    set(_UI_ASSETS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ui")
    add_custom_command(TARGET test_ui_hud_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:test_ui_hud_smoke>/assets/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_UI_ASSETS_SRC}"
            "$<TARGET_FILE_DIR:test_ui_hud_smoke>/assets/ui"
        COMMENT "Staging UI assets for test_ui_hud_smoke"
    )

    # Copy TBB DLL (transitive dep via SVO → RenderGraph → TBB)
    if(TARGET TBB::tbb)
        add_custom_command(TARGET test_ui_hud_smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:test_ui_hud_smoke>
            COMMENT "Copying TBB DLL for test_ui_hud_smoke"
        )
    endif()

    set_target_properties(test_ui_hud_smoke PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_ui_hud_smoke)

    message(STATUS "✓ test_ui_hud_smoke configured (S1: HUD data model headless smoke)")
else()
    message(STATUS "⊗ test_ui_hud_smoke skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-2: Hud golden-mirror gate (headless — no Vulkan required)
# ===========================================================================
# Proves the generated Generated/Hud.g.h RmlUi data-model registration block
# (a) compiles + binds against a real Rml::DataModelConstructor, and (b) matches,
# as a normalized ordered call sequence, the canonical Hud [View] schema field order.

if(TARGET GTest::gtest_main)
    add_executable(test_view_hud_golden
        test_view_hud_golden.cpp
    )

    target_link_libraries(test_view_hud_golden PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Hud.g.h is now a CONSUMER artifact (application/main) — the golden still verifies it, but
    # needs the app's include dir to resolve "Generated/Hud.g.h" (View Contract Inc-2, Task 4).
    target_include_directories(test_view_hud_golden PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)

    target_compile_definitions(test_view_hud_golden PRIVATE
        VIEW_HUD_G_H="${CMAKE_SOURCE_DIR}/application/main/include/Generated/Hud.g.h")

    set_target_properties(test_view_hud_golden PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_view_hud_golden)

    message(STATUS "✓ test_view_hud_golden configured (View Contract Inc-2: golden-mirror gate)")
else()
    message(STATUS "⊗ test_view_hud_golden skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Inc-A2: EditorLayers golden-mirror gate (headless — no Vulkan required)
# ===========================================================================
# Mirrors test_view_hud_golden exactly, for the editor layer view's [View] schema instead of Hud's.

if(TARGET GTest::gtest_main)
    add_executable(test_view_editor_layers_golden
        test_view_editor_layers_golden.cpp
    )

    target_link_libraries(test_view_editor_layers_golden PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # EditorLayers.g.h is a CONSUMER artifact (application/editor) — needs the editor's include
    # dir to resolve "Generated/EditorLayers.g.h" (mirrors test_view_hud_golden's application/main
    # include-dir need).
    target_include_directories(test_view_editor_layers_golden PRIVATE ${CMAKE_SOURCE_DIR}/application/editor/include)

    target_compile_definitions(test_view_editor_layers_golden PRIVATE
        VIEW_EDITOR_LAYERS_G_H="${CMAKE_SOURCE_DIR}/application/editor/include/Generated/EditorLayers.g.h")

    set_target_properties(test_view_editor_layers_golden PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_view_editor_layers_golden)

    message(STATUS "✓ test_view_editor_layers_golden configured (Inc-A2: golden-mirror gate)")
else()
    message(STATUS "⊗ test_view_editor_layers_golden skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Inc-B: external-change .changed<LayerMask>() reconcile proof (headless — no Vulkan required)
# ===========================================================================
# Task 4's key proof: a deterministic EXTERNAL Gaia write (not via WriteU32/ToggleLayer/the
# same-frame echo) reconciles into the bound EditorLayersView "layers" model. Two TUs, same
# robin_hood ODR isolation the editor itself uses (EditorLayersViewBridge.h's file header):
# GaiaLayerReconcileTestBridge.cpp is the gaia-only side (no RmlUi); the test .cpp is the
# RmlUi-only side (no gaia.h) -- see both files' own header comments.

if(TARGET GTest::gtest_main)
    add_executable(test_view_editor_layers_reconcile
        test_view_editor_layers_reconcile.cpp
        ${CMAKE_SOURCE_DIR}/application/editor/source/GaiaLayerReconcileTestBridge.cpp
    )

    target_link_libraries(test_view_editor_layers_reconcile PRIVATE
        GTest::gtest_main
        RenderGraph
        AppFlow
        GaiaVoxelWorld::GaiaVoxelWorld
    )

    # Same editor-consumer-artifact include need as test_view_editor_layers_golden (EditorLayersView.h
    # / EditorLayers.g.h / the Inc-B bridge headers all live under application/editor/include).
    target_include_directories(test_view_editor_layers_reconcile PRIVATE ${CMAKE_SOURCE_DIR}/application/editor/include)

    set_target_properties(test_view_editor_layers_reconcile PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_view_editor_layers_reconcile)

    message(STATUS "✓ test_view_editor_layers_reconcile configured (Inc-B: external-change reconcile proof)")
else()
    message(STATUS "⊗ test_view_editor_layers_reconcile skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Inc-C: multi-instance selection (IViewSelectionProvider) proof (headless — no Vulkan required)
# ===========================================================================
# Task 4's key proof: a committed SUBSET of Gaia entities tagged `Selected` yields exactly that
# subset (in stable order) via IViewSelectionProvider, and resolving a selection index through
# SelectionResolvingViewDataProvider to the EXISTING (Inc-B, unmodified) GaiaLayerViewDataProvider
# reads/writes the CORRECT entity. Pure Gaia/AppFlow TU -- no RmlUi anywhere in this test (Task 1's
# decision), so no bridge-split TU is needed (contrast with test_view_editor_layers_reconcile.cpp,
# which DOES need the split because it touches a real RmlUi data-model).

if(TARGET GTest::gtest_main)
    add_executable(test_view_selection_provider
        test_view_selection_provider.cpp
    )

    target_link_libraries(test_view_selection_provider PRIVATE
        GTest::gtest_main
        RenderGraph
        AppFlow
        GaiaVoxelWorld::GaiaVoxelWorld
    )

    # GaiaViewSelectionProvider.h / SelectionComponents.h / SelectionResolvingViewDataProvider.h
    # live under application/editor/include, same as the Inc-B provider headers this test builds on.
    target_include_directories(test_view_selection_provider PRIVATE ${CMAKE_SOURCE_DIR}/application/editor/include)

    set_target_properties(test_view_selection_provider PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_view_selection_provider)

    message(STATUS "✓ test_view_selection_provider configured (Inc-C: multi-instance selection proof)")
else()
    message(STATUS "⊗ test_view_selection_provider skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Inc-D Milestone 1: set-mutation dispatch (SetMutationDispatch.h) smoke proof (headless)
# ===========================================================================
# Forward-dispatch-only smoke test: one ActionStack group, one identity write per selected entity,
# through the EXISTING (unchanged) Inc-B/C provider chain. Undo/redo restore + dead-entity-at-undo
# skip proof is Milestone 2 (Task 3/4) -- a separate test file follows this one's pattern.

if(TARGET GTest::gtest_main)
    add_executable(test_set_mutation_dispatch
        test_set_mutation_dispatch.cpp
    )

    target_link_libraries(test_set_mutation_dispatch PRIVATE
        GTest::gtest_main
        RenderGraph
        AppFlow
        GaiaVoxelWorld::GaiaVoxelWorld
    )

    # SetMutationDispatch.h / GaiaViewSelectionProvider.h / SelectionComponents.h live under
    # application/editor/include, same as the Inc-B/C provider headers this test builds on.
    target_include_directories(test_set_mutation_dispatch PRIVATE ${CMAKE_SOURCE_DIR}/application/editor/include)

    set_target_properties(test_set_mutation_dispatch PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_set_mutation_dispatch)

    message(STATUS "✓ test_set_mutation_dispatch configured (Inc-D Milestone 1: set-mutation dispatch proof)")
else()
    message(STATUS "⊗ test_set_mutation_dispatch skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-2b: ViewBlob/ViewKind/ViewValue reflection-blob contract
# ===========================================================================
# Pure CPU test of the constexpr descriptor shape + scalar kind/value agreement.
# No RmlUi/Vulkan/GPU required.

if(TARGET GTest::gtest_main)
    add_executable(test_view_blob test_view_blob.cpp)
    target_link_libraries(test_view_blob PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_view_blob PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_view_blob DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_view_blob configured (View Contract Inc-2b: ViewBlob contract)")
else()
    message(STATUS "⊗ test_view_blob skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-2b: ViewStore typed storage + by-field setters
# ===========================================================================
# Pure CPU test of scalar/array storage + blob-validated setter rejection.
# Links RmlUi (via RenderGraph) for Rml::String/DataModelHandle, no GPU/device.

if(TARGET GTest::gtest_main)
    add_executable(test_view_store test_view_store.cpp)
    target_link_libraries(test_view_store PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_view_store PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_view_store DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_view_store configured (View Contract Inc-2b: ViewStore storage)")
else()
    message(STATUS "⊗ test_view_store skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-2b: ViewBlobFile — runtime .viewblob parser
# ===========================================================================
# Pure CPU test of the line-grammar parser (model/version/field/elem) + hard-fail
# validation on malformed input. No RmlUi/Vulkan/GPU touchpoints.

if(TARGET GTest::gtest_main)
    add_executable(test_view_blob_file test_view_blob_file.cpp)
    target_link_libraries(test_view_blob_file PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_view_blob_file PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_view_blob_file DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_view_blob_file configured (View Contract Inc-2b: ViewBlobFile parser)")
else()
    message(STATUS "⊗ test_view_blob_file skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-2b: BlobView — generic IView host, dynamic RmlUi model
# ===========================================================================
# Proves a ViewBlob alone (no per-schema C++) builds a live RmlUi data model
# (scalars + array-of-struct) and that a version mismatch hard-skips registration.

if(TARGET GTest::gtest_main)
    add_executable(test_blob_view test_blob_view.cpp)
    target_link_libraries(test_blob_view PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_blob_view PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_blob_view DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_blob_view configured (View Contract Inc-2b: BlobView dynamic registration)")
else()
    message(STATUS "⊗ test_blob_view skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-3: ViewWireReader — UTVA wire round-trip proof
# ===========================================================================
# Cross-language seam proof: the CanonicalWire buffer built in-test is the byte-for-byte
# twin of the C# HudViewWriter.ToBuffer() golden (Yeroket ViewWriterEmitterTests). Proves
# ViewWireReader::Apply reads back every field via ViewStore, hard-fails on a version
# mismatch (store untouched), and rejects malformed input (bad magic/truncated/field-count
# mismatch/trailing garbage). Pure CPU, no RmlUi rendering/Vulkan/GPU touchpoints.

if(TARGET GTest::gtest_main)
    add_executable(test_view_wire_roundtrip test_view_wire_roundtrip.cpp)
    target_link_libraries(test_view_wire_roundtrip PRIVATE GTest::gtest_main RenderGraph)

    # Hud.blob.g.h is a CONSUMER artifact (application/main) -- needs the app's include dir to
    # resolve "Generated/Hud.blob.g.h" (mirrors test_view_hud_golden's identical include dir).
    target_include_directories(test_view_wire_roundtrip PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)

    set_target_properties(test_view_wire_roundtrip PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_view_wire_roundtrip DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_view_wire_roundtrip configured (View Contract Inc-3: UTVA wire round-trip)")
else()
    message(STATUS "⊗ test_view_wire_roundtrip skipped (GoogleTest not available)")
endif()

# ===========================================================================
# BarrierTypes Tests (auto-sync P1)
# ===========================================================================
# Validates AccessKind -> Vulkan sync-semantics mapping

if(TARGET GTest::gtest_main)
    add_executable(test_barrier_types test_barrier_types.cpp)
    target_link_libraries(test_barrier_types PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_barrier_types PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_barrier_types)
    message(STATUS "✓ test_barrier_types configured (auto-sync P1)")
else()
    message(STATUS "⊗ test_barrier_types skipped (GoogleTest not available)")
endif()

# ===========================================================================
# FrameSyncSchedule Data Types Tests (auto-sync P2)
# ===========================================================================
# Validates FrameSyncSchedule data types (pure structs, no business logic)

if(TARGET GTest::gtest_main)
    add_executable(test_frame_sync_scheduler test_frame_sync_scheduler.cpp)
    target_link_libraries(test_frame_sync_scheduler PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_frame_sync_scheduler PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_frame_sync_scheduler)
    message(STATUS "✓ test_frame_sync_scheduler configured (auto-sync P2)")
else()
    message(STATUS "⊗ test_frame_sync_scheduler skipped (GoogleTest not available)")
endif()

# ===========================================================================
# FrameSyncNode Timeline Semaphore + frameBase Tests (auto-sync P5a M1)
# ===========================================================================
# Pure CPU test of NextFrameBase arithmetic (no Vulkan device required).
# Validates: monotonic advance, no ring collision, zero-stride no-op.

if(TARGET GTest::gtest_main)
    add_executable(test_frame_sync_node_timeline test_frame_sync_node_timeline.cpp)
    target_link_libraries(test_frame_sync_node_timeline PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_frame_sync_node_timeline PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_frame_sync_node_timeline)
    message(STATUS "✓ test_frame_sync_node_timeline configured (auto-sync P5a M1)")
else()
    message(STATUS "⊗ test_frame_sync_node_timeline skipped (GoogleTest not available)")
endif()

# ===========================================================================
# PassGroupSchedule Tests (auto-sync P4 M1)
# ===========================================================================
# Validates BuildPassGroupSchedule node-local barrier baking (pure CPU, no GPU)

if(TARGET GTest::gtest_main)
    add_executable(test_pass_group_schedule test_pass_group_schedule.cpp)
    target_link_libraries(test_pass_group_schedule PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_pass_group_schedule PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_pass_group_schedule)
    message(STATUS "✓ test_pass_group_schedule configured (auto-sync P4 M1)")
else()
    message(STATUS "⊗ test_pass_group_schedule skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Node Self-Registration Anchor (M3)
# ===========================================================================
# Guards the whole-archive requirement: links the RenderGraph facade (which
# whole-archives RenderGraphNodes). If that whole-archive is ever lost, the
# per-node static registrars are stripped and GetNodeTypeCount() collapses to ~0,
# failing this test.
add_executable(test_node_self_registration test_node_self_registration.cpp)
if(TARGET RenderGraph)
    target_link_libraries(test_node_self_registration PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_node_self_registration PRIVATE GTest::gtest_main)
endif()
# Exact expected registration count, derived from the same RENDERGRAPH_NODE_SOURCES list the
# CMakeLists.txt completeness check (above, in the parent CMakeLists.txt) verifies against disk --
# one source of truth, so this can never drift out of sync with the real node count the way a
# second hand-maintained magic number could. One node source (ConstantNode.cpp) registers TWO
# NodeTypes (ConstantNodeType + ShaderConstantNodeType) from a single TU (see
# cmake/VixenNodeLinkage.cmake's header comment) -- RENDERGRAPH_NODE_SOURCES counts TUs, not
# NodeTypes, so the actual registered-type count is (#sources + 1).
list(LENGTH RENDERGRAPH_NODE_SOURCES RENDERGRAPH_NODE_SOURCE_COUNT)
math(EXPR RENDERGRAPH_EXPECTED_NODE_TYPE_COUNT "${RENDERGRAPH_NODE_SOURCE_COUNT} + 1")
target_compile_definitions(test_node_self_registration PRIVATE
    RENDERGRAPH_EXPECTED_NODE_TYPE_COUNT=${RENDERGRAPH_EXPECTED_NODE_TYPE_COUNT})
set_target_properties(test_node_self_registration PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_node_self_registration)

# ===========================================================================
# PassGroupNode Smoke Test (auto-sync P4 M3)
# ===========================================================================
# No GPU required: (1) registry lookup by type, (2) AddComputePass/AddRenderPass
# grow the pass list, (3) SetPasses replaces existing passes.
if(TARGET GTest::gtest_main)
    add_executable(test_pass_group_node_smoke test_pass_group_node_smoke.cpp)
    if(TARGET RenderGraph)
        target_link_libraries(test_pass_group_node_smoke PRIVATE GTest::gtest_main RenderGraph)
    else()
        target_link_libraries(test_pass_group_node_smoke PRIVATE GTest::gtest_main)
    endif()
    set_target_properties(test_pass_group_node_smoke PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_pass_group_node_smoke)
    message(STATUS "✓ test_pass_group_node_smoke configured (auto-sync P4 M3)")
else()
    message(STATUS "⊗ test_pass_group_node_smoke skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Partial Brick Upload Test (Sparse-Mip ESVO LOD Inc1 M2)
# ===========================================================================
# Real Vulkan device required (software rasterizer or Mesa-Dozen on WSL2 — see
# IsAcceptableDevice in the test file). Drives BodyOctreeSceneNode's real
# Setup/Compile/Execute lifecycle against a real BatchedUploader (wired exactly as
# DeviceNode::CreateDeviceBudgetManager does) to verify brick-pool residency gating.
if(TARGET GTest::gtest_main)
    add_executable(test_partial_brick_upload Nodes/test_partial_brick_upload.cpp)
    if(TARGET RenderGraph)
        target_link_libraries(test_partial_brick_upload PRIVATE GTest::gtest_main RenderGraph)
    else()
        target_link_libraries(test_partial_brick_upload PRIVATE GTest::gtest_main)
    endif()
    set_target_properties(test_partial_brick_upload PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_partial_brick_upload)
    message(STATUS "✓ test_partial_brick_upload configured (Sparse-Mip ESVO LOD Inc1 M2)")
else()
    message(STATUS "⊗ test_partial_brick_upload skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Bandwidth A/B Measurement (Sparse-Mip ESVO LOD Inc1 M5, Task 12)
# ===========================================================================
# The increment's gate measurement: N real BodyOctreeSceneNode trees, all-resident
# (baseline) vs all mip-only (Inc1), real BatchedUploaderStats bytes + wall time.
if(TARGET GTest::gtest_main)
    add_executable(test_bandwidth_ab_measurement Nodes/test_bandwidth_ab_measurement.cpp)
    if(TARGET RenderGraph)
        target_link_libraries(test_bandwidth_ab_measurement PRIVATE GTest::gtest_main RenderGraph)
    else()
        target_link_libraries(test_bandwidth_ab_measurement PRIVATE GTest::gtest_main)
    endif()
    set_target_properties(test_bandwidth_ab_measurement PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_bandwidth_ab_measurement)
    message(STATUS "✓ test_bandwidth_ab_measurement configured (Sparse-Mip ESVO LOD Inc1 M5)")
else()
    message(STATUS "⊗ test_bandwidth_ab_measurement skipped (GoogleTest not available)")
endif()

# ===========================================================================
# BufferSyncGathererNode Tests (Sampled Lighting Inc3 M5)
# ===========================================================================
# Config-shape + pre-registration + node-type-registration checks — the generic
# variadic-VkBuffer-gatherer that replaces ComputeStageNodeConfig's old fixed named
# buffer-sync slots. Real correctness is validated by the live render-graph gate (see
# gate-artifacts/inc3-m5-hashes.txt), mirroring every other gatherer node in this codebase.
if(TARGET GTest::gtest_main)
    add_executable(test_buffer_sync_gatherer_node Nodes/test_buffer_sync_gatherer_node.cpp)
    if(TARGET RenderGraph)
        target_link_libraries(test_buffer_sync_gatherer_node PRIVATE GTest::gtest_main RenderGraph)
    else()
        target_link_libraries(test_buffer_sync_gatherer_node PRIVATE GTest::gtest_main)
    endif()
    set_target_properties(test_buffer_sync_gatherer_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_buffer_sync_gatherer_node)
    message(STATUS "✓ test_buffer_sync_gatherer_node configured (Sampled Lighting Inc3 M5)")
else()
    message(STATUS "⊗ test_buffer_sync_gatherer_node skipped (GoogleTest not available)")
endif()

# ===========================================================================
# CompileTimeResourceSystem Tests
# ===========================================================================
# Test suite for compile-time type system validation, caching, and traits.
# Moved from root CMakeLists.txt for proper organization.

add_subdirectory(CompileTimeResourceSystem)
