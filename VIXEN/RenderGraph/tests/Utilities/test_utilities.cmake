# ============================================================================
# P8: Utility Classes Tests
# ============================================================================
# NodeType, NodeTypeRegistry, TypedConnection, IGraphCompilable,
# INodeWiring, UnknownTypeRegistry, NodeLogging
# Coverage: Type system, registry, interfaces, utilities
# Lines: 300+, Tests: 30+

set(TEST_UTILITIES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/Utilities/test_utilities.cpp
)

add_executable(test_utilities ${TEST_UTILITIES_SOURCES})

target_link_libraries(test_utilities
    PRIVATE
        RenderGraph
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(test_utilities
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Code coverage support
if(ENABLE_COVERAGE)
    if(MSVC)
        target_compile_options(test_utilities PRIVATE /ZI /Od)
        target_link_options(test_utilities PRIVATE /PROFILE /DEBUG:FULL)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_utilities PRIVATE --coverage -fprofile-arcs -ftest-coverage -O0 -g)
        target_link_libraries(test_utilities PRIVATE gcov)
    endif()
endif()

gtest_discover_tests(test_utilities
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
        LABELS "P8;Utilities;Interfaces;TypeSystem;Unit"
)

message(STATUS "✅ P8 Utility Classes Tests configured (30+ tests)")
