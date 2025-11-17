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

# Link against library target - includes propagate automatically
target_link_libraries(test_device_node PRIVATE
    GTest::gtest_main
    RenderGraph
)


# Visual Studio solution folder organization
set_target_properties(test_device_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_device_node)

message(STATUS "[RenderGraph Tests] Added: test_device_node")

# WindowNode Tests
add_executable(test_window_node
    Nodes/test_window_node.cpp
)

# Link against library target - includes propagate automatically
target_link_libraries(test_window_node PRIVATE
    GTest::gtest_main
    RenderGraph
)


# Visual Studio solution folder organization
set_target_properties(test_window_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_window_node)

message(STATUS "[RenderGraph Tests] Added: test_window_node")

# CommandPoolNode Tests
add_executable(test_command_pool_node
    Nodes/test_command_pool_node.cpp
)

# Link against library target - includes propagate automatically
target_link_libraries(test_command_pool_node PRIVATE
    GTest::gtest_main
    RenderGraph
)


# Visual Studio solution folder organization
set_target_properties(test_command_pool_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_command_pool_node)

message(STATUS "[RenderGraph Tests] Added: test_command_pool_node")

# SwapChainNode Tests
add_executable(test_swap_chain_node
    Nodes/test_swap_chain_node.cpp
)

# Link against library target - includes propagate automatically
target_link_libraries(test_swap_chain_node PRIVATE
    GTest::gtest_main
    RenderGraph
)


# Visual Studio solution folder organization
set_target_properties(test_swap_chain_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_swap_chain_node)

message(STATUS "[RenderGraph Tests] Added: test_swap_chain_node")

# FrameSyncNode Tests
add_executable(test_frame_sync_node
    Nodes/test_frame_sync_node.cpp
)

# Link against library target - includes propagate automatically
target_link_libraries(test_frame_sync_node PRIVATE
    GTest::gtest_main
    RenderGraph
)


# Visual Studio solution folder organization
set_target_properties(test_frame_sync_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_frame_sync_node)

message(STATUS "[RenderGraph Tests] Added: test_frame_sync_node")

# PushConstantGathererNode Tests
add_executable(test_push_constant_gatherer_node
    Nodes/test_push_constant_gatherer_node.cpp
)

# Link against library targets - includes and test fixtures propagate automatically
# ShaderManagementTestFixtures exposes test fixture headers for cross-library testing
target_link_libraries(test_push_constant_gatherer_node PRIVATE
    GTest::gtest_main
    RenderGraph
    ShaderManagementTestFixtures
)


# Visual Studio solution folder organization
set_target_properties(test_push_constant_gatherer_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_push_constant_gatherer_node)

message(STATUS "[RenderGraph Tests] Added: test_push_constant_gatherer_node")

# DescriptorResourceGathererNode Tests
add_executable(test_descriptor_resource_gatherer_node
    Nodes/test_descriptor_resource_gatherer_node.cpp
)

# Link against library targets - includes and test fixtures propagate automatically
# ShaderManagementTestFixtures exposes test fixture headers for cross-library testing
target_link_libraries(test_descriptor_resource_gatherer_node PRIVATE
    GTest::gtest_main
    RenderGraph
    ShaderManagementTestFixtures
)


# Visual Studio solution folder organization
set_target_properties(test_descriptor_resource_gatherer_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_descriptor_resource_gatherer_node)

message(STATUS "[RenderGraph Tests] Added: test_descriptor_resource_gatherer_node")
