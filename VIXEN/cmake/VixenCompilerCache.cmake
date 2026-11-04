# USE_CCACHE=OFF must mean "no compiler cache launcher", including one a previous configure left in
# CMakeCache.txt (-DCMAKE_<LANG>_COMPILER_LAUNCHER=ccache, or the environment variable of the same
# name, both of which persist in the cache). Only ccache/sccache launchers are cleared; any other
# launcher (distcc, a PCH wrapper) is the caller's explicit choice and is left alone.
function(vixen_clear_cached_compiler_cache_launchers)
    foreach(_language C CXX)
        set(_var CMAKE_${_language}_COMPILER_LAUNCHER)
        if(NOT DEFINED CACHE{${_var}})
            continue()
        endif()
        get_property(_value CACHE ${_var} PROPERTY VALUE)
        if(_value MATCHES "(^|[;/\\\\])(ccache|sccache)(\\.exe)?(;|$)")
            message(STATUS "USE_CCACHE=OFF: clearing cached ${_var} (was '${_value}')")
            set(${_var} "" CACHE STRING "Compiler launcher (cleared: USE_CCACHE=OFF)" FORCE)
            set(${_var} "" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()
