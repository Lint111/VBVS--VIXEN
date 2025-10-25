# ============================================================================
# ShaderManagement Library Tests
# ============================================================================

# Test executable
add_executable(ShaderManagement_Tests
    test_shader_cache_manager.cpp
    test_shader_preprocessor.cpp
    test_shader_compiler.cpp
    test_integration.cpp
)

# Link against the library being tested and Google Test
target_link_libraries(ShaderManagement_Tests
    PRIVATE
        ShaderManagement
        GTest::gtest
        GTest::gtest_main  # Provides main() function
        GTest::gmock       # For mocking if needed
)

# Set C++ standard
set_property(TARGET ShaderManagement_Tests PROPERTY CXX_STANDARD 23)
set_property(TARGET ShaderManagement_Tests PROPERTY CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(ShaderManagement_Tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Discover tests automatically
gtest_discover_tests(ShaderManagement_Tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
        LABELS "ShaderManagement"
        TIMEOUT 30
)

# Add custom target to run just ShaderManagement tests
add_custom_target(test_shader_management
    COMMAND ${CMAKE_CTEST_COMMAND} -L ShaderManagement --output-on-failure
    DEPENDS ShaderManagement_Tests
    COMMENT "Running ShaderManagement tests..."
)

message(STATUS "ShaderManagement tests configured")
