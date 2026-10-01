include_guard(GLOBAL)

# Keep configure-time outputs' timestamps stable when their bytes are unchanged.
function(vixen_write_if_different path content)
    if(EXISTS "${path}")
        file(READ "${path}" _previous)
        if(_previous STREQUAL content)
            return()
        endif()
    endif()
    get_filename_component(_parent "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${_parent}")
    file(WRITE "${path}" "${content}")
endfunction()
