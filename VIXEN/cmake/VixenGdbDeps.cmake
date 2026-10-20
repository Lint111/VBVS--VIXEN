include_guard(GLOBAL)

# Turns `apt-cache depends gdb` output into the list of gdb's direct dependency packages for the
# host's release. Skips <virtual> packages; keeps every alternative of an "A | B" dependency.
function(vixen_gdb_direct_dependency_packages out_var apt_depends_output)
    string(REPLACE "\r" "" _text "${apt_depends_output}")
    string(REPLACE "\n" ";" _lines "${_text}")
    set(_packages "")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "^[ |]+Depends: ([^<: ][^: ]*)$")
            list(APPEND _packages "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _packages)
    set(${out_var} "${_packages}" PARENT_SCOPE)
endfunction()
