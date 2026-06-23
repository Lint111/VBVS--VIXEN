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

# BodyOctreeSceneNode software-Vulkan (lavapipe) LIFETIME test.
# Drives the REAL node lifecycle (Compile -> Execute ring cycles -> grow -> Cleanup
# recompile/teardown -> device destroy) on the lavapipe CPU rasterizer under the
# Khronos validation layer, asserting NO lifetime errors (destroy-while-bound,
# double-free, leaked objects). Links the same set as the FR-7 ring sibling
# test_dynamic_instance_buffer_node; SVO (ShellOctree/PackInstances) comes in via
# the RenderGraph link closure. See the file header for the SAFETY contract and the
# required VK_ICD_FILENAMES (lavapipe) + VK_LAYER_PATH (validation) run-time env.
add_executable(test_body_octree_lifetime
    Nodes/test_body_octree_lifetime.cpp
)

target_link_libraries(test_body_octree_lifetime PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_body_octree_lifetime PRIVATE SVO)
endif()

# Visual Studio solution folder organization
set_target_properties(test_body_octree_lifetime PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_body_octree_lifetime)

message(STATUS "[RenderGraph Tests] Added: test_body_octree_lifetime (lavapipe lifetime)")

# ===========================================================================
# BodyInstanceRayMarch.comp REAL-SHADER render-to-PNG test (lavapipe).
# ===========================================================================
# Compiles the SHIPPED ray-march compute shader to SPIR-V at build time with the
# bundled glslc, then renders the SP2 body scene through it on the lavapipe CPU
# rasterizer and dumps /tmp/glsl_shader_near.png. Same SAFETY contract + env as
# test_body_octree_lifetime (lavapipe ICD + validation layer; software device is
# hard-asserted before any vkQueueSubmit). The PNG is directly comparable to the
# CPU castRay reference (cpu_body_render NEAR view) to settle the brick-crack Q.
# NOTE: do NOT gate on if(TARGET SVO) — at the point tests/CMakeLists.txt includes
# this file the SVO target is not yet visible in this directory scope (subdirectory
# ordering). SVO symbols (ShellOctree/BodyInstanceGpu, header-only + lib) reach this
# target transitively via RenderGraph in RENDERGRAPH_TEST_COMMON_LIBS, exactly like
# test_body_octree_lifetime above (whose own `if(TARGET SVO)` link is likewise a no-op).

# --- Compile BodyInstanceRayMarch.comp -> SPIR-V with the bundled glslc ---
# VIXEN_SHADER_SOURCE_DIR is <VIXEN>/shaders; the bundled SDK sits beside it.
# This is a lavapipe/WSL-only test: it needs the auto-provisioned Linux LunarG SDK's glslc
# (and lavapipe at run time). On environments where Vulkan came from the system (so the SDK
# cache was never provisioned — e.g. the Windows/MSVC build), the bundled glslc is absent;
# gate the whole rule on its existence so the rest of the suite still builds. Without the gate,
# the missing glslc fails the entire ninja build ("system cannot find the path specified").
set(_brm_shader_dir "${VIXEN_SHADER_SOURCE_DIR}")
# Locate glslc from the auto-provisioned Vulkan SDK (ProvisionVulkan.cmake) rather than a
# hardcoded version path, so a clean WSL configure (which downloads the SDK) finds it and a
# version bump (VIXEN_VULKAN_SDK_VERSION) doesn't silently skip this test. Falls back to the
# legacy relative path if the provisioning vars are unset (e.g. a system-SDK build).
if(DEFINED VIXEN_VULKAN_CACHE_DIR AND DEFINED VIXEN_VULKAN_SDK_VERSION)
    set(_brm_glslc "${VIXEN_VULKAN_CACHE_DIR}/${VIXEN_VULKAN_SDK_VERSION}/x86_64/bin/glslc")
else()
    set(_brm_glslc "${_brm_shader_dir}/../.vulkan-sdk/1.4.350.1/x86_64/bin/glslc")
endif()
if(EXISTS "${_brm_glslc}")
set(_brm_src "${_brm_shader_dir}/BodyInstanceRayMarch.comp")
set(_brm_spv "${CMAKE_CURRENT_BINARY_DIR}/BodyInstanceRayMarch.spv")

# DEPEND on the .comp AND every .glsl it may #include — otherwise editing an include
# (e.g. StoredSdf.glsl / ESVOTraversal.glsl) leaves the .spv stale ("ninja: no work to do").
file(GLOB _brm_includes CONFIGURE_DEPENDS "${_brm_shader_dir}/*.glsl")

add_custom_command(
    OUTPUT  ${_brm_spv}
    COMMAND ${_brm_glslc}
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

set_target_properties(test_body_instance_raymarch_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
# DISCOVERY_MODE PRE_TEST: defer the --gtest_list_tests invocation to ctest run-time,
# not POST_BUILD. This prevents the Vulkan-init timeout from making the build "FAILED"
# (the known MSB3073 / 5s discovery timeout flake — see friction log 2026-06-13).
gtest_discover_tests(test_body_instance_raymarch_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_body_instance_raymarch_render (lavapipe real-shader render)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_body_instance_raymarch_render — bundled glslc not provisioned at ${_brm_glslc} (lavapipe/WSL-only test)")
endif()
