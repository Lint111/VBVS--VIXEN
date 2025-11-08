# ===========================================================================
# LoopManager Tests - Core Infrastructure
# ===========================================================================
# This test suite validates the LoopManager class (Core infrastructure):
# - Loop registration and ID management
# - Variable timestep loops
# - Fixed timestep loops (60Hz, 120Hz)
# - Three catchup modes: FireAndForget, SingleCorrectiveStep, MultipleSteps
# - Spiral of death protection (maxCatchupTime)
# - Frame index tracking
# - Step count tracking
# - Multiple independent loops
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).

add_executable(test_loop_manager
    Core/test_loop_manager.cpp
)

target_link_libraries(test_loop_manager PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_loop_manager)

message(STATUS "[RenderGraph Tests] Added: test_loop_manager")
