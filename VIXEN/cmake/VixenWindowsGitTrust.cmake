include_guard(GLOBAL)

# Windows Git refuses a repository on the WSL filesystem ("detected dubious ownership") and suggests a
# persistent global safe.directory entry. These helpers add the trust to this configure process only,
# through GIT_CONFIG_COUNT, so every git it spawns (FetchContent clones, kernel snapshot) inherits it and
# user configuration is untouched.

# Append `safe.directory = <pattern>` to the process's GIT_CONFIG_* list, keeping entries already there.
function(vixen_git_trust_process_directory pattern)
    set(_count 0)
    if("$ENV{GIT_CONFIG_COUNT}" MATCHES "^[0-9]+$")
        set(_count "$ENV{GIT_CONFIG_COUNT}")
    endif()
    set(_index 0)
    while(_index LESS _count)
        if("$ENV{GIT_CONFIG_KEY_${_index}}" STREQUAL "safe.directory" AND
           "$ENV{GIT_CONFIG_VALUE_${_index}}" STREQUAL "${pattern}")
            return()
        endif()
        math(EXPR _index "${_index} + 1")
    endwhile()
    set(ENV{GIT_CONFIG_KEY_${_count}} "safe.directory")
    set(ENV{GIT_CONFIG_VALUE_${_count}} "${pattern}")
    math(EXPR _count "${_count} + 1")
    set(ENV{GIT_CONFIG_COUNT} "${_count}")
endfunction()

# Trust `path` (an existing directory) on a Windows host when it lives on the WSL filesystem. With
# CHILDREN, trust every repository below it (a FetchContent cache). Git matches the //wsl.localhost
# form even when the path is reached through a mapped drive letter, so resolve it first.
function(vixen_trust_wsl_git_directory path)
    if(NOT CMAKE_HOST_WIN32)
        return()
    endif()
    file(REAL_PATH "${path}" _real)
    if(NOT _real MATCHES "^//wsl(\\.localhost|[$])/")
        return()
    endif()
    if("${ARGN}" STREQUAL "CHILDREN")
        string(APPEND _real "/*")
    endif()
    vixen_git_trust_process_directory("${_real}")
endfunction()
