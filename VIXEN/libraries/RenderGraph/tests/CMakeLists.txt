cmake_minimum_required(VERSION 3.15)
project(RenderGraphTests)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

# ===========================================================================
# RenderGraph Core Basics (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges 4 formerly-separate targets, each a bare GTest::gtest_main + RenderGraph
# link surface, into one executable — same test coverage, far fewer redundant
# ~150-160MB /Z7 PDB collations. See
# Vixen-Docs/04-Development/RenderGraph-Test-PDB-Consolidation-Plan-2026-07.md.
#
# - test_rendergraph_basic / test_rendergraph_dependency: dependency-tracking
#   basics (array-slot dedup, multiple-producer detection).
# - test_typednode_helpers (+ test_executeonly_behavior, formerly appended via
#   target_sources): TypedNode helper coverage, including ExecuteOnly node
#   behavior.
# - test_lightingconfig_parity (Sampled Lighting Inc0 M1): generated Light /
#   LightingConfig [GpuStruct] drift-guard; see
#   codegen/config-schemas/LightingConfig.cs.
add_executable(test_rendergraph_core
    test_rendergraph_basic.cpp
    test_rendergraph_dependency.cpp
    test_array_slot_dedup.cpp
    test_multiple_producers.cpp
    test_typednode_helpers.cpp
    test_executeonly_behavior.cpp
    test_lightingconfig_parity.cpp
)

# Ensure tests can include generated headers and centralized test mocks
# (Allows includes like <generated/sdi/Name.h> and <RenderGraph/tests/TestMocks.h>)
include_directories(${VIXEN_ROOT})

# Link against the RenderGraph library target - includes propagate automatically
# RenderGraph target exposes its include directory as PUBLIC, no manual include_directories needed
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_core PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_core PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_core PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_core)

# ===========================================================================
# RenderGraph Shader Mirrors (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges 3 formerly-separate bare-link-surface targets into one executable.
# test_sampling_compile_gate is NOT included here — it has a compile definition
# (VIXEN_SHADER_SOURCE_DIR) and a conditional ShaderManagement link in its
# else() branch, a real build-semantics complication, so it stays standalone.
#
# - test_brdf_mirror (Sampled Lighting Inc0 M2): gpu-shader-debug mirror of
#   shaders/Brdf.glsl's evalBRDF, verified against an independent scalar reference.
# - test_traceworld_mirror (Sampled Lighting Inc1 M2): gpu-shader-debug mirror of
#   shaders/TraceWorld.glsl's TraceWorldShadow any-hit occlusion test, verified
#   against an independent AABB-list reference using Data/VoxelTraversal.h's
#   Ray/AABB/IntersectRayAABB.
# - test_vndf_mirror (Sampled Lighting Inc0 M4): gpu-shader-debug mirror of
#   shaders/Sampling.glsl's sampleGGXVNDF/vndfPdf, verified against two independent
#   references: a from-scratch analytic VNDF density and the original Heitz 2018
#   sampler construction.
add_executable(test_rendergraph_shadermirrors
    test_brdf_mirror.cpp
    test_traceworld_mirror.cpp
    test_vndf_mirror.cpp
)

if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_shadermirrors PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_shadermirrors PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_rendergraph_shadermirrors PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_shadermirrors)

# HDR3 CPU mirror of ExposureMeter.comp's deterministic sun/cosmic percentile clamp.
add_executable(test_exposure_meter_mirror Nodes/test_exposure_meter_mirror.cpp)
target_compile_features(test_exposure_meter_mirror PRIVATE cxx_std_17)
set_target_properties(test_exposure_meter_mirror PROPERTIES FOLDER "Tests/RenderGraph Tests")
add_test(NAME test_exposure_meter_mirror COMMAND test_exposure_meter_mirror)

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
# RenderGraph Node/Connection Helpers (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges 3 formerly-separate bare-link-surface targets into one executable.
# test_connection_rule is deliberately NOT included here — it defines a file-scope
# `struct MockBindingRef` with a DIFFERENT member layout than test_connection_concepts's
# own `struct MockBindingRef` (2 fields vs. 3, including a const char* name). Linking
# both into the same binary would be an ODR violation (ill-formed, no diagnostic
# required) rather than a clean build/link error, so test_connection_rule stays out of
# this group — see its own standalone target further below.
# test_connection_concepts is ALSO deliberately NOT included here (a second,
# independent reason it can't join this group beyond the ODR issue above): its
# `main()` is hand-rolled and never calls RUN_ALL_TESTS() — it just prints
# static_assert-pass banners to stdout (a pre-existing property of this file,
# not something this milestone changed). Verified live: merging it into
# test_rendergraph_nodeconn made this file's main() win link-time main()
# selection for the WHOLE binary, which silently stopped
# test_node_parameter_manager/test_accumulation_gather/test_passthroughstorage_handles's
# real TEST() cases from ever running (their gtest registrations existed but
# RUN_ALL_TESTS() was never called) — a real regression caught only by actually
# running the merged binary, not by static review. test_connection_concepts stays
# standalone below.
#
# - test_node_parameter_manager: NodeParameterManager coverage.
# - test_accumulation_gather: typed runtime accumulation-gather (InAll<T> fan-in) —
#   CPU-only, no device.
# - test_passthroughstorage_handles: validates PassThroughStorage accepts platform
#   handle types used by nodes; compile-time type registration, no Vulkan runtime.

if(TARGET GTest::gtest_main)
    add_executable(test_rendergraph_nodeconn
        Core/test_node_parameter_manager.cpp
        test_accumulation_gather.cpp
        test_passthroughstorage_handles.cpp
    )

    # Link against the RenderGraph library target - includes propagate automatically
    target_link_libraries(test_rendergraph_nodeconn PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Visual Studio solution folder organization
    set_target_properties(test_rendergraph_nodeconn PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_rendergraph_nodeconn)

    message(STATUS "✓ test_rendergraph_nodeconn configured (NodeParameterManager/AccumulationGather/PassThroughStorage)")
else()
    message(STATUS "⊗ test_rendergraph_nodeconn skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Connection Concepts Test (Sprint 6.0.1) — kept standalone, PDB-consolidation Milestone 1
# ===========================================================================
# This test validates C++20 concepts for the unified connection system:
# - SlotReference, BindingReference, AccumulationSlot concepts
# - ConnectionMetadata concept and helper types
# - Legacy type trait wrappers
# Tests are primarily compile-time (static_assert).
#
# NOT merged with anything: (1) an ODR collision with test_connection_rule's own
# differently-shaped `struct MockBindingRef` (see test_rendergraph_nodeconn's
# comment above), and (2) independently, its hand-rolled `main()` never calls
# RUN_ALL_TESTS() — it monopolizes main() for whatever binary it's linked into,
# silently preventing any other merged file's real TEST() cases from running.
# Confirmed live during this milestone: merging it into test_rendergraph_nodeconn
# silently zeroed out that group's other 3 files' test execution. Stays standalone.

# Semantic Shader Wiring S2: provider-registry wire plan (pure data-level,
# no device) — standalone for the same main()-monopoly reason as its siblings.
if(TARGET GTest::gtest_main)
    add_executable(test_sdi_stage_wiring
        test_sdi_stage_wiring.cpp
    )
    target_link_libraries(test_sdi_stage_wiring PRIVATE
        GTest::gtest_main
        RenderGraph
    )
    set_target_properties(test_sdi_stage_wiring PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_sdi_stage_wiring)
    message(STATUS "✓ test_sdi_stage_wiring configured (Semantic Wiring S2)")
endif()

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
# Connection Rule Test (Sprint 6.0.1) — kept standalone, PDB-consolidation Milestone 1
# ===========================================================================
# This test validates the ConnectionRule system:
# - SlotDescriptor and BindingDescriptor creation
# - ConnectionRuleRegistry rule matching
# - DirectConnectionRule validation logic
# Tests use GoogleTest framework.
#
# NOT merged into test_rendergraph_nodeconn: test_connection_rule defines a
# file-scope `struct MockBindingRef` with a DIFFERENT member layout than
# test_connection_concepts's own `struct MockBindingRef` (2 fields vs. 3,
# including a const char* name) — an ODR violation if both land in one binary.
# NOT merged with test_graph_lifecycle_hooks either (its other natural
# neighbor after test_graph_topology got excluded for a different collision,
# see below): both test_connection_rule.cpp and test_graph_lifecycle_hooks.cpp
# define their own `int main()` (leftover boilerplate identical to what
# GTest::gtest_main already supplies from back when each was a standalone
# target) — a hard LNK2005 "main already defined" error, caught by an actual
# Windows/MSVC link during this milestone. With every other bare-link-surface
# neighbor ruled out by one collision or another, this one stays standalone.

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
# Graph Lifecycle Hooks Tests — kept standalone, PDB-consolidation Milestone 1
# ===========================================================================
# Validates GraphLifecycleHooks node-hook targeting:
# - A hook registered against a specific target node only runs for that node
# - Untargeted (global) hooks still run for every node (legacy behavior)
# - ClearNodeHooks clears both targeted and global storage
#
# Formerly in test_graph_systems.cmake, alongside test_graph_topology. NOT
# merged with test_graph_topology: both define an out-of-line static
# `MockNodeType MockNode::mockType("MockNode");`, a duplicate-symbol link
# error if both TUs land in one binary (test_graph_topology instead moved
# into test_rendergraph_syncbarrier below). NOT merged with test_connection_rule
# either — see that target's own comment just above for why (both define their
# own redundant `int main()`).

if(TARGET GTest::gtest_main)
    add_executable(test_graph_lifecycle_hooks
        test_graph_lifecycle_hooks.cpp
    )

    target_include_directories(test_graph_lifecycle_hooks PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

    target_link_libraries(test_graph_lifecycle_hooks PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_graph_lifecycle_hooks PROPERTIES FOLDER "Tests/RenderGraph Tests")

    if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
        if(TARGET Vulkan::Vulkan)
            target_link_libraries(test_graph_lifecycle_hooks PRIVATE Vulkan::Vulkan)
        endif()
    endif()

    gtest_discover_tests(test_graph_lifecycle_hooks)

    message(STATUS "✓ test_graph_lifecycle_hooks configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
else()
    message(STATUS "⊗ test_graph_lifecycle_hooks skipped (GoogleTest not available)")
endif()

# ===========================================================================
# RenderGraph Dispatch/Scheduling (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges 5 formerly-separate bare-link-surface targets into one executable.
#
# - test_group_dispatch (Sprint 6.1): MultiDispatchNode GROUP_INPUTS and
#   GroupKeyModifier — GroupKeyModifier validation and metadata storage,
#   DispatchPass validation (IsValid()), group partitioning logic, deterministic
#   ordering (std::map), backward compatibility with QueueDispatch().
# - test_task_queue (Sprint 6.2): TaskQueue template — budget enforcement
#   (strict/lenient modes), priority-based scheduling, overflow protection,
#   TaskBudget configuration and presets, backward compatibility with Sprint 6.1.
# - test_multidispatch_integration (Sprint 6.2): MultiDispatchNode + TaskQueue
#   integration tests.
# - test_prediction_error_tracker (Sprint 6.3 Phase 3.1): prediction error tracking
#   system — error ratio computation, rolling statistics (mean, variance, bias),
#   correction factor generation, TimelineCapacityTracker integration. (Other Sprint
#   6.3 tests — GPUQueryManager, TimelineCapacityTracker — are in test_core_systems.cmake.)
# - test_task_profile (Sprint 6.3 Phase 3.2a): TaskProfile unit tests.

if(TARGET GTest::gtest_main)
    add_executable(test_rendergraph_dispatch
        test_group_dispatch.cpp
        test_task_queue.cpp
        test_multidispatch_integration.cpp
        test_prediction_error_tracker.cpp
        test_task_profile.cpp
    )

    target_link_libraries(test_rendergraph_dispatch PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_rendergraph_dispatch PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_rendergraph_dispatch)

    message(STATUS "✓ test_rendergraph_dispatch configured (GroupDispatch/TaskQueue/MultiDispatchIntegration/PredictionErrorTracker/TaskProfile)")
else()
    message(STATUS "⊗ test_rendergraph_dispatch skipped (GoogleTest not available)")
endif()

# ===========================================================================
# RenderGraph Resource/Task Tracking (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges the resource-tracking and wave-scheduling coverage into one executable.
#
# - test_resource_access_tracker (Sprint 6.4 Phase 0): resource access conflict
#   detection — read/write access tracking, conflict detection (write-write,
#   write-read), multi-resource scenarios, GraphTopology integration.
# - test_wave_scheduler (Sprint 6.4 Phase 1): WaveScheduler coverage.
#   (the former local executor coverage was retired with the duplicate executor.)
if(TARGET GTest::gtest_main)
    add_executable(test_rendergraph_tracking
        test_resource_access_tracker.cpp
        test_wave_scheduler.cpp
    )

    target_link_libraries(test_rendergraph_tracking PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_rendergraph_tracking PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_rendergraph_tracking)

    message(STATUS "✓ test_rendergraph_tracking configured (ResourceAccessTracker/WaveScheduler)")
else()
    message(STATUS "⊗ test_rendergraph_tracking skipped (GoogleTest not available)")
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
# Step-6 M-ui: UI multi-document composition host (headless — no Vulkan required)
# ===========================================================================
# Proves the IUiCompositionHost mount/unmount mechanics + the building_inspector.rml fragment +
# BuildingInspectorView binding. Compiles BuildingInspectorView.cpp + HudView.cpp directly (same as
# test_ui_hud_smoke — not the VixenApp library, to stay gaia-free / ODR-safe) and reuses the same
# assets/ui staging (building_inspector.rml sits beside hud.rml in ../assets/ui, copied wholesale).
if(TARGET GTest::gtest_main)
    add_executable(test_ui_composition_host
        test_ui_composition_host.cpp
        ${CMAKE_SOURCE_DIR}/application/main/source/graph/HudView.cpp
        ${CMAKE_SOURCE_DIR}/application/main/source/graph/BuildingInspectorView.cpp
    )
    target_link_libraries(test_ui_composition_host PRIVATE GTest::gtest_main RenderGraph)
    target_include_directories(test_ui_composition_host PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)

    set(_UI_ASSETS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ui")
    add_custom_command(TARGET test_ui_composition_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:test_ui_composition_host>/assets/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_UI_ASSETS_SRC}"
            "$<TARGET_FILE_DIR:test_ui_composition_host>/assets/ui"
        COMMENT "Staging UI assets for test_ui_composition_host"
    )
    if(TARGET TBB::tbb)
        add_custom_command(TARGET test_ui_composition_host POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:test_ui_composition_host>
            COMMENT "Copying TBB DLL for test_ui_composition_host"
        )
    endif()
    set_target_properties(test_ui_composition_host PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_ui_composition_host)
    message(STATUS "✓ test_ui_composition_host configured (M-ui: composition host + inspector fragment)")
else()
    message(STATUS "⊗ test_ui_composition_host skipped (GoogleTest not available)")
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
        AppFlow   # regenerated *.typed.g.h #include "generated/AppFlowCallables.g.hpp"
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
        AppFlow
    )

    # EditorLayers.g.h is a CONSUMER artifact (application/editor) — needs the editor's include
    # dir to resolve "Generated/EditorLayers.g.h" (mirrors test_view_hud_golden's application/main
    # include-dir need). Inc-Ovr: EditorLayers.g.h now #includes "generated/AppFlowCallables.g.hpp"
    # (the isChecked Projection's [KernelCallable] transform, Vixen::AppFlow::Generated::bitAt) --
    # AppFlow's PUBLIC include dir (libraries/AppFlow/include) resolves that path; linking AppFlow
    # (already a header-bearing static lib, no new runtime dependency for this headless gate).
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
# RenderGraph View/Blob Contract (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges 4 formerly-separate bare-link-surface targets into one executable.
# All 4 originals used gtest_discover_tests(... DISCOVERY_MODE PRE_TEST
# DISCOVERY_TIMEOUT 60) — preserved on the merged target's single discover call.
# The other View Contract targets below (wire_roundtrip, wire_soa_roundtrip,
# typed_accessor_emitter, view_container_builder) are NOT included here — they
# each need application/main/include for a Generated/*.g.h header, a real
# build-semantics complication, so they stay standalone.
#
# - test_view_blob (View Contract Inc-2b): ViewBlob/ViewKind/ViewValue
#   reflection-blob contract — pure CPU test of the constexpr descriptor shape +
#   scalar kind/value agreement. No RmlUi/Vulkan/GPU required.
# - test_view_store (View Contract Inc-2b): ViewStore typed storage + by-field
#   setters — pure CPU test of scalar/array storage + blob-validated setter
#   rejection. Links RmlUi (via RenderGraph) for Rml::String/DataModelHandle, no
#   GPU/device.
# - test_view_blob_file (View Contract Inc-2b): ViewBlobFile runtime .viewblob
#   parser — pure CPU test of the line-grammar parser (model/version/field/elem)
#   + hard-fail validation on malformed input. No RmlUi/Vulkan/GPU touchpoints.
# - test_blob_view (View Contract Inc-2b): BlobView generic IView host, dynamic
#   RmlUi model — proves a ViewBlob alone (no per-schema C++) builds a live
#   RmlUi data model (scalars + array-of-struct) and that a version mismatch
#   hard-skips registration.

if(TARGET GTest::gtest_main)
    add_executable(test_rendergraph_viewblob
        test_view_blob.cpp
        test_view_store.cpp
        test_view_blob_file.cpp
        test_blob_view.cpp
    )
    target_link_libraries(test_rendergraph_viewblob PRIVATE GTest::gtest_main RenderGraph)
    set_target_properties(test_rendergraph_viewblob PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_rendergraph_viewblob DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_rendergraph_viewblob configured (ViewBlob/ViewStore/ViewBlobFile/BlobView)")
else()
    message(STATUS "⊗ test_rendergraph_viewblob skipped (GoogleTest not available)")
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
# View Contract Inc-5: ViewWireReaderSoa — SoA wire round-trip proof
# ===========================================================================
# Sibling proof to test_view_wire_roundtrip: the CanonicalSoaWire buffer built in-test is the
# byte-for-byte twin of the C# HudViewWriter.ToBuffer() golden for a Soa-declared Hud model
# (Yeroket ViewWriterEmitterTests.ToBuffer_Produces_Canonical_Soa_Bytes). Proves
# ViewWireReaderSoa::Apply reads back every field via ViewStore -- per-column contiguous scalars
# and per-column offsets+blob strings, including a non-empty AND an empty string row in the same
# column -- hard-fails on a version mismatch (store untouched), and rejects malformed input.

if(TARGET GTest::gtest_main)
    add_executable(test_view_wire_soa_roundtrip test_view_wire_soa_roundtrip.cpp)
    target_link_libraries(test_view_wire_soa_roundtrip PRIVATE GTest::gtest_main RenderGraph)

    # Hud.blob.g.h is a CONSUMER artifact (application/main) -- needs the app's include dir to
    # resolve "Generated/Hud.blob.g.h" (mirrors test_view_wire_roundtrip's identical include dir).
    target_include_directories(test_view_wire_soa_roundtrip PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)

    set_target_properties(test_view_wire_soa_roundtrip PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_view_wire_soa_roundtrip DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_view_wire_soa_roundtrip configured (View Contract Inc-5: SoA wire round-trip)")
else()
    message(STATUS "⊗ test_view_wire_soa_roundtrip skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-5b Milestone A: TypedAccessorEmitter equivalence proof
# ===========================================================================
# Fills a ViewStore via ViewWireReaderSoa::Apply (same canonical wire + kHudBlob fixture as
# test_view_wire_soa_roundtrip.cpp) then wraps the SAME store in the NEW GENERATED
# Vixen::Views::HudSection accessor class (TypedAccessorEmitter.cs, --typed-accessor-cpp) and
# asserts the decoded values match exactly -- proving the generated typed accessors reproduce what
# the raw ViewStore API already proves correct.

if(TARGET GTest::gtest_main)
    add_executable(test_typed_accessor_emitter test_typed_accessor_emitter.cpp)
    target_link_libraries(test_typed_accessor_emitter PRIVATE GTest::gtest_main RenderGraph)

    # HudTypedAccessor.g.h / Hud.blob.g.h are CONSUMER artifacts (application/main) -- needs the
    # app's include dir to resolve "Generated/..." (mirrors test_view_wire_soa_roundtrip's identical
    # include dir).
    target_include_directories(test_typed_accessor_emitter PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)

    set_target_properties(test_typed_accessor_emitter PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_typed_accessor_emitter DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_typed_accessor_emitter configured (View Contract Inc-5b Milestone A: generated accessor equivalence)")
else()
    message(STATUS "⊗ test_typed_accessor_emitter skipped (GoogleTest not available)")
endif()

# ===========================================================================
# View Contract Inc-5b Milestone 2.5: writer-side wiring + UTVC container proof
# ===========================================================================
# C#-side/isolated round-trip proof (per the plan's explicit scope -- does NOT touch undertow's
# real C++ reader/live seam, that is Milestone 3's job). The embedded byte array is the VERBATIM
# output of the real undertow C# writer path (Undertow.View.ViewWriterAdapter +
# Undertow.View.ViewContainerBuilder.Build) run against a REAL captured SimFrame
# (new UndertowSim(); sim.NewCampaign(7); sim.ProjectObserverFrame()) -- manually unwraps the new
# UTVC multi-section container, then decodes each of the 5 sub-buffers via
# ViewWireReaderSoa::Apply + the Milestone-2/2.4-generated typed accessors, asserting decoded
# values match the real SimFrame's real values exactly.

if(TARGET GTest::gtest_main)
    add_executable(test_view_container_builder test_view_container_builder.cpp)
    # AppFlow: the regenerated Undertow*.typed.g.h now #include "generated/AppFlowCallables.g.hpp"
    # ([Projected] typed-accessor transforms) — AppFlow's PUBLIC include dir resolves it (the same
    # wiring test_view_editor_layers_golden documents above).
    target_link_libraries(test_view_container_builder PRIVATE GTest::gtest_main RenderGraph AppFlow)

    # The 5 Undertow*.blob.g.h/.typed.g.h headers are CONSUMER artifacts (application/main) --
    # needs the app's include dir to resolve "Generated/..." (mirrors test_typed_accessor_emitter's
    # identical include dir).
    target_include_directories(test_view_container_builder PRIVATE ${CMAKE_SOURCE_DIR}/application/main/include)

    set_target_properties(test_view_container_builder PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_view_container_builder DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)
    message(STATUS "✓ test_view_container_builder configured (View Contract Inc-5b Milestone 2.5: writer-side UTVC round-trip)")
else()
    message(STATUS "⊗ test_view_container_builder skipped (GoogleTest not available)")
endif()

# ===========================================================================
# RenderGraph Sync/Barrier + GraphTopology (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges 5 formerly-separate bare-link-surface targets, PLUS test_graph_topology
# (formerly in test_graph_systems.cmake — moved here because it could not be
# merged with its original neighbor test_graph_lifecycle_hooks: both define an
# out-of-line static `MockNodeType MockNode::mockType("MockNode");`, a genuine
# duplicate-symbol link error if both land in one binary. Verified no
# MockNode/MockNodeType collision against any file in this group.)
#
# - test_barrier_types (auto-sync P1): AccessKind -> Vulkan sync-semantics mapping.
# - test_frame_sync_scheduler (auto-sync P2): FrameSyncSchedule data types (pure
#   structs, no business logic).
# - test_frame_sync_node_timeline (auto-sync P5a M1): pure CPU test of
#   NextFrameBase arithmetic (no Vulkan device required) — monotonic advance, no
#   ring collision, zero-stride no-op.
# - test_pass_group_schedule (auto-sync P4 M1): BuildPassGroupSchedule node-local
#   barrier baking (pure CPU, no GPU).
# - test_graph_topology: graph topology validation and dependency tracking —
#   circular dependency detection (direct & indirect), topological sorting,
#   dependency chain analysis, complex graph validation, edge case handling.

if(TARGET GTest::gtest_main)
    add_executable(test_rendergraph_syncbarrier
        test_barrier_types.cpp
        test_frame_sync_scheduler.cpp
        test_frame_sync_node_timeline.cpp
        test_pass_group_schedule.cpp
        test_graph_topology.cpp
    )

    # test_graph_topology's own need: allow #include "RenderGraph/..." with a clean path.
    target_include_directories(test_rendergraph_syncbarrier PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
    )

    target_link_libraries(test_rendergraph_syncbarrier PRIVATE GTest::gtest_main RenderGraph)

    if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
        if(TARGET Vulkan::Vulkan)
            target_link_libraries(test_rendergraph_syncbarrier PRIVATE Vulkan::Vulkan)
        endif()
    endif()

    set_target_properties(test_rendergraph_syncbarrier PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_rendergraph_syncbarrier)
    message(STATUS "✓ test_rendergraph_syncbarrier configured (BarrierTypes/FrameSyncScheduler/FrameSyncNodeTimeline/PassGroupSchedule/GraphTopology)")
else()
    message(STATUS "⊗ test_rendergraph_syncbarrier skipped (GoogleTest not available)")
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
# RenderGraph Nodes/ Suite (PDB-consolidation Milestone 1, 2026-07)
# ===========================================================================
# Merges 4 formerly-separate bare-link-surface targets (all registered directly
# from Nodes/*.cpp, not routed through test_critical_nodes.cmake) into one
# executable.
#
# - test_partial_brick_upload (Sparse-Mip ESVO LOD Inc1 M2): real Vulkan device
#   required (software rasterizer or Mesa-Dozen on WSL2 — see IsAcceptableDevice
#   in the test file). Drives BodyOctreeSceneNode's real Setup/Compile/Execute
#   lifecycle against a real BatchedUploader (wired exactly as
#   DeviceNode::CreateDeviceBudgetManager does) to verify brick-pool residency
#   gating.
# - test_bandwidth_ab_measurement (Sparse-Mip ESVO LOD Inc1 M5, Task 12): the
#   increment's gate measurement — N real BodyOctreeSceneNode trees, all-resident
#   (baseline) vs all mip-only (Inc1), real BatchedUploaderStats bytes + wall time.
# - test_buffer_sync_gatherer_node (Sampled Lighting Inc3 M5): config-shape +
#   pre-registration + node-type-registration checks — the generic
#   variadic-VkBuffer-gatherer that replaces ComputeStageNodeConfig's old fixed
#   named buffer-sync slots. Real correctness is validated by the live
#   render-graph gate (see gate-artifacts/inc3-m5-hashes.txt), mirroring every
#   other gatherer node in this codebase.
# - test_probegridconfig_layout (Sampled Lighting Inc4 M2): ProbeGridConfig
#   drift-guard — pure C++ struct-layout test, no shader reflection yet (see the
#   original test_probegridconfig_layout.cpp's own file header for why).

if(TARGET GTest::gtest_main)
    add_executable(test_rendergraph_nodes
        Nodes/test_partial_brick_upload.cpp
        Nodes/test_bandwidth_ab_measurement.cpp
        Nodes/test_buffer_sync_gatherer_node.cpp
        Nodes/test_probegridconfig_layout.cpp
    )
    if(TARGET RenderGraph)
        target_link_libraries(test_rendergraph_nodes PRIVATE GTest::gtest_main RenderGraph)
    else()
        target_link_libraries(test_rendergraph_nodes PRIVATE GTest::gtest_main)
    endif()
    set_target_properties(test_rendergraph_nodes PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_rendergraph_nodes)
    message(STATUS "✓ test_rendergraph_nodes configured (PartialBrickUpload/BandwidthAbMeasurement/BufferSyncGathererNode/ProbeGridConfigLayout)")
else()
    message(STATUS "⊗ test_rendergraph_nodes skipped (GoogleTest not available)")
endif()

# ===========================================================================
# CompileTimeResourceSystem Tests
# ===========================================================================
# Test suite for compile-time type system validation, caching, and traits.
# Moved from root CMakeLists.txt for proper organization.

add_subdirectory(CompileTimeResourceSystem)
