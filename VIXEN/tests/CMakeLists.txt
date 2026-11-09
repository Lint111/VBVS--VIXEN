# ============================================================================
# VIXEN Project Test Suite
# ============================================================================
#
# This file configures all unit tests for the project using Google Test.
# Each library has its own test subdirectory with focused unit tests.
#
# To run tests:
#   cmake --build . && ctest
#   or
#   cmake --build . && ctest --output-on-failure
#

cmake_minimum_required(VERSION 3.15...4.2)

# This CMakeLists aggregates all per-library test subdirectories so the top-level
# CMake can simply call add_subdirectory(tests).
vixen_schedule_runtime_dlls_for_current_directory()

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest OPTIONAL)

add_test(NAME Codegen.LauncherArguments
    COMMAND "${CMAKE_COMMAND}" -P "${VIXEN_ROOT}/cmake/tests/test_codegen_launcher_args.cmake")
set_tests_properties(Codegen.LauncherArguments PROPERTIES LABELS "Codegen;cpu")

add_test(NAME Codegen.StageGpuSchemaSource
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_STAGE_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/stage-gpu-schema-source"
            -P "${VIXEN_ROOT}/cmake/tests/test_stage_gpu_schema_source.cmake")
set_tests_properties(Codegen.StageGpuSchemaSource PROPERTIES LABELS "Codegen;cpu")

add_test(NAME Native.WslDozenHostGate
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_DOZEN_GATE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/wsl-dozen-host-gate"
            -P "${VIXEN_ROOT}/cmake/tests/test_wsl_dozen_host_gate.cmake")
set_tests_properties(Native.WslDozenHostGate PROPERTIES LABELS "cpu")

add_test(NAME Native.LookdevCaptureWrapperArgs
    COMMAND "${CMAKE_COMMAND}" "-DLOOKDEV_ARGS_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/lookdev-wrapper-args"
            "-DLOOKDEV_WRAPPER=${VIXEN_ROOT}/application/main/tests/run_lookdev_capture.cmake"
            -P "${VIXEN_ROOT}/cmake/tests/test_run_lookdev_capture_args.cmake")
set_tests_properties(Native.LookdevCaptureWrapperArgs PROPERTIES LABELS "cpu")

add_test(NAME Native.WindowsGitTrust
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_GIT_TRUST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/windows-git-trust"
            -P "${VIXEN_ROOT}/cmake/tests/test_windows_git_trust.cmake")
set_tests_properties(Native.WindowsGitTrust PROPERTIES LABELS "cpu")

add_test(NAME Provision.GdbDependencyPackages
    COMMAND "${CMAKE_COMMAND}" -P "${VIXEN_ROOT}/cmake/tests/test_gdb_dependency_packages.cmake")
set_tests_properties(Provision.GdbDependencyPackages PROPERTIES LABELS "cpu")

add_test(NAME Provision.GpuEnvironmentScript
    COMMAND "${CMAKE_COMMAND}" "-DCMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${VIXEN_ROOT}/cmake/tests/test_gpu_environment_script.cmake")
set_tests_properties(Provision.GpuEnvironmentScript PROPERTIES LABELS "cpu")
add_test(NAME Provision.VulkanNotFoundMessage
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_SMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/vulkan-not-found"
            -P "${VIXEN_ROOT}/cmake/tests/test_vulkan_not_found_message.cmake")
set_tests_properties(Provision.VulkanNotFoundMessage PROPERTIES LABELS "cpu")
add_test(NAME Provision.WindowingFailure
    COMMAND "${CMAKE_COMMAND}"
            "-DVIXEN_WINDOWING_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/windowing-provisioning"
            -P "${VIXEN_ROOT}/cmake/tests/test_windowing_provisioning.cmake")
set_tests_properties(Provision.WindowingFailure PROPERTIES LABELS "cpu")

add_test(NAME Native.KernelPinOverride
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_PIN_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/kernel-pin-override"
            -P "${VIXEN_ROOT}/cmake/tests/test_kernel_pin_override.cmake")
set_tests_properties(Native.KernelPinOverride PROPERTIES LABELS "cpu")

add_test(NAME Native.NinjaVisibleLauncher
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_LAUNCHER_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ninja-visible-launcher"
            -P "${VIXEN_ROOT}/cmake/tests/test_ninja_visible_launcher.cmake")
set_tests_properties(Native.NinjaVisibleLauncher PROPERTIES LABELS "cpu")

add_test(NAME Native.CompilerCacheOff
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_CCACHE_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/compiler-cache-off"
            -P "${VIXEN_ROOT}/cmake/tests/test_compiler_cache_off.cmake")
set_tests_properties(Native.CompilerCacheOff PROPERTIES LABELS "cpu")

add_test(NAME Native.KernelSnapshotReuse
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_SNAPSHOT_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/kernel-snapshot-reuse"
            -P "${VIXEN_ROOT}/cmake/tests/test_kernel_snapshot_reuse.cmake")
set_tests_properties(Native.KernelSnapshotReuse PROPERTIES LABELS "cpu")

add_test(NAME Native.KernelSnapshotPublish
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_SNAPSHOT_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/kernel-snapshot-publish"
            -P "${VIXEN_ROOT}/cmake/tests/test_kernel_snapshot_publish.cmake")
set_tests_properties(Native.KernelSnapshotPublish PROPERTIES LABELS "cpu")

add_test(NAME Native.KernelCacheEnv
    COMMAND "${CMAKE_COMMAND}" -P "${VIXEN_ROOT}/cmake/tests/test_kernel_cache_env.cmake")
set_tests_properties(Native.KernelCacheEnv PROPERTIES LABELS "cpu")

add_test(NAME Native.YeroketRootOrder
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_YEROKET_ROOT_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/yeroket-root-order"
            -P "${VIXEN_ROOT}/cmake/tests/test_yeroket_root_order.cmake")
set_tests_properties(Native.YeroketRootOrder PROPERTIES LABELS "cpu")

if(UNIX)
    add_test(NAME Native.WslRouteLauncher
        COMMAND bash "${VIXEN_ROOT}/cmake/tests/test_vixen_native_launcher.sh" "${VIXEN_ROOT}/.."
                "${CMAKE_CURRENT_BINARY_DIR}/wsl-route-launcher")
    set_tests_properties(Native.WslRouteLauncher PROPERTIES LABELS "cpu")
endif()

# Common test compile options
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ============================================================================
# Test Organization Note
# ============================================================================
#
# Tests have been migrated to component-local directories:
#   - CashSystem/tests/
#   - EventBus/tests/
#   - logger/tests/
#   - RenderGraph/tests/
#   - ResourceManagement/tests/
#   - ShaderManagement/tests/
#   - source/VulkanResources/tests/
#
# Each component's CMakeLists.txt now includes its own tests/ subdirectory
# when BUILD_TESTS is enabled. This centralized tests/ directory is deprecated
# and should be removed once migration is complete.
#
# ============================================================================

# ============================================================================
# Test Discovery Complete
# ============================================================================
# All component tests are now added via their respective CMakeLists.txt files:
#   - logger/CMakeLists.txt includes logger/tests/
#   - EventBus/CMakeLists.txt includes EventBus/tests/
#   - ResourceManagement/CMakeLists.txt includes ResourceManagement/tests/
#   - ShaderManagement/CMakeLists.txt includes ShaderManagement/tests/
#   - source/VulkanResources/CMakeLists.txt includes source/VulkanResources/tests/
#   - RenderGraph/CMakeLists.txt includes RenderGraph/tests/
#   - CashSystem/CMakeLists.txt includes CashSystem/tests/
#
# This centralized tests/ directory serves only as a common configuration point.
# No test targets are defined here directly.

# ============================================================================
# Standalone Test Executables (Not Google Test)
# ============================================================================

# Coordinate System Test - Verifies Vulkan coordinate conventions
add_executable(coordinate_system_test coordinate_system_test.cpp)
target_link_libraries(coordinate_system_test PRIVATE glm::glm)
set_target_properties(coordinate_system_test PROPERTIES
    FOLDER "Tests/Standalone"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/binaries/tests"
)

message(STATUS "Tests aggregation ready. Use ctest to run the discovered tests.")

add_test(NAME Native.CaptureRootContract
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_CAPTURE_ROOT_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/capture-root-contract"
            -P "${VIXEN_ROOT}/cmake/tests/test_capture_root.cmake")
set_tests_properties(Native.CaptureRootContract PROPERTIES LABELS "cpu")

add_test(NAME Native.AutoMessageTypeUnique
    COMMAND "${CMAKE_COMMAND}" "-DVIXEN_LIBRARIES_DIR=${VIXEN_ROOT}/libraries"
            "-DVIXEN_APPLICATION_DIR=${VIXEN_ROOT}/application"
            -P "${VIXEN_ROOT}/cmake/tests/test_auto_message_type_unique.cmake")
set_tests_properties(Native.AutoMessageTypeUnique PROPERTIES LABELS "cpu")

add_test(NAME Native.VixenTestClassApplyLoadContext
    COMMAND "${CMAKE_COMMAND}"
            "-DVIXEN_TEST_CLASS_APPLY=${VIXEN_ROOT}/cmake/VixenTestClassApply.cmake"
            "-DVIXEN_TEST_APPLY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/vixen-test-class-apply"
            -P "${VIXEN_ROOT}/cmake/tests/test_vixen_test_class_apply.cmake")
set_tests_properties(Native.VixenTestClassApplyLoadContext PROPERTIES LABELS "cpu")

if(VIXEN_RENDERER_BYTE_IDENTITY_TEST)
    set(_renderer_identity_undertow_root "$ENV{UNDERTOW_ROOT}")
    add_test(NAME Native.RendererObjectByteIdentity
        COMMAND "${CMAKE_COMMAND}"
                "-DVIXEN_SOURCE_DIR=${VIXEN_ROOT}"
                "-DVIXEN_BYTE_IDENTITY_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/renderer-object-byte-identity"
                "-DVIXEN_IDENTITY_UNDERTOW_ROOT=${_renderer_identity_undertow_root}"
                "-DVIXEN_IDENTITY_FETCHCONTENT_BASE_DIR=${FETCHCONTENT_BASE_DIR}"
                "-DVIXEN_IDENTITY_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                -P "${VIXEN_ROOT}/cmake/tests/test_renderer_object_byte_identity.cmake")
    set_tests_properties(Native.RendererObjectByteIdentity PROPERTIES LABELS "cpu")
endif()
