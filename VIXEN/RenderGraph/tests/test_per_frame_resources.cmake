# ===========================================================================
# PerFrameResources Tests - Core Infrastructure
# ===========================================================================
# This test suite validates the PerFrameResources class (Core infrastructure):
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
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).

add_executable(test_per_frame_resources
    Core/test_per_frame_resources.cpp
)

# Allow tests to include library headers with clean paths: #include "RenderGraph/..."
target_include_directories(test_per_frame_resources PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_per_frame_resources PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_per_frame_resources)

message(STATUS "[RenderGraph Tests] Added: test_per_frame_resources")
