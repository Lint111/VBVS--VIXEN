cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED VIXEN_GIT_TRUST_ROOT)
    message(FATAL_ERROR "VIXEN_GIT_TRUST_ROOT must be a scratch directory inside the caller's build tree")
endif()
get_filename_component(vixen_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${vixen_root}/cmake/VixenWindowsGitTrust.cmake")
file(REMOVE_RECURSE "${VIXEN_GIT_TRUST_ROOT}")
file(MAKE_DIRECTORY "${VIXEN_GIT_TRUST_ROOT}")

function(expect_env_equal name expected)
    if(NOT "$ENV{${name}}" STREQUAL "${expected}")
        message(FATAL_ERROR "${name} is [$ENV{${name}}], expected [${expected}]")
    endif()
endfunction()

# Entries the caller already configured survive, and trust is appended after them once.
set(ENV{GIT_CONFIG_COUNT} "1")
set(ENV{GIT_CONFIG_KEY_0} "core.autocrlf")
set(ENV{GIT_CONFIG_VALUE_0} "false")
vixen_git_trust_process_directory("//wsl.localhost/Ubuntu/home/test/cache/*")
vixen_git_trust_process_directory("//wsl.localhost/Ubuntu/home/test/cache/*")
expect_env_equal(GIT_CONFIG_COUNT "2")
expect_env_equal(GIT_CONFIG_KEY_0 "core.autocrlf")
expect_env_equal(GIT_CONFIG_VALUE_0 "false")
expect_env_equal(GIT_CONFIG_KEY_1 "safe.directory")
expect_env_equal(GIT_CONFIG_VALUE_1 "//wsl.localhost/Ubuntu/home/test/cache/*")

# No existing list starts at zero.
unset(ENV{GIT_CONFIG_COUNT})
vixen_git_trust_process_directory("//wsl.localhost/Ubuntu/home/test/kernel")
expect_env_equal(GIT_CONFIG_COUNT "1")
expect_env_equal(GIT_CONFIG_VALUE_0 "//wsl.localhost/Ubuntu/home/test/kernel")

# A non-Windows host is untouched, and a Windows host never trusts a directory outside the WSL
# filesystem (a local scratch directory here).
if(NOT CMAKE_HOST_WIN32)
    unset(ENV{GIT_CONFIG_COUNT})
    vixen_trust_wsl_git_directory("${VIXEN_GIT_TRUST_ROOT}")
    set(CMAKE_HOST_WIN32 TRUE)
    vixen_trust_wsl_git_directory("${VIXEN_GIT_TRUST_ROOT}")
    unset(CMAKE_HOST_WIN32)
    if(NOT "$ENV{GIT_CONFIG_COUNT}" STREQUAL "")
        message(FATAL_ERROR "a non-WSL directory was trusted: $ENV{GIT_CONFIG_COUNT}")
    endif()
endif()

message(STATUS "Windows Git trust is process-scoped, append-only and limited to WSL directories")
