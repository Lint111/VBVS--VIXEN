# VixenTesting.cmake
#
# Configure-time runtime environment and suite metadata for CTest entries. GoogleTest's
# gtest_discover_tests() creates one CTest entry per case, so labels, fixtures and environment
# belong in its PROPERTIES rather than on the executable target.

function(vixen_configure_ctest_gpu_environment output_variable)
    set(_environment)
    set(_library_dirs)

    if(VIXEN_WSL_DZN_ICD)
        list(APPEND _environment "VK_ICD_FILENAMES=${VIXEN_WSL_DZN_ICD}")
    endif()
    if(VIXEN_VULKAN_LAYER_PATH)
        list(APPEND _environment "VK_LAYER_PATH=${VIXEN_VULKAN_LAYER_PATH}")
    endif()

    if(UNIX)
        if(VIXEN_WSL_DZN_LIBRARY_DIR AND IS_DIRECTORY "${VIXEN_WSL_DZN_LIBRARY_DIR}")
            list(APPEND _library_dirs "${VIXEN_WSL_DZN_LIBRARY_DIR}")
        endif()

        if(VIXEN_WINDOWING_CACHE_DIR)
            foreach(_candidate
                "${VIXEN_WINDOWING_CACHE_DIR}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
                "${VIXEN_WINDOWING_CACHE_DIR}/usr/lib")
                if(IS_DIRECTORY "${_candidate}")
                    list(APPEND _library_dirs "${_candidate}")
                endif()
            endforeach()
        endif()

        if(VULKAN_PATH)
            foreach(_candidate "${VULKAN_PATH}/lib/VulkanLoader/lib" "${VULKAN_PATH}/lib")
                if(IS_DIRECTORY "${_candidate}")
                    list(APPEND _library_dirs "${_candidate}")
                endif()
            endforeach()
        endif()

        if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
            list(APPEND _library_dirs "$ENV{LD_LIBRARY_PATH}")
        endif()

        list(REMOVE_DUPLICATES _library_dirs)
        if(_library_dirs)
            string(JOIN ":" _ld_library_path ${_library_dirs})
            list(APPEND _environment "LD_LIBRARY_PATH=${_ld_library_path}")
        endif()
    endif()

    set(${output_variable} "${_environment}" PARENT_SCOPE)
endfunction()

function(vixen_gtest_discover_tests suite target)
    cmake_parse_arguments(PARSE_ARGV 2 VGT
        "NO_PRETTY_TYPES;NO_PRETTY_VALUES;GPU"
        "DISCOVERY_MODE;DISCOVERY_TIMEOUT;WORKING_DIRECTORY;TEST_LIST;TEST_PREFIX;TEST_SUFFIX;XML_OUTPUT_DIR;TIMEOUT;FIXTURES_REQUIRED"
        "EXTRA_ARGS;TEST_ENVIRONMENT;TEST_ENVIRONMENT_MODIFICATION;GPU_TESTS")

    set(_gtest_arguments)
    foreach(_option NO_PRETTY_TYPES NO_PRETTY_VALUES)
        if(VGT_${_option})
            list(APPEND _gtest_arguments "${_option}")
        endif()
    endforeach()
    foreach(_argument DISCOVERY_MODE DISCOVERY_TIMEOUT WORKING_DIRECTORY TEST_LIST TEST_PREFIX TEST_SUFFIX XML_OUTPUT_DIR)
        if(DEFINED VGT_${_argument})
            list(APPEND _gtest_arguments "${_argument}" "${VGT_${_argument}}")
        endif()
    endforeach()
    if(VGT_EXTRA_ARGS)
        list(APPEND _gtest_arguments EXTRA_ARGS ${VGT_EXTRA_ARGS})
    endif()

    set(_properties LABELS "${suite}")
    set(_environment ${VIXEN_CTEST_GPU_ENVIRONMENT} ${VGT_TEST_ENVIRONMENT})
    if(_environment)
        string(JOIN ";" _environment_value ${_environment})
        # GoogleTest serializes properties through a second CMake script, so
        # preserve each semicolon across both list-expansion boundaries.
        string(REPLACE ";" "\\\\;" _environment_value "${_environment_value}")
        list(APPEND _properties ENVIRONMENT "${_environment_value}")
    endif()
    if(VGT_TEST_ENVIRONMENT_MODIFICATION)
        string(JOIN ";" _environment_modification ${VGT_TEST_ENVIRONMENT_MODIFICATION})
        string(REPLACE ";" "\\\\;" _environment_modification "${_environment_modification}")
        list(APPEND _properties ENVIRONMENT_MODIFICATION "${_environment_modification}")
    endif()
    if(VGT_FIXTURES_REQUIRED)
        string(REPLACE ";" "\\\\;" _fixtures_required "${VGT_FIXTURES_REQUIRED}")
        list(APPEND _properties FIXTURES_REQUIRED "${_fixtures_required}")
    endif()
    if(VGT_TIMEOUT)
        list(APPEND _properties TIMEOUT "${VGT_TIMEOUT}")
    endif()
    if(VGT_GPU)
        list(APPEND _properties RESOURCE_LOCK "vixen_gpu_device")
    endif()

    if(VGT_GPU_TESTS)
        set(_gpu_test_list "_vixen_gpu_tests_${target}")
        list(APPEND _gtest_arguments TEST_LIST "${_gpu_test_list}")

        # PRE_TEST discovery creates cases at CTest run time. Its TEST_LIST variable
        # is available to TEST_INCLUDE_FILES, which can apply locks per discovered case.
        set(_gpu_test_lock_script "${CMAKE_CURRENT_BINARY_DIR}/vixen_gpu_test_locks_${target}.cmake")
        set(_gpu_test_lock_content "foreach(_vixen_gpu_test IN LISTS ${_gpu_test_list})\n")
        foreach(_pattern IN LISTS VGT_GPU_TESTS)
            string(APPEND _gpu_test_lock_content
                "  if(_vixen_gpu_test MATCHES [==[${_pattern}]==])\n"
                "    set_tests_properties(\"\${_vixen_gpu_test}\" PROPERTIES RESOURCE_LOCK \"vixen_gpu_device\")\n"
                "  endif()\n")
        endforeach()
        string(APPEND _gpu_test_lock_content "endforeach()\n")
        file(WRITE "${_gpu_test_lock_script}" "${_gpu_test_lock_content}")

    endif()

    gtest_discover_tests(${target} ${_gtest_arguments} PROPERTIES ${_properties})

    if(VGT_GPU_TESTS)
        # GoogleTest appends its discovery include while registering the target.
        # Append after it so the per-case TEST_LIST is populated before filtering.
        get_property(_directory_test_include_files DIRECTORY PROPERTY TEST_INCLUDE_FILES)
        if(NOT "${_gpu_test_lock_script}" IN_LIST _directory_test_include_files)
            set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_gpu_test_lock_script}")
        endif()
    endif()
endfunction()

function(vixen_set_suite_properties suite)
    cmake_parse_arguments(PARSE_ARGV 1 VSP "GPU" "" "TESTS;TEST_ENVIRONMENT")
    if(NOT VSP_TESTS)
        return()
    endif()

    set(_properties LABELS "${suite}")
    set(_environment ${VIXEN_CTEST_GPU_ENVIRONMENT} ${VSP_TEST_ENVIRONMENT})
    if(_environment)
        string(JOIN ";" _environment_value ${_environment})
        string(REPLACE ";" "\\;" _environment_value "${_environment_value}")
        list(APPEND _properties ENVIRONMENT "${_environment_value}")
    endif()
    if(VSP_GPU)
        list(APPEND _properties RESOURCE_LOCK "vixen_gpu_device")
    endif()
    set_tests_properties(${VSP_TESTS} PROPERTIES ${_properties})
endfunction()

function(vixen_lock_gpu_tests)
    cmake_parse_arguments(PARSE_ARGV 0 VLG "" "" "TESTS")
    if(NOT VLG_TESTS)
        message(FATAL_ERROR "vixen_lock_gpu_tests requires TESTS")
    endif()
    set_tests_properties(${VLG_TESTS} PROPERTIES RESOURCE_LOCK "vixen_gpu_device")
endfunction()

function(_vixen_collect_subtree_targets directory output_variable)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        get_filename_component(_subdirectory "${_subdirectory}" ABSOLUTE BASE_DIR "${directory}")
        _vixen_collect_subtree_targets("${_subdirectory}" _child_targets)
        list(APPEND _targets ${_child_targets})
    endforeach()
    set(${output_variable} "${_targets}" PARENT_SCOPE)
endfunction()

function(vixen_register_verification_targets directory)
    _vixen_collect_subtree_targets("${directory}" _suite_targets)
    set_property(GLOBAL APPEND PROPERTY VIXEN_RENDERGRAPH_SVO_BUILD_TARGETS ${_suite_targets})
endfunction()
