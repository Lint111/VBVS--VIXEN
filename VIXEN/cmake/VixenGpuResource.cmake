# VixenGpuResource.cmake
#
# The one place that says what "this test needs the GPU device" means to CTest.
#
# Every GPU test requests one slot of the `gpu` resource (RESOURCE_GROUPS gpu:1). The slot count is
# the number of GPU tests CTest may run at the same time; it comes from a CTest resource spec file
# generated into the build tree, never from a constant in a test. The file is wired into the build's
# DartConfiguration.tcl, so a plain `ctest -j` honours it. Override per run with
# `ctest --resource-spec-file <file>` (any file with a `gpu` resource), or per build with
# `-DVIXEN_GPU_TEST_SLOTS=<n>`.
#
# This replaces RESOURCE_LOCK vixen_gpu_device, which serialised every GPU test and was set up to
# three times on the same case (the suite flag, the per-case filter and the class stub each added
# it). A property SET (never APPEND) keeps one request per case.

# Default 8: the RenderGraph suite (1507 tests, 1047 GPU) gave byte-identical PNGs and the same
# pass/fail set at 1, 2, 4 and 8 slots, three repeats each, on Dozen and on the native Windows Debug
# build (reports/sn-rgconc1.md). Peak memory of one ctest process grew 2.5 GB -> 5.8 GB from 1 to 8
# slots on Dozen, inside the queue's 7 GB admission.
set(VIXEN_GPU_TEST_SLOTS "8" CACHE STRING
    "GPU tests CTest may run concurrently: slots of the `gpu` resource in the generated resource spec")

# Value of the CTest RESOURCE_GROUPS property on a GPU test. Also handed to the class stub, which
# runs as a separate CTest-time script and cannot see this variable.
set(VIXEN_GPU_RESOURCE_GROUPS "gpu:1")

# Marks existing tests (names) as needing the GPU device. CTest ignores names that do not exist.
function(vixen_gpu_resource_apply)
    set_tests_properties(${ARGN} PROPERTIES RESOURCE_GROUPS "${VIXEN_GPU_RESOURCE_GROUPS}")
endfunction()

# Writes <build>/vixen-gpu-resources.json and points CTest at it. Call once, in the directory
# that calls enable_testing(), before include(CTest).
function(vixen_configure_gpu_resource_spec)
    if(NOT VIXEN_GPU_TEST_SLOTS MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "VIXEN_GPU_TEST_SLOTS must be a positive integer, got '${VIXEN_GPU_TEST_SLOTS}'.")
    endif()
    set(_spec "${CMAKE_BINARY_DIR}/vixen-gpu-resources.json")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VixenWriteIfDifferent.cmake")
    vixen_write_if_different("${_spec}"
        "{\"version\":{\"major\":1,\"minor\":0},\"local\":[{\"gpu\":[{\"id\":\"0\",\"slots\":${VIXEN_GPU_TEST_SLOTS}}]}]}\n")
    set(CTEST_RESOURCE_SPEC_FILE "${_spec}" PARENT_SCOPE)
endfunction()
