# The Windows compiler cache is ONE shared cache for every native tree (R873), keyed so that two
# trees under one root hash alike. Pure-function checks: the resolution, the base_dir and the
# launcher environment do not need Windows.
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${vixen_root}/cmake/VixenCompilerCache.cmake")

function(expect_eq what actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${what}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

set(legacy "C:/vixen-native/lane1/build/ninja-release/_cache/compiler")

# Existing trees cached the legacy per-tree default: it must count as unset.
vixen_resolve_windows_compiler_cache_dir(ccache dir "${legacy}" "${legacy}" "" "C:")
expect_eq("legacy default, no env" "${dir}" "C:/ccache")
vixen_resolve_windows_compiler_cache_dir(ccache dir "${legacy}" "${legacy}" "D:\\shared\\ccache" "C:")
expect_eq("legacy default, build.bat env" "${dir}" "D:/shared/ccache")
vixen_resolve_windows_compiler_cache_dir(sccache dir "${legacy}" "${legacy}" "" "")
expect_eq("sccache, no drive" "${dir}" "C:/sccache")
# Two different trees resolve to the same directory.
vixen_resolve_windows_compiler_cache_dir(ccache dir_a "C:/vixen-native/a/build/_cache/compiler" "C:/vixen-native/a/build/_cache/compiler" "C:\\ccache" "C:")
vixen_resolve_windows_compiler_cache_dir(ccache dir_b "C:/vixen-native/b/build/_cache/compiler" "C:/vixen-native/b/build/_cache/compiler" "C:\\ccache" "C:")
expect_eq("tree a vs tree b" "${dir_a}" "${dir_b}")
# An explicit directory keeps the old <dir>/<kind> layout.
vixen_resolve_windows_compiler_cache_dir(ccache dir "E:/cc" "${legacy}" "C:\\ccache" "C:")
expect_eq("explicit" "${dir}" "E:/cc/ccache")

# base_dir: the parent of the checkout, never a drive root.
vixen_compiler_cache_base_dir(base "C:/vixen-native/lane1/VIXEN" "")
expect_eq("base_dir native tree" "${base}" "C:/vixen-native")
vixen_compiler_cache_base_dir(base "C:/vixen-native/lane2/VIXEN" "")
expect_eq("base_dir sibling tree" "${base}" "C:/vixen-native")
vixen_compiler_cache_base_dir(base "C:/work/VIXEN" "")
expect_eq("base_dir never a drive root" "${base}" "C:/work")
vixen_compiler_cache_base_dir(base "C:/vixen-native/lane1/VIXEN" "D:\\roots")
expect_eq("base_dir env wins" "${base}" "D:/roots")

# Launcher environment.
vixen_compiler_cache_launcher_env(env ccache "C:/ccache" "C:/vixen-native" "" "")
expect_eq("ccache env" "${env}" "CCACHE_DIR=C:/ccache;CCACHE_BASEDIR=C:/vixen-native;CCACHE_NOHASHDIR=1;CCACHE_MAXSIZE=20G")
vixen_compiler_cache_launcher_env(env ccache "C:/ccache" "C:/vixen-native" "50G" "pch_defines,time_macros")
expect_eq("caller size wins, sloppiness" "${env}" "CCACHE_DIR=C:/ccache;CCACHE_BASEDIR=C:/vixen-native;CCACHE_NOHASHDIR=1;CCACHE_SLOPPINESS=pch_defines,time_macros")
vixen_compiler_cache_launcher_env(env sccache "C:/sccache" "C:/vixen-native" "" "")
expect_eq("sccache env" "${env}" "SCCACHE_DIR=C:/sccache")

# A cache inside WSL is refused (what/where/why/fix in the message).
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DVIXEN_WSL_PATH=//wsl.localhost/Ubuntu/home/x/ccache"
    -P "${CMAKE_CURRENT_LIST_DIR}/test_compiler_cache_shared_wsl.cmake"
    RESULT_VARIABLE rc ERROR_VARIABLE err)
if(rc EQUAL 0 OR NOT err MATCHES "inside WSL")
    message(FATAL_ERROR "a WSL cache dir must be refused: rc=${rc} ${err}")
endif()
message(STATUS "Windows compiler cache: one shared directory, common base_dir, WSL refused")
