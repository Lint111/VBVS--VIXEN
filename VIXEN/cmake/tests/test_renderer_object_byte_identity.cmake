cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED VIXEN_SOURCE_DIR OR NOT DEFINED VIXEN_BYTE_IDENTITY_TEST_ROOT)
    message(FATAL_ERROR
        "VIXEN_SOURCE_DIR and VIXEN_BYTE_IDENTITY_TEST_ROOT are required")
endif()

set(_build_a "${VIXEN_BYTE_IDENTITY_TEST_ROOT}/build-a")
set(_build_b "${VIXEN_BYTE_IDENTITY_TEST_ROOT}/build-b")
file(REMOVE_RECURSE "${VIXEN_BYTE_IDENTITY_TEST_ROOT}")
file(MAKE_DIRECTORY "${VIXEN_BYTE_IDENTITY_TEST_ROOT}")

set(_configure_args
    -S "${VIXEN_SOURCE_DIR}"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTS=OFF
    -DVIXEN_CODEGEN_GATE=ON
    -DVIXEN_NO_NEW_MUTEX_GATE=OFF
    -DVIXEN_AUTO_PROVISION_WSL_VULKAN=ON
    -DVIXEN_AUTO_DERIVE_UNDERTOW_ROOT=OFF
    -DVIXEN_RUNTIME_CACHE_DIR=runtime-cache
)
if(DEFINED VIXEN_IDENTITY_CXX_COMPILER AND NOT "${VIXEN_IDENTITY_CXX_COMPILER}" STREQUAL "")
    list(APPEND _configure_args "-DCMAKE_CXX_COMPILER=${VIXEN_IDENTITY_CXX_COMPILER}")
endif()
if(DEFINED VIXEN_IDENTITY_FETCHCONTENT_BASE_DIR
   AND NOT "${VIXEN_IDENTITY_FETCHCONTENT_BASE_DIR}" STREQUAL "")
    list(APPEND _configure_args
        "-DFETCHCONTENT_BASE_DIR=${VIXEN_IDENTITY_FETCHCONTENT_BASE_DIR}")
endif()

function(configure_build build_dir)
    set(_command "${CMAKE_COMMAND}" -S "${VIXEN_SOURCE_DIR}" -B "${build_dir}")
    list(APPEND _command ${_configure_args})
    if(DEFINED VIXEN_IDENTITY_UNDERTOW_ROOT
       AND NOT "${VIXEN_IDENTITY_UNDERTOW_ROOT}" STREQUAL "")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                    "UNDERTOW_ROOT=${VIXEN_IDENTITY_UNDERTOW_ROOT}"
                    ${_command}
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _stdout
            ERROR_VARIABLE _stderr)
    else()
        message(FATAL_ERROR
            "VIXEN_IDENTITY_UNDERTOW_ROOT is required: this test must use a generated Undertow provider")
    endif()
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "configure failed for ${build_dir}\n${_stdout}\n${_stderr}")
    endif()
endfunction()

configure_build("${_build_a}")
configure_build("${_build_b}")

foreach(_build_dir IN ITEMS "${_build_a}" "${_build_b}")
    set(_build_command "${CMAKE_COMMAND}" --build "${_build_dir}" --target
        Core RenderGraphCore RenderGraphNode_DeviceNode --parallel 4)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "UNDERTOW_ROOT=${VIXEN_IDENTITY_UNDERTOW_ROOT}"
                ${_build_command}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "build failed for ${_build_dir}\n${_stdout}\n${_stderr}")
    endif()
endforeach()

function(compare_object relative_path)
    set(_path_a "${_build_a}/${relative_path}")
    set(_path_b "${_build_b}/${relative_path}")
    if(NOT EXISTS "${_path_a}" OR NOT EXISTS "${_path_b}")
        message(FATAL_ERROR "expected renderer objects are missing: ${_path_a}; ${_path_b}")
    endif()
    file(SHA256 "${_path_a}" _hash_a)
    file(SHA256 "${_path_b}" _hash_b)
    if(NOT _hash_a STREQUAL _hash_b)
        message(FATAL_ERROR
            "renderer object differs across build directories: ${relative_path}\n"
            "  build-a: ${_hash_a}\n  build-b: ${_hash_b}")
    endif()
    message(STATUS "byte-identical renderer object: ${relative_path} (${_hash_a})")
endfunction()

compare_object("libraries/Core/CMakeFiles/Core.dir/src/RuntimeCachePaths.cpp.o")
compare_object("libraries/RenderGraph/CMakeFiles/RenderGraphCore.dir/src/Core/RenderGraph.cpp.o")
compare_object("libraries/RenderGraph/CMakeFiles/RenderGraphNode_DeviceNode.dir/src/Nodes/DeviceNode.cpp.o")
