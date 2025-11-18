# ============================================================================
# P4: Pipeline Nodes Tests
# ============================================================================
# GraphicsPipelineNode, RenderPassNode, ComputePipelineNode, ComputeDispatchNode
# Coverage: Config validation, slot metadata, type checking
# Lines: 150+, Tests: 14+

set(TEST_PIPELINE_NODES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/Pipelines/test_pipeline_nodes.cpp
)

add_executable(test_pipeline_nodes ${TEST_PIPELINE_NODES_SOURCES})

target_link_libraries(test_pipeline_nodes
    PRIVATE
        RenderGraph
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(test_pipeline_nodes
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Code coverage support
if(ENABLE_COVERAGE)
    if(MSVC)
        target_compile_options(test_pipeline_nodes PRIVATE /ZI /Od)
        target_link_options(test_pipeline_nodes PRIVATE /PROFILE /DEBUG:FULL)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_pipeline_nodes PRIVATE --coverage -fprofile-arcs -ftest-coverage -O0 -g)
        target_link_libraries(test_pipeline_nodes PRIVATE gcov)
    endif()
endif()

gtest_discover_tests(test_pipeline_nodes
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
        LABELS "P4;PipelineNodes;Config;Unit"
)

message(STATUS "✅ P4 Pipeline Nodes Tests configured (14+ tests)")
