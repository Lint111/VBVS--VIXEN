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
