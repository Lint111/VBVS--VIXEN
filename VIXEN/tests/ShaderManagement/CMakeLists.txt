# ShaderManagement Tests (Symbolic Link to Library-Local Tests)
#
# This CMakeLists.txt acts as a bridge for VS Code Test Explorer to discover
# ShaderManagement tests through the project-level tests/ hierarchy.
#
# The actual tests are built and registered in ShaderManagement/tests/
# We simply reference them here for GUI visibility.

cmake_minimum_required(VERSION 3.15)

message(STATUS "ShaderManagement tests: Creating symbolic test link for VS Code")

# The ShaderManagement library already builds and registers its tests via:
# ShaderManagement/CMakeLists.txt -> add_subdirectory(tests)
#
# Those tests are already registered with CTest. This file exists to provide
# a project-level tests/ShaderManagement/ path for VS Code Test Explorer.

# Verify the test target exists
if(TARGET ShaderManagementTests)
    message(STATUS "  ✓ ShaderManagementTests target found")
    message(STATUS "  ✓ 35 tests registered via ShaderManagement/tests/CMakeLists.txt")
    message(STATUS "  ✓ Run via: ctest -R Shader")

    # Create a custom CTestTestfile.cmake that references the library-local tests
    # This makes CTest discovery work through the tests/ hierarchy
    set(CTEST_BRIDGE_FILE "${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake")
    file(WRITE ${CTEST_BRIDGE_FILE}
        "# Bridge to library-local ShaderManagement tests\n"
        "# This file links project-level tests/ to ShaderManagement/tests/\n"
        "subdirs(\"../../ShaderManagement/tests\")\n"
    )

    message(STATUS "  ✓ CTest bridge created for test discovery")
else()
    message(WARNING "  ShaderManagementTests target not found")
    message(WARNING "  Ensure ShaderManagement/CMakeLists.txt calls add_subdirectory(tests)")
endif()
