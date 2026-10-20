cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../VixenGdbDeps.cmake")

function(expect_packages label depends_output)
    vixen_gdb_direct_dependency_packages(_got "${depends_output}")
    if(NOT "${_got}" STREQUAL "${ARGN}")
        message(FATAL_ERROR "${label}: expected '${ARGN}' but got '${_got}'")
    endif()
endfunction()

# Ubuntu 24.04 (noble) names the Python runtime libpython3.12t64.
expect_packages(noble
"gdb
  Depends: libc6
  Depends: libpython3.12t64
  Depends: libreadline8t64"
    libc6 libpython3.12t64 libreadline8t64)

# Ubuntu 26.04 (resolute) ships libpython3.14; the list must follow the host, not a fixed release.
expect_packages(resolute
"gdb
  Depends: libbabeltrace1
  Depends: libpython3.14
  Depends: zlib1g"
    libbabeltrace1 libpython3.14 zlib1g)

# Alternatives are both kept, virtual packages and non-Depends relations are skipped, duplicates collapse.
expect_packages(alternatives
"gdb
 |Depends: libfoo1
  Depends: libfoo2
  Depends: <python3-virtual>
  Depends: libfoo2
  Recommends: libdebuginfod1t64
  Depends: libc6:i386"
    libfoo1 libfoo2)

expect_packages(empty "")
message(STATUS "gdb dependency package parsing checks passed")
