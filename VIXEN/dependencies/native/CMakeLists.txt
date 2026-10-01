# Native loader dependencies shared by VIXEN's full build and native-only provider mode.
include(FetchContent)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)
if(TARGET glm)
    get_target_property(_glm_includes glm INTERFACE_INCLUDE_DIRECTORIES)
    if(_glm_includes)
        set_property(TARGET glm APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${_glm_includes})
    endif()
    set_target_properties(glm PROPERTIES FOLDER "Dependencies")
endif()

# This is the single Gaia revision shared by VIXEN and the native loader.
set(VIXEN_GAIA_UPSTREAM_REVISION "a2e62491b56a8589c31da97128f0658088050a70")
if((NOT DEFINED FETCHCONTENT_SOURCE_DIR_GAIA OR "${FETCHCONTENT_SOURCE_DIR_GAIA}" STREQUAL "")
   AND DEFINED FETCHCONTENT_BASE_DIR)
    string(SUBSTRING "${VIXEN_GAIA_UPSTREAM_REVISION}" 0 8 _vixen_gaia_revision_prefix)
    set(_vixen_gaia_revision_cache
        "${FETCHCONTENT_BASE_DIR}/gaia-${_vixen_gaia_revision_prefix}/gaia-src")
    if(EXISTS "${_vixen_gaia_revision_cache}/single_include/gaia.h")
        set(FETCHCONTENT_SOURCE_DIR_GAIA "${_vixen_gaia_revision_cache}" CACHE PATH
            "Revision-keyed source directory for VIXEN's pinned Gaia." FORCE)
        message(STATUS "VIXEN: using revision-keyed Gaia cache ${FETCHCONTENT_SOURCE_DIR_GAIA}")
    endif()
endif()
FetchContent_Declare(
    gaia
    GIT_REPOSITORY https://github.com/richardbiely/gaia-ecs.git
    GIT_TAG        "${VIXEN_GAIA_UPSTREAM_REVISION}"
    GIT_SHALLOW    FALSE
    GIT_PROGRESS   TRUE
)
set(GAIA_BUILD_UNITTEST OFF CACHE BOOL "Build Gaia unit tests" FORCE)
set(GAIA_BUILD_EXAMPLES OFF CACHE BOOL "Build Gaia examples" FORCE)
set(GAIA_PROFILER_CPU OFF CACHE BOOL "Enable Gaia CPU profiler" FORCE)
set(GAIA_GENERATE_SINGLE_HEADER OFF CACHE BOOL "Generate the single file header automatically." FORCE)
FetchContent_MakeAvailable(gaia)
if(NOT TARGET gaia)
    message(FATAL_ERROR "VIXEN: FetchContent did not provide the declared Gaia target")
endif()
execute_process(
    COMMAND git -C "${gaia_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE _vixen_gaia_revision_rc
    OUTPUT_VARIABLE _vixen_gaia_actual_revision
    ERROR_VARIABLE _vixen_gaia_revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _vixen_gaia_revision_rc EQUAL 0)
    message(FATAL_ERROR
        "VIXEN: cannot verify the fetched Gaia source revision at ${gaia_SOURCE_DIR}: "
        "${_vixen_gaia_revision_error}")
endif()
if(NOT "${_vixen_gaia_actual_revision}" STREQUAL "${VIXEN_GAIA_UPSTREAM_REVISION}")
    message(FATAL_ERROR
        "VIXEN: Gaia source revision mismatch: declared ${VIXEN_GAIA_UPSTREAM_REVISION}, "
        "fetched ${_vixen_gaia_actual_revision} at ${gaia_SOURCE_DIR}. Refresh the FetchContent "
        "source directory for the declared SHA.")
endif()
set_property(TARGET gaia PROPERTY VIXEN_GAIA_UPSTREAM_REVISION "${VIXEN_GAIA_UPSTREAM_REVISION}")
set_target_properties(gaia PROPERTIES FOLDER "Dependencies")
target_include_directories(gaia INTERFACE $<BUILD_INTERFACE:${gaia_SOURCE_DIR}/single_include>)
set_property(TARGET gaia APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
    $<BUILD_INTERFACE:${gaia_SOURCE_DIR}/single_include>)
message(STATUS "VIXEN: Gaia source verified at ${_vixen_gaia_actual_revision}")

find_package(TBB QUIET)
if(NOT TARGET TBB::tbb)
    FetchContent_Declare(
        TBB
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB
        GIT_TAG v2021.11.0
        GIT_SHALLOW TRUE
    )
    set(TBB_TEST OFF CACHE BOOL "" FORCE)
    set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
    # oneTBB defaults to BUILD_SHARED_LIBS only when the variable is undefined.
    # Other dependencies set the shared-library default to OFF later in the
    # configure, so explicitly scope ON to oneTBB regardless of configure history.
    if(DEFINED BUILD_SHARED_LIBS)
        set(_vixen_build_shared_libs_was_defined TRUE)
        set(_vixen_previous_build_shared_libs "${BUILD_SHARED_LIBS}")
    else()
        set(_vixen_build_shared_libs_was_defined FALSE)
    endif()
    set(BUILD_SHARED_LIBS ON)
    FetchContent_MakeAvailable(TBB)
    if(_vixen_build_shared_libs_was_defined)
        set(BUILD_SHARED_LIBS "${_vixen_previous_build_shared_libs}")
    else()
        unset(BUILD_SHARED_LIBS)
    endif()
    unset(_vixen_build_shared_libs_was_defined)
    unset(_vixen_previous_build_shared_libs)
    foreach(_tbb_target IN ITEMS tbb tbbmalloc tbbmalloc_proxy)
        if(TARGET ${_tbb_target})
            set_target_properties(${_tbb_target} PROPERTIES FOLDER "Dependencies")
            if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                target_compile_options(${_tbb_target} PRIVATE
                    -Wno-error=stringop-overflow -Wno-stringop-overflow)
            endif()
        endif()
    endforeach()
endif()

add_subdirectory("${VIXEN_ROOT}/libraries/KernelDispatch"
    "${CMAKE_CURRENT_BINARY_DIR}/kernel-dispatch")

add_library(VIXENNativeDependencies INTERFACE)
add_library(VIXEN::NativeDependencies ALIAS VIXENNativeDependencies)
target_link_libraries(VIXENNativeDependencies INTERFACE
    gaia glm::glm TBB::tbb KernelDispatch)
