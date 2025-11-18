# ============================================================================
# P5: Descriptor & Resource Nodes Tests
# ============================================================================
# DescriptorSetNode, TextureLoaderNode, VertexBufferNode,
# DepthBufferNode, DescriptorResourceGathererNode
# Coverage: Config validation, slot metadata, type checking
# Lines: 200+, Tests: 25+

set(TEST_RESOURCE_NODES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/Resources/test_resource_nodes.cpp
)

add_executable(test_resource_nodes ${TEST_RESOURCE_NODES_SOURCES})

target_link_libraries(test_resource_nodes
    PRIVATE
        RenderGraph
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(test_resource_nodes
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Code coverage support
if(ENABLE_COVERAGE)
    if(MSVC)
        target_compile_options(test_resource_nodes PRIVATE /ZI /Od)
        target_link_options(test_resource_nodes PRIVATE /PROFILE /DEBUG:FULL)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_resource_nodes PRIVATE --coverage -fprofile-arcs -ftest-coverage -O0 -g)
        target_link_libraries(test_resource_nodes PRIVATE gcov)
    endif()
endif()

gtest_discover_tests(test_resource_nodes
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
        LABELS "P5;ResourceNodes;Descriptors;Config;Unit"
)

message(STATUS "✅ P5 Descriptor & Resource Nodes Tests configured (25+ tests)")
