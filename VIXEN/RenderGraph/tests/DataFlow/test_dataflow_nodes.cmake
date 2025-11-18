# ============================================================================
# P7: Data Flow Nodes Tests
# ============================================================================
# ConstantNode, LoopBridgeNode, BoolOpNode, ShaderLibraryNode
# Coverage: Config validation, slot metadata, type checking
# Lines: 220+, Tests: 24+

set(TEST_DATAFLOW_NODES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/DataFlow/test_dataflow_nodes.cpp
)

add_executable(test_dataflow_nodes ${TEST_DATAFLOW_NODES_SOURCES})

target_link_libraries(test_dataflow_nodes
    PRIVATE
        RenderGraph
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(test_dataflow_nodes
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Code coverage support
if(ENABLE_COVERAGE)
    if(MSVC)
        target_compile_options(test_dataflow_nodes PRIVATE /ZI /Od)
        target_link_options(test_dataflow_nodes PRIVATE /PROFILE /DEBUG:FULL)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_dataflow_nodes PRIVATE --coverage -fprofile-arcs -ftest-coverage -O0 -g)
        target_link_libraries(test_dataflow_nodes PRIVATE gcov)
    endif()
endif()

gtest_discover_tests(test_dataflow_nodes
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
        LABELS "P7;DataFlowNodes;Config;Unit"
)

message(STATUS "✅ P7 Data Flow Nodes Tests configured (24+ tests)")
