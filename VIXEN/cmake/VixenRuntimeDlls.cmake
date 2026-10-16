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

# Attach staging to executable targets declared in the directory that owns
# them. CMake requires TARGET POST_BUILD commands to be added from that same
# directory (unless using newer CMake versions with the DIRECTORY option).
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
endfunction()

function(vixen_schedule_runtime_dlls_for_current_directory)
    if(NOT WIN32)
        return()
    endif()

    get_property(_scheduled DIRECTORY PROPERTY VIXEN_RUNTIME_DLL_SCAN_SCHEDULED)
    if(_scheduled)
        return()
    endif()
    set_property(DIRECTORY PROPERTY VIXEN_RUNTIME_DLL_SCAN_SCHEDULED TRUE)
    cmake_language(DEFER CALL vixen_stage_runtime_dlls_in_directory
        "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

if(WIN32)
    # The root project() call has already happened before this module is
    # included. Schedule its directory explicitly, then use CMake's project
    # include hook to schedule every nested project at the end of its directory.
    set(VIXEN_RUNTIME_DLLS_PREVIOUS_PROJECT_INCLUDE "${CMAKE_PROJECT_INCLUDE}")
    set(CMAKE_PROJECT_INCLUDE
        "${CMAKE_CURRENT_LIST_DIR}/VixenRuntimeDllProjectInclude.cmake")
    vixen_schedule_runtime_dlls_for_current_directory()
endif()
