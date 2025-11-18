# ============================================================================
# P6: Rendering Nodes Tests
# ============================================================================
# FramebufferNode, GeometryRenderNode, PresentNode
# Coverage: Config validation, slot metadata, type checking
# Lines: 180+, Tests: 20+

set(TEST_RENDERING_NODES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/Rendering/test_rendering_nodes.cpp
)

add_executable(test_rendering_nodes ${TEST_RENDERING_NODES_SOURCES})

target_link_libraries(test_rendering_nodes
    PRIVATE
        RenderGraph
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(test_rendering_nodes
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Code coverage support
if(ENABLE_COVERAGE)
    if(MSVC)
        target_compile_options(test_rendering_nodes PRIVATE /ZI /Od)
        target_link_options(test_rendering_nodes PRIVATE /PROFILE /DEBUG:FULL)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_rendering_nodes PRIVATE --coverage -fprofile-arcs -ftest-coverage -O0 -g)
        target_link_libraries(test_rendering_nodes PRIVATE gcov)
    endif()
endif()

gtest_discover_tests(test_rendering_nodes
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
        LABELS "P6;RenderingNodes;Config;Unit"
)

message(STATUS "✅ P6 Rendering Nodes Tests configured (20+ tests)")
