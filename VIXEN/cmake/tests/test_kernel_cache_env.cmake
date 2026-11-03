# VIXEN_KERNEL_CACHE_DIR from the environment places the cache; an explicit -D value still wins.
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ENV{VIXEN_KERNEL_CACHE_DIR} "/lane/env-cache")
include("${vixen_root}/cmake/VixenKernelSnapshot.cmake")
if(NOT VIXEN_KERNEL_CACHE_DIR STREQUAL "/lane/env-cache")
    message(FATAL_ERROR "environment VIXEN_KERNEL_CACHE_DIR ignored: got '${VIXEN_KERNEL_CACHE_DIR}'")
endif()
