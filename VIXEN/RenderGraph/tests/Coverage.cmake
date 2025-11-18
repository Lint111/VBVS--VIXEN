# ===========================================================================
# Code Coverage Configuration for Visual Studio Community & Command Line Tools
# ===========================================================================
# This file configures code coverage instrumentation for RenderGraph tests.
# Supports: Visual Studio Community 2022, OpenCppCoverage, gcov/lcov
#
# Usage in Visual Studio:
#   1. Open Test Explorer
#   2. Run > Analyze Code Coverage for All Tests
#   3. View results in Code Coverage Results window
#
# Usage with OpenCppCoverage (Windows):
#   OpenCppCoverage.exe --sources RenderGraph\include --sources RenderGraph\src ^
#       --export_type cobertura:coverage.xml -- test_executable.exe
#
# Usage with gcov/lcov (Linux/WSL):
#   cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..
#   make
#   make test
#   make coverage
#

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(ENABLE_COVERAGE)
    message(STATUS "[Coverage] Code coverage instrumentation ENABLED")

    if(MSVC)
        # Visual Studio Code Coverage
        # Instruments binaries for coverage data collection
        message(STATUS "[Coverage] Using Visual Studio Code Coverage")

        # Add coverage flags for MSVC
        set(COVERAGE_COMPILE_FLAGS "/ZI /Od")
        set(COVERAGE_LINK_FLAGS "/PROFILE /DEBUG:FULL")

        # Apply to all test targets
        add_compile_options(${COVERAGE_COMPILE_FLAGS})
        add_link_options(${COVERAGE_LINK_FLAGS})

        message(STATUS "[Coverage] MSVC flags: ${COVERAGE_COMPILE_FLAGS}")
        message(STATUS "[Coverage] Link flags: ${COVERAGE_LINK_FLAGS}")

    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # GCC/Clang Code Coverage (gcov/lcov)
        message(STATUS "[Coverage] Using GCC/Clang Code Coverage (gcov)")

        set(COVERAGE_COMPILE_FLAGS "--coverage -fprofile-arcs -ftest-coverage -O0 -g")
        set(COVERAGE_LINK_FLAGS "--coverage")

        add_compile_options(${COVERAGE_COMPILE_FLAGS})
        add_link_options(${COVERAGE_LINK_FLAGS})

        message(STATUS "[Coverage] GCC/Clang flags: ${COVERAGE_COMPILE_FLAGS}")

        # Add coverage target for lcov
        find_program(LCOV_PATH lcov)
        find_program(GENHTML_PATH genhtml)

        if(LCOV_PATH AND GENHTML_PATH)
            add_custom_target(coverage
                COMMAND ${LCOV_PATH} --directory . --zerocounters
                COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info
                COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/vcpkg_installed/*' '*/googletest/*' '*/tests/*' --output-file coverage.info.cleaned
                COMMAND ${GENHTML_PATH} -o coverage coverage.info.cleaned
                COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in coverage/index.html"
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating code coverage report"
            )
            message(STATUS "[Coverage] Added 'coverage' target (run with: make coverage)")
        else()
            message(WARNING "[Coverage] lcov/genhtml not found. Coverage report generation disabled.")
        endif()
    else()
        message(WARNING "[Coverage] Unsupported compiler for coverage: ${CMAKE_CXX_COMPILER_ID}")
    endif()

else()
    message(STATUS "[Coverage] Code coverage instrumentation DISABLED (use -DENABLE_COVERAGE=ON to enable)")
endif()

# ===========================================================================
# Coverage Helper Function
# ===========================================================================
# Adds coverage metadata to a test target
function(add_coverage_to_target target_name)
    if(ENABLE_COVERAGE)
        if(MSVC)
            # Set target properties for Visual Studio Code Coverage
            set_target_properties(${target_name} PROPERTIES
                VS_GLOBAL_EnableCppCoreCheck "false"
                VS_GLOBAL_CodeAnalysisRuleSet "NativeRecommendedRules.ruleset"
                VS_GLOBAL_RunCodeAnalysis "false"
            )
        endif()

        message(STATUS "[Coverage] Added coverage to target: ${target_name}")
    endif()
endfunction()

# ===========================================================================
# Coverage Output Configuration
# ===========================================================================
if(ENABLE_COVERAGE)
    # Create coverage output directory
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/TestResults/Coverage)
    message(STATUS "[Coverage] Output directory: ${CMAKE_BINARY_DIR}/TestResults/Coverage")
endif()

# ===========================================================================
# Visual Studio Test Adapter Configuration
# ===========================================================================
# Configure Google Test adapter for Visual Studio Test Explorer
if(MSVC)
    # Set environment variables for test discovery
    set(ENV{VSTEST_HOST_DEBUG} "0")

    # Configure test adapter paths (if using vcpkg)
    if(DEFINED ENV{VCPKG_ROOT})
        set(GTEST_ADAPTER_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/googletest")
        if(EXISTS ${GTEST_ADAPTER_PATH})
            message(STATUS "[Coverage] Google Test adapter path: ${GTEST_ADAPTER_PATH}")
        endif()
    endif()
endif()

# ===========================================================================
# Documentation
# ===========================================================================
# Coverage Workflow:
#
# 1. Visual Studio Community:
#    - Open solution in VS
#    - Test > Analyze Code Coverage for All Tests
#    - View in Code Coverage Results window
#    - Export to .coverage or .coveragexml
#
# 2. Command Line (Windows - OpenCppCoverage):
#    - Install: choco install OpenCppCoverage
#    - Run: OpenCppCoverage --sources RenderGraph -- test.exe
#    - Output: coverage.xml (Cobertura format)
#
# 3. Command Line (Linux - lcov):
#    - cmake -DENABLE_COVERAGE=ON ..
#    - make coverage
#    - Open: coverage/index.html
#
# 4. CI/CD Integration:
#    - Enable ENABLE_COVERAGE in CI builds
#    - Upload coverage.xml to Codecov/Coveralls
#    - Display badge in README
#
# Coverage Metrics:
#  - Line Coverage: % of lines executed
#  - Branch Coverage: % of conditional branches taken
#  - Function Coverage: % of functions called
#
# Target: 80%+ line coverage for core systems
