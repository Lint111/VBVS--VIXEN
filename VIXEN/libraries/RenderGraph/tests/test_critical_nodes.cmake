# ===========================================================================
# Critical Nodes Tests - Priority 3
# ===========================================================================
# These test suites validate critical node classes (infrastructure + sync):
# - DeviceNode: Vulkan device initialization, queue families
# - WindowNode: Window creation, surface management
# - CommandPoolNode: Command pool creation, buffer allocation
# - SwapChainNode: Swapchain creation, image acquisition, present modes
# - FrameSyncNode: Fences and semaphores for frame synchronization
#
# Unit Tests: Configuration validation, slot metadata, type checking
# Integration Tests: Actual Vulkan resource creation (requires full SDK)
#
# Compatible with VULKAN_TRIMMED_BUILD for unit tests.
# Integration tests require full Vulkan SDK.

# DeviceNode Tests
add_executable(test_device_node
    Nodes/test_device_node.cpp
)

# Common libraries for RenderGraph tests. Start with GoogleTest and RenderGraph
set(RENDERGRAPH_TEST_COMMON_LIBS
    GTest::gtest_main
    RenderGraph
)

# Optionally append other known dependencies if their targets exist in the build
if(TARGET VulkanResources)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS VulkanResources)
endif()
if(TARGET GaiaVoxelWorld)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS GaiaVoxelWorld)
endif()
if(TARGET Logger)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS Logger)
endif()
if(TARGET EventBus)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS EventBus)
endif()
if(TARGET ResourceManagement)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS ResourceManagement)
endif()
if(TARGET ShaderManagement)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS ShaderManagement)
endif()

# Link against library target and explicit dependencies
target_link_libraries(test_device_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})


# Visual Studio solution folder organization
set_target_properties(test_device_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_device_node)

message(STATUS "[RenderGraph Tests] Added: test_device_node")

# WindowNode Tests
add_executable(test_window_node
    Nodes/test_window_node.cpp
)

target_link_libraries(test_window_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})


# Visual Studio solution folder organization
set_target_properties(test_window_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_window_node)

message(STATUS "[RenderGraph Tests] Added: test_window_node")

# CommandPoolNode Tests
add_executable(test_command_pool_node
    Nodes/test_command_pool_node.cpp
)

target_link_libraries(test_command_pool_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})


# Visual Studio solution folder organization
set_target_properties(test_command_pool_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_command_pool_node)

message(STATUS "[RenderGraph Tests] Added: test_command_pool_node")

# SwapChainNode Tests
add_executable(test_swap_chain_node
    Nodes/test_swap_chain_node.cpp
)

target_link_libraries(test_swap_chain_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})


# Visual Studio solution folder organization
set_target_properties(test_swap_chain_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_swap_chain_node)

message(STATUS "[RenderGraph Tests] Added: test_swap_chain_node")

# FrameSyncNode Tests
add_executable(test_frame_sync_node
    Nodes/test_frame_sync_node.cpp
)

target_link_libraries(test_frame_sync_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})


# Visual Studio solution folder organization
set_target_properties(test_frame_sync_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_frame_sync_node)

message(STATUS "[RenderGraph Tests] Added: test_frame_sync_node")

# PushConstantGathererNode Tests
add_executable(test_push_constant_gatherer_node
    Nodes/test_push_constant_gatherer_node.cpp
)

target_link_libraries(test_push_constant_gatherer_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS} ShaderManagementTestFixtures)


# Visual Studio solution folder organization
set_target_properties(test_push_constant_gatherer_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_push_constant_gatherer_node)

message(STATUS "[RenderGraph Tests] Added: test_push_constant_gatherer_node")

# DescriptorResourceGathererNode Tests
add_executable(test_descriptor_resource_gatherer_node
    Nodes/test_descriptor_resource_gatherer_node.cpp
)

target_link_libraries(test_descriptor_resource_gatherer_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS} ShaderManagementTestFixtures)


# Visual Studio solution folder organization
set_target_properties(test_descriptor_resource_gatherer_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_descriptor_resource_gatherer_node)

message(STATUS "[RenderGraph Tests] Added: test_descriptor_resource_gatherer_node")

# AccelerationStructureNode Tests (Phase 3.4)
add_executable(test_acceleration_structure_node
    Nodes/test_acceleration_structure_node.cpp
)

# Link against library targets including CashSystem for ASBuildMode
target_link_libraries(test_acceleration_structure_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET CashSystem)
    target_link_libraries(test_acceleration_structure_node PRIVATE CashSystem)
endif()


# Visual Studio solution folder organization
set_target_properties(test_acceleration_structure_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_acceleration_structure_node)

message(STATUS "[RenderGraph Tests] Added: test_acceleration_structure_node (Phase 3.4)")

# RenderTargetNode Tests (AR#28)
add_executable(test_render_target_node
    Nodes/test_render_target_node.cpp
)

target_link_libraries(test_render_target_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_render_target_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_render_target_node)

message(STATUS "[RenderGraph Tests] Added: test_render_target_node (AR#28)")

# ComputeDispatchNode KI-007 fix Tests — pure unit tests, no device (see the test file header)
add_executable(test_compute_dispatch_node
    Nodes/test_compute_dispatch_node.cpp
)

target_link_libraries(test_compute_dispatch_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

set_target_properties(test_compute_dispatch_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_compute_dispatch_node)

message(STATUS "[RenderGraph Tests] Added: test_compute_dispatch_node (KI-007 render-target layout tracking)")

# Blend mode recipe + config Tests (AR#32) — pure unit tests, no device
add_executable(test_blend_mode
    Nodes/test_blend_mode.cpp
)

target_link_libraries(test_blend_mode PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_blend_mode PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_blend_mode)

message(STATUS "[RenderGraph Tests] Added: test_blend_mode (AR#32)")

# Selection core types (SEL-P1) — pure unit tests, no device
# Covers SelectionSet.apply modifier semantics + SelectionId hash/equality.
add_executable(test_selection_set
    Selection/test_selection_set.cpp
)

target_link_libraries(test_selection_set PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_selection_set PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_selection_set)

message(STATUS "[RenderGraph Tests] Added: test_selection_set (SEL-P1)")

# Selection candidate resolution (SEL-P2, providers-are-nodes) — pure unit tests, no device.
# Pins pickBestCandidate() (max priority, tie-break min depth, ignore non-hits) — the rule the
# SelectionCoordinatorNode applies to the MultiConnect candidate fan-in. The voxel provider node's
# GPU readback is exercised live in the app (the user's manual click test).
add_executable(test_selection_resolve
    Selection/test_selection_resolve.cpp
)

target_link_libraries(test_selection_resolve PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_selection_resolve PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_selection_resolve)

message(STATUS "[RenderGraph Tests] Added: test_selection_resolve (SEL-P2)")

# PickRay unproject Tests (AR#35) — pure unit tests, no device
add_executable(test_pick_ray
    Nodes/test_pick_ray.cpp
)

target_link_libraries(test_pick_ray PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_pick_ray PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_pick_ray)

message(STATUS "[RenderGraph Tests] Added: test_pick_ray (AR#35)")

# InstanceBufferNode Tests (AR#31)
add_executable(test_instance_buffer_node
    Nodes/test_instance_buffer_node.cpp
)

target_link_libraries(test_instance_buffer_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_instance_buffer_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_instance_buffer_node)

message(STATUS "[RenderGraph Tests] Added: test_instance_buffer_node (AR#31)")

# PickIdTargetNode Tests (AR#35 GPU picking P1)
add_executable(test_pick_id_target_node
    Nodes/test_pick_id_target_node.cpp
)

target_link_libraries(test_pick_id_target_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_pick_id_target_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_pick_id_target_node)

message(STATUS "[RenderGraph Tests] Added: test_pick_id_target_node (AR#35)")

# DynamicInstanceBufferNode Tests (AR#33)
add_executable(test_dynamic_instance_buffer_node
    Nodes/test_dynamic_instance_buffer_node.cpp
)

target_link_libraries(test_dynamic_instance_buffer_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_dynamic_instance_buffer_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_dynamic_instance_buffer_node)

message(STATUS "[RenderGraph Tests] Added: test_dynamic_instance_buffer_node (AR#33)")

# MvpUniformNode Tests (AR#31)
add_executable(test_mvp_uniform_node
    Nodes/test_mvp_uniform_node.cpp
)

target_link_libraries(test_mvp_uniform_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})

# Visual Studio solution folder organization
set_target_properties(test_mvp_uniform_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_mvp_uniform_node)

message(STATUS "[RenderGraph Tests] Added: test_mvp_uniform_node (AR#31)")

# BodyOctreeSceneNode GPU-resource LIFETIME test.
# Drives the REAL node lifecycle (Compile -> Execute ring cycles -> grow -> Cleanup
# recompile/teardown -> device destroy) on a real device under the Khronos validation
# layer, asserting NO lifetime errors (destroy-while-bound, double-free, leaked objects).
# Links the same set as the FR-7 ring sibling test_dynamic_instance_buffer_node; SVO
# (ShellOctree/PackInstances) comes in via the RenderGraph link closure. Uses
# VixenSelectWslGpuIcd() (see the file header) to prefer Mesa-Dozen (the real GPU) on
# WSL2, falling back to lavapipe otherwise — no VK_ICD_FILENAMES env needed by default.
add_executable(test_body_octree_lifetime
    Nodes/test_body_octree_lifetime.cpp
)

target_link_libraries(test_body_octree_lifetime PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_body_octree_lifetime PRIVATE SVO)
endif()
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_body_octree_lifetime PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

# Visual Studio solution folder organization
set_target_properties(test_body_octree_lifetime PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_body_octree_lifetime)

message(STATUS "[RenderGraph Tests] Added: test_body_octree_lifetime (real-GPU lifetime)")

# ===========================================================================
# BodyInstanceRayMarch.comp REAL-SHADER render-to-PNG test.
# ===========================================================================
# Compiles the SHIPPED ray-march compute shader to SPIR-V at build time with the
# bundled glslc, then renders the SP2 body scene through it (Dozen preferred, lavapipe
# fallback — see test_body_octree_lifetime.cpp's file header) and dumps
# /tmp/glsl_shader_near.png. Same device-selection + validation-layer contract as
# test_body_octree_lifetime (an unrecognized device is hard-asserted against before
# any vkQueueSubmit). The PNG is directly comparable to the CPU castRay reference
# (cpu_body_render NEAR view) to settle the brick-crack Q.
# NOTE: do NOT gate on if(TARGET SVO) — at the point tests/CMakeLists.txt includes
# this file the SVO target is not yet visible in this directory scope (subdirectory
# ordering). SVO symbols (ShellOctree/BodyInstanceGpu, header-only + lib) reach this
# target transitively via RenderGraph in RENDERGRAPH_TEST_COMMON_LIBS, exactly like
# test_body_octree_lifetime above (whose own `if(TARGET SVO)` link is likewise a no-op).

# --- Compile BodyInstanceRayMarch.comp -> SPIR-V with an environment-appropriate glslc ---
# VIXEN_SHADER_SOURCE_DIR is <VIXEN>/shaders; the auto-provisioned SDK sits beside it.
# These are GPU-render tests that need a glslc RUNNABLE ON THE CURRENT PLATFORM. The
# auto-provisioned LunarG SDK is a LINUX build — its bin/glslc is an ELF binary with NO
# extension. On a Windows/MSVC configure the WSL-provisioned tree is still visible (shared
# /mnt/c checkout), and cmd.exe cannot execute a Linux ELF ("... is not recognized ...").
#
# Two traps this resolution avoids:
#   1. A bare `if(EXISTS bin/glslc)` gate: the Linux ELF file EXISTS on Windows, so the gate
#      wrongly passes and the whole ninja build fails at the exec step.
#   2. `find_program(NAMES glslc)` on Windows still MATCHES the extensionless Linux ELF (Windows
#      find_program tries the exact name too, not only .exe), so it picks the wrong-OS binary.
# Fix: make the search OS-aware. On Windows require glslc.exe and look in a real system SDK
# (VULKAN_SDK / PATH) FIRST — the WSL Linux tree can never supply a .exe. On Linux use the
# extensionless glslc from the provisioned tree. Then gate on whether a platform-runnable glslc
# was actually found, so the tests build+run wherever one exists and cleanly skip where none does.
set(_brm_shader_dir "${VIXEN_SHADER_SOURCE_DIR}")
if(WIN32)
    # Windows: only a .exe is runnable. Prefer a system SDK; never the WSL-provisioned ELF.
    set(_glslc_names glslc.exe)
    set(_glslc_hints "")
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _glslc_hints "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    endif()
else()
    # Linux/WSL: the auto-provisioned SDK's extensionless glslc (versioned via ProvisionVulkan.cmake,
    # with a legacy relative fallback when the provisioning vars are unset), then a system SDK.
    set(_glslc_names glslc)
    set(_glslc_hints "")
    if(DEFINED VIXEN_VULKAN_CACHE_DIR AND DEFINED VIXEN_VULKAN_SDK_VERSION)
        list(APPEND _glslc_hints "${VIXEN_VULKAN_CACHE_DIR}/${VIXEN_VULKAN_SDK_VERSION}/x86_64/bin")
    endif()
    list(APPEND _glslc_hints "${_brm_shader_dir}/../.vulkan-sdk/1.4.350.1/x86_64/bin")
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _glslc_hints "$ENV{VULKAN_SDK}/bin")
    endif()
endif()
find_program(VIXEN_GLSLC NAMES ${_glslc_names} HINTS ${_glslc_hints})
if(VIXEN_GLSLC)
set(_brm_src "${_brm_shader_dir}/BodyInstanceRayMarch.comp")
set(_brm_spv "${CMAKE_CURRENT_BINARY_DIR}/BodyInstanceRayMarch.spv")

# DEPEND on the .comp AND every .glsl it may #include — otherwise editing an include
# (e.g. StoredSdf.glsl / ESVOTraversal.glsl) leaves the .spv stale ("ninja: no work to do").
file(GLOB _brm_includes CONFIGURE_DEPENDS "${_brm_shader_dir}/*.glsl")

add_custom_command(
    OUTPUT  ${_brm_spv}
    COMMAND ${VIXEN_GLSLC}
            -fshader-stage=compute
            -I ${_brm_shader_dir}
            --target-env=vulkan1.3
            ${_brm_src}
            -o ${_brm_spv}
    DEPENDS ${_brm_src} ${_brm_includes}
    COMMENT "Compiling BodyInstanceRayMarch.comp -> SPIR-V (bundled glslc)"
    VERBATIM)
add_custom_target(body_instance_raymarch_spv DEPENDS ${_brm_spv})

add_executable(test_body_instance_raymarch_render
    Nodes/test_body_instance_raymarch_render.cpp
)
add_dependencies(test_body_instance_raymarch_render body_instance_raymarch_spv)
target_link_libraries(test_body_instance_raymarch_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_body_instance_raymarch_render PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_body_instance_raymarch_render PRIVATE stb)
else()
    # stb is an INTERFACE header dep; if the target isn't visible here, add its include dir.
    target_include_directories(test_body_instance_raymarch_render PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
target_compile_definitions(test_body_instance_raymarch_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_body_instance_raymarch_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_body_instance_raymarch_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
# DISCOVERY_MODE PRE_TEST: defer the --gtest_list_tests invocation to ctest run-time,
# not POST_BUILD. This prevents the Vulkan-init timeout from making the build "FAILED"
# (the known MSB3073 / 5s discovery timeout flake — see friction log 2026-06-13).
gtest_discover_tests(test_body_instance_raymarch_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_body_instance_raymarch_render (real-shader render)")

# ===========================================================================
# I4.1 — SetRecipePool pool render gate: 4 baked SDF recipes,
# one instance per octreeIndex, asserts all 4 bodies produce visible pixels.
# ===========================================================================
add_executable(test_recipe_pool_render
    Nodes/test_recipe_pool_render.cpp
)
add_dependencies(test_recipe_pool_render body_instance_raymarch_spv)
target_link_libraries(test_recipe_pool_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_recipe_pool_render PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_recipe_pool_render PRIVATE stb)
else()
    target_include_directories(test_recipe_pool_render PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
target_compile_definitions(test_recipe_pool_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_recipe_pool_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_recipe_pool_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_recipe_pool_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_recipe_pool_render (I4.1 pool render gate)")

# ===========================================================================
# Sparse-Mip ESVO LOD Inc1 M3 — shader-side mip fallback read (Tasks 7-9):
# a mip-only tree (residency never requested) renders a recognizable round
# silhouette from mip samples alone; a fully-resident tree renders comparably.
# ===========================================================================
add_executable(test_mip_fallback_render
    Nodes/test_mip_fallback_render.cpp
)
add_dependencies(test_mip_fallback_render body_instance_raymarch_spv)
target_link_libraries(test_mip_fallback_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_mip_fallback_render PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_mip_fallback_render PRIVATE stb)
else()
    target_include_directories(test_mip_fallback_render PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
target_compile_definitions(test_mip_fallback_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_mip_fallback_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_mip_fallback_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_mip_fallback_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_mip_fallback_render (Sparse-Mip ESVO LOD Inc1 M3)")

# ===========================================================================
# Sparse-Mip ESVO LOD Inc1 M4b — GPU per-ray occlusion reject: a synthetic
# camera -> occluder -> occluded-target line-up (front-to-back sorted CPU-side)
# asserts the occluded instance's ESVO traversal ran ZERO iterations once the
# occluder's hit landed in bestT (binding 14, InstanceIterDebugBuffer), not just
# "no visible pixel difference." No PNG output (no stb dependency needed).
# ===========================================================================
add_executable(test_body_instance_occlusion_reject
    Nodes/test_body_instance_occlusion_reject.cpp
)
add_dependencies(test_body_instance_occlusion_reject body_instance_raymarch_spv)
target_link_libraries(test_body_instance_occlusion_reject PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_body_instance_occlusion_reject PRIVATE SVO)
endif()
target_compile_definitions(test_body_instance_occlusion_reject PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_body_instance_occlusion_reject PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_body_instance_occlusion_reject PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_body_instance_occlusion_reject
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_body_instance_occlusion_reject (Sparse-Mip ESVO LOD Inc1 M4b)")

# ===========================================================================
# Tiered-ESVO Inc2 M4 (Tasks 9-10) — live-GPU proof of the screen-space LOD
# early-out and residency-reuse fallback for a farBit==1 tier-crossing leaf.
# Extends test_body_instance_occlusion_reject.cpp's real-device/real-shader
# dispatch pattern with binding 15 (TierRefTableBuffer) wired in, driving a
# genuine two-tree tier-crossing scene (same construction as
# BuildRenderGraph.cpp's VIXEN_TIER_CROSSING_DEMO).
# ===========================================================================
add_executable(test_tier_crossing_lod_residency
    Nodes/test_tier_crossing_lod_residency.cpp
)
add_dependencies(test_tier_crossing_lod_residency body_instance_raymarch_spv)
target_link_libraries(test_tier_crossing_lod_residency PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_tier_crossing_lod_residency PRIVATE SVO)
endif()
if(TARGET GaiaVoxelWorld)
    target_link_libraries(test_tier_crossing_lod_residency PRIVATE GaiaVoxelWorld)
endif()
target_compile_definitions(test_tier_crossing_lod_residency PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_tier_crossing_lod_residency PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_tier_crossing_lod_residency PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_tier_crossing_lod_residency
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_tier_crossing_lod_residency (Tiered-ESVO Inc2 M4 Tasks 9-10)")

# ===========================================================================
# Voxel Authoring Inc1 M4 — vixen_editor's load/flatten/bake/render/toggle path:
# golden document renders, then a cut-layer ablation asserts a real
# pixel-level top-face difference (the cylinder punches through the box).
# ===========================================================================
add_executable(test_editor_document_render
    Nodes/test_editor_document_render.cpp
)
add_dependencies(test_editor_document_render body_instance_raymarch_spv)
target_link_libraries(test_editor_document_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_editor_document_render PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_editor_document_render PRIVATE stb)
else()
    target_include_directories(test_editor_document_render PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
target_compile_definitions(test_editor_document_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}"
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_editor_document_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_editor_document_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_editor_document_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_editor_document_render (Voxel Authoring Inc1 M4 editor live gate)")

# ===========================================================================
# AppFlow Inc-2 M4 — headless GPU render-gate: a ToggleLayer dispatched through
# AppFlowRuntime re-flattens + re-bakes + re-renders the golden document, and Undo()
# restores the render byte-for-byte. Links AppFlow (offline-only lib) alongside
# RenderGraph + SVO — the GPU dependency lives in this test, not in the AppFlow lib.
# ===========================================================================
add_executable(test_appflow_editor_toggle_render
    Nodes/test_appflow_editor_toggle_render.cpp
)
add_dependencies(test_appflow_editor_toggle_render body_instance_raymarch_spv)
target_link_libraries(test_appflow_editor_toggle_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_appflow_editor_toggle_render PRIVATE SVO)
endif()
if(TARGET AppFlow)
    target_link_libraries(test_appflow_editor_toggle_render PRIVATE AppFlow)
endif()
if(TARGET stb)
    target_link_libraries(test_appflow_editor_toggle_render PRIVATE stb)
else()
    target_include_directories(test_appflow_editor_toggle_render PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
target_compile_definitions(test_appflow_editor_toggle_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}"
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_appflow_editor_toggle_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_appflow_editor_toggle_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_appflow_editor_toggle_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_appflow_editor_toggle_render (AppFlow Inc-2 M4 GPU render-gate)")

# ===========================================================================
# I4.2 — Recipe authoring live gate:
#   a) Subtract(Box, Sphere) CSG recipe renders a non-trivial solid.
#   b) Default 3-shell scene regression confirms the M2 SSBO fix holds.
# ===========================================================================
add_executable(test_recipe_authoring_gate
    Nodes/test_recipe_authoring_gate.cpp
)
add_dependencies(test_recipe_authoring_gate body_instance_raymarch_spv)
target_link_libraries(test_recipe_authoring_gate PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_recipe_authoring_gate PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_recipe_authoring_gate PRIVATE stb)
else()
    target_include_directories(test_recipe_authoring_gate PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
target_compile_definitions(test_recipe_authoring_gate PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_recipe_authoring_gate PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_recipe_authoring_gate PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_recipe_authoring_gate
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_recipe_authoring_gate (I4.2 CSG + regression)")

# ===========================================================================
# OctreeConfig <-> shader SDI layout drift-guard (CPU reflection, no render).
# Reflects the built BodyInstanceRayMarch SPIR-V via ShaderManagement's SpirvReflector
# and asserts the C++ Vixen::SVO::OctreeConfig layout matches it (per-field offsets +
# configs[] array stride == sizeof). Replaces the hand-eyeballed SPIR-V layout check.
# Pure CPU (reflection only) — no lavapipe / no GPU. Reuses the same compiled .spv.
# ===========================================================================
add_executable(test_octree_config_sdi_parity
    Nodes/test_octree_config_sdi_parity.cpp
)
add_dependencies(test_octree_config_sdi_parity body_instance_raymarch_spv)
target_link_libraries(test_octree_config_sdi_parity PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_octree_config_sdi_parity PRIVATE SVO)
endif()
target_compile_definitions(test_octree_config_sdi_parity PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
set_target_properties(test_octree_config_sdi_parity PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_octree_config_sdi_parity
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_octree_config_sdi_parity (SDI layout drift-guard)")

# ===========================================================================
# LightingConfig SDI Parity Test (Sampled Lighting Inc0 M3)
# ===========================================================================
# Reflects the built BodyInstanceRayMarch SPIR-V and asserts the generated
# C++ Vixen::Gpu::LightingConfig / Light layout matches it (per-field offsets
# + lights[] array stride == sizeof(Light)). Sibling of
# test_octree_config_sdi_parity above, promised by Inc0 M1's
# test_lightingconfig_parity.cpp once a shader consumer existed. Pure CPU
# (reflection only) — no lavapipe / no GPU. Reuses the same compiled .spv.
# ===========================================================================
add_executable(test_lightingconfig_sdi_parity
    Nodes/test_lightingconfig_sdi_parity.cpp
)
add_dependencies(test_lightingconfig_sdi_parity body_instance_raymarch_spv)
target_link_libraries(test_lightingconfig_sdi_parity PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
target_compile_definitions(test_lightingconfig_sdi_parity PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
set_target_properties(test_lightingconfig_sdi_parity PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_lightingconfig_sdi_parity
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_lightingconfig_sdi_parity (SDI layout drift-guard)")

else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_body_instance_raymarch_render — no glslc runnable on this platform found (searched ${_glslc_hints} + PATH)")
endif()

# ===========================================================================
# AppFlow Inc-2b M3 — windowed editor toggle/undo/redo capture assertion.
# Reads the 4 PNGs VIXEN/temp/run_editor_script.bat's unattended vixen_editor.exe run dumps and
# asserts the toggle/undo/redo relations (see test_editor_toggle_undo_capture.cpp's file header).
# Pure file I/O (stb_image) -- no Vulkan/GPU, so deliberately registered OUTSIDE the glslc-gated
# `if(VIXEN_GLSLC)` block above so it builds+runs on the Windows/MSVC side too, matching
# where the windowed editor itself builds and runs (this whole increment is Windows-side-first).
# ===========================================================================
add_executable(test_editor_toggle_undo_capture
    Nodes/test_editor_toggle_undo_capture.cpp
)
target_link_libraries(test_editor_toggle_undo_capture PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET stb)
    target_link_libraries(test_editor_toggle_undo_capture PRIVATE stb)
else()
    target_include_directories(test_editor_toggle_undo_capture PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
set_target_properties(test_editor_toggle_undo_capture PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_editor_toggle_undo_capture)
message(STATUS "[RenderGraph Tests] Added: test_editor_toggle_undo_capture (AppFlow Inc-2b M3 windowed gate assertion)")

# ===========================================================================
# View Contract Inc-2 Task 5 — windowed main-app HUD capture assertion.
# Reads the 3 PNGs application/main/main_hud_capture.bat's unattended VIXEN.exe run dumps and
# asserts the generic IView-host + native HudView path renders the HUD (see
# test_hud_render_capture.cpp's file header). Pure file I/O (stb_image) -- no Vulkan/GPU, so
# registered OUTSIDE the glslc-gated block above, mirroring test_editor_toggle_undo_capture.
# ===========================================================================
add_executable(test_hud_render_capture
    Nodes/test_hud_render_capture.cpp
)
target_link_libraries(test_hud_render_capture PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET stb)
    target_link_libraries(test_hud_render_capture PRIVATE stb)
else()
    target_include_directories(test_hud_render_capture PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
set_target_properties(test_hud_render_capture PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_hud_render_capture)
message(STATUS "[RenderGraph Tests] Added: test_hud_render_capture (View Contract Inc-2 Task 5 windowed gate assertion)")

# ===========================================================================
# P2.2 M2 — Procedural recipe live compute render (compile realization)
# ===========================================================================
# Emits an all-HLSL compute shader from SdfInstruction[], compiles it via
# ShaderCompiler (HLSL→SPIR-V at test run time), dispatches with a
# minimal 1-binding (storage-image) + push-constant compute harness.
# No pre-compiled .spv needed (ShaderCompiler handles it at runtime).
# Same device-selection contract as test_body_instance_raymarch_render.
if(TARGET ShaderManagement)
# NOTE: SVO target is not visible at this include scope (subdirectory ordering),
# same as test_body_instance_raymarch_render. SVO headers + ShaderCompiler reach
# the test transitively via RENDERGRAPH_TEST_COMMON_LIBS (which includes ShaderManagement).

add_executable(test_procedural_recipe_render
    Nodes/test_procedural_recipe_render.cpp
)

target_link_libraries(test_procedural_recipe_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
# Optional explicit SVO link (no-op at this scope, transitive via RENDERGRAPH_TEST_COMMON_LIBS).
if(TARGET SVO)
    target_link_libraries(test_procedural_recipe_render PRIVATE SVO)
endif()

if(TARGET stb)
    target_link_libraries(test_procedural_recipe_render PRIVATE stb)
else()
    target_include_directories(test_procedural_recipe_render PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()

# SDF_CORE_KERNELS_HLSL_PATH: same path as used by test_recipe_codegen.
target_compile_definitions(test_procedural_recipe_render PRIVATE
    SDF_CORE_KERNELS_HLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl"
)
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_procedural_recipe_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

if(TARGET TBB::tbb)
    add_custom_command(TARGET test_procedural_recipe_render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_procedural_recipe_render>
        COMMENT "Copying TBB DLL for test_procedural_recipe_render")
endif()

set_target_properties(test_procedural_recipe_render PROPERTIES FOLDER "Tests/RenderGraph Tests")

# PRE_TEST discovery to avoid Vulkan-init timeout during build (same as raymarch render test).
gtest_discover_tests(test_procedural_recipe_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_procedural_recipe_render (P2.2 M2 live procedural compute)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_procedural_recipe_render — ShaderManagement not available")
endif()

# ===========================================================================
# Surface-Shell ESVO cache — ShellRevalidateNode GPU dispatch live-gate.
# ===========================================================================
# Compiles the SHIPPED shaders/ShellDerive.comp to SPIR-V at build time with the
# environment-appropriate glslc (same gate as body_instance_raymarch_spv above — skipped
# when no platform-runnable glslc is found), then: (a) asserts the GPU dispatch's shellFlags[]
# classification matches the CPU Vixen::SVO::DeriveShell oracle bit-for-bit, and (b) assembles a
# real ComputePassStep pair with disjoint Resource* accesses and asserts BuildPassGroupSchedule
# bakes ZERO entry barriers between them (double-buffer parallelism proof).
if(VIXEN_GLSLC)
set(_shellderive_src "${_brm_shader_dir}/ShellDerive.comp")
set(_shellderive_spv "${CMAKE_CURRENT_BINARY_DIR}/ShellDerive.spv")
file(GLOB _shellderive_includes CONFIGURE_DEPENDS "${_brm_shader_dir}/*.glsl" "${_brm_shader_dir}/Generated/*.glsl")

add_custom_command(
    OUTPUT  ${_shellderive_spv}
    COMMAND ${VIXEN_GLSLC}
            -fshader-stage=compute
            -I ${_brm_shader_dir}
            --target-env=vulkan1.3
            ${_shellderive_src}
            -o ${_shellderive_spv}
    DEPENDS ${_shellderive_src} ${_shellderive_includes}
    COMMENT "Compiling ShellDerive.comp -> SPIR-V (bundled glslc)"
    VERBATIM)
add_custom_target(shell_derive_spv DEPENDS ${_shellderive_spv})

add_executable(test_shell_revalidate_node
    Nodes/test_shell_revalidate_node.cpp
)
add_dependencies(test_shell_revalidate_node shell_derive_spv)
target_link_libraries(test_shell_revalidate_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_shell_revalidate_node PRIVATE SVO)
endif()
target_compile_definitions(test_shell_revalidate_node PRIVATE
    SHELLDERIVE_SPV="${_shellderive_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_shell_revalidate_node PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_shell_revalidate_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_shell_revalidate_node
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_shell_revalidate_node (Surface-Shell GPU dispatch live-gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_shell_revalidate_node — no glslc runnable on this platform found")
endif()
