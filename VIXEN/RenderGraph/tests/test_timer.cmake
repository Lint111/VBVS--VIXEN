# ===========================================================================
# Timer Tests - Core Infrastructure
# ===========================================================================
# This test suite validates the Timer class (Core infrastructure):
# - High-resolution delta time measurement
# - Elapsed time tracking
# - Reset functionality
# - Precision validation
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).

add_executable(test_timer
    Core/test_timer.cpp
)

# Allow tests to include library headers with clean paths: #include "RenderGraph/..."
target_include_directories(test_timer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_timer PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_timer)

message(STATUS "[RenderGraph Tests] Added: test_timer")
