# CMAKE_PROJECT_INCLUDE runs inside each nested project directory. Schedule
# the shared runtime DLL pass there so POST_BUILD commands are attached from
# the directory that owns each executable target.
if(VIXEN_RUNTIME_DLLS_PREVIOUS_PROJECT_INCLUDE)
    foreach(_vixen_previous_project_include IN LISTS VIXEN_RUNTIME_DLLS_PREVIOUS_PROJECT_INCLUDE)
        include("${_vixen_previous_project_include}")
    endforeach()
endif()

if(WIN32 AND COMMAND vixen_schedule_runtime_dlls_for_current_directory)
    vixen_schedule_runtime_dlls_for_current_directory()
endif()
