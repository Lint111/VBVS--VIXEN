# ===========================================================================
# Scene Generator Tests (Phase H.2.5)
# ===========================================================================
# This test suite validates procedural scene generation:
# - Cornell Box (10% density ±5%)
# - Cave System (50% density ±5%)
# - Urban Grid (90% density ±5%)
# - Reproducibility and spatial coherence
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

add_executable(test_scene_generators
    Data/test_scene_generators.cpp
)

target_link_libraries(test_scene_generators PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_scene_generators)

message(STATUS "[RenderGraph Tests] Added: test_scene_generators")
