# Helper of test_compiler_cache_shared.cmake: resolving a WSL-internal cache directory must fail.
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${vixen_root}/cmake/VixenCompilerCache.cmake")
vixen_resolve_windows_compiler_cache_dir(ccache dir "" "" "${VIXEN_WSL_PATH}" "C:")
