# Stage the transitive Windows runtime DLLs next to each executable.
#
# CMake only provides TARGET_RUNTIME_DLLS with CMake 3.21+, so fail clearly for
# older Windows toolchains instead of producing executables that fail at launch.
include_guard(GLOBAL)

if(WIN32 AND CMAKE_VERSION VERSION_LESS 3.21)
    message(FATAL_ERROR "Windows runtime DLL staging requires CMake 3.21 or newer")
endif()

function(vixen_stage_runtime_dlls_for_target target)
    if(NOT WIN32)
        return()
    endif()

    get_property(_already_staged TARGET "${target}" PROPERTY VIXEN_RUNTIME_DLLS_STAGED)
    if(_already_staged)
        return()
    endif()
    set_property(TARGET "${target}" PROPERTY VIXEN_RUNTIME_DLLS_STAGED TRUE)

    get_target_property(_additional_destinations "${target}" VIXEN_RUNTIME_DLL_DESTINATIONS)
    if(NOT _additional_destinations OR _additional_destinations STREQUAL "_additional_destinations-NOTFOUND")
        set(_additional_destinations_arg "")
    else()
        string(JOIN "|" _additional_destinations_arg ${_additional_destinations})
    endif()

    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DVIXEN_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:${target}>"
            "-DVIXEN_RUNTIME_DLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:${target}>,|>"
            "-DVIXEN_RUNTIME_ADDITIONAL_DESTINATIONS=${_additional_destinations_arg}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageRuntimeDlls.cmake"
        COMMENT "Staging runtime DLLs for ${target}"
        VERBATIM
    )
endfunction()

# Walk the full CMake directory tree after configuration so targets from every
# subdirectory, including included .cmake fragments, receive the same staging.
function(vixen_stage_runtime_dlls_in_directory directory)
    if(NOT WIN32)
        return()
    endif()

    get_property(_directory_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _directory_targets)
        get_target_property(_target_type "${_target}" TYPE)
        if(_target_type STREQUAL "EXECUTABLE")
            vixen_stage_runtime_dlls_for_target("${_target}")
        endif()
    endforeach()

    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        vixen_stage_runtime_dlls_in_directory("${_subdirectory}")
    endforeach()
endfunction()

function(vixen_stage_runtime_dlls_in_project)
    if(NOT WIN32)
        return()
    endif()

    # The root directory defers this pass until every add_subdirectory() has
    # declared its targets. New executable targets then inherit staging without
    # requiring a per-directory hook.
    vixen_stage_runtime_dlls_in_directory("${CMAKE_SOURCE_DIR}")
endfunction()

if(WIN32)
    # The runtime DLL expression is available starting in CMake 3.21, so the
    # deferred whole-project pass uses only that supported toolchain range.
    cmake_language(DEFER CALL vixen_stage_runtime_dlls_in_project)
endif()
