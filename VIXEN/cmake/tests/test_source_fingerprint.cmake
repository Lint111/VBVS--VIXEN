# Checks cmake/VixenSourceFingerprintScript.cmake: -DSCRIPT=<script> -DWORK=<scratch dir> -P this file.
# The fingerprint must follow file contents (not paths) and the salt, so identical generator code in
# two build trees shares a store while any code or toolchain difference does not.
cmake_minimum_required(VERSION 3.21)

function(fingerprint_of dir salt out)
    file(WRITE "${dir}/list.txt" "${dir}/gen.cpp\n${dir}/gen.h\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DOUT_HEADER=${dir}/fp.h" "-DCONSTANT=Test::kFp"
            "-DLIST_FILE=${dir}/list.txt" "-DSALT=${salt}" -P "${SCRIPT}"
        RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "fingerprint script failed in ${dir}: ${_err}")
    endif()
    file(READ "${dir}/fp.h" _header)
    string(REGEX MATCH "kFp\\[\\] = \"([0-9a-f]+)\"" _unused "${_header}")
    set(_value "${CMAKE_MATCH_1}")
    if(NOT _value MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "no fingerprint in generated header:\n${_header}")
    endif()
    string(LENGTH "${_value}" _len)
    if(NOT _len EQUAL 16)
        message(FATAL_ERROR "fingerprint '${_value}' is not 16 hex digits")
    endif()
    set(${out} "${_value}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK}")
foreach(tree a b)
    file(MAKE_DIRECTORY "${WORK}/${tree}")
    file(WRITE "${WORK}/${tree}/gen.cpp" "int generate() { return 1; }\n")
    file(WRITE "${WORK}/${tree}/gen.h" "int generate();\n")
endforeach()

fingerprint_of("${WORK}/a" "gcc 13" fp_a)
fingerprint_of("${WORK}/b" "gcc 13" fp_b)
if(NOT fp_a STREQUAL fp_b)
    message(FATAL_ERROR "identical sources in two trees must share a fingerprint: ${fp_a} vs ${fp_b}")
endif()

file(WRITE "${WORK}/b/gen.cpp" "int generate() { return 2; }\n")
fingerprint_of("${WORK}/b" "gcc 13" fp_edited)
if(fp_edited STREQUAL fp_a)
    message(FATAL_ERROR "an edited generator source must change the fingerprint (${fp_a})")
endif()

fingerprint_of("${WORK}/a" "gcc 14" fp_salt)
if(fp_salt STREQUAL fp_a)
    message(FATAL_ERROR "a different compiler salt must change the fingerprint (${fp_a})")
endif()

# An unchanged rerun must not rewrite the header (dependents would rebuild on every build).
file(TIMESTAMP "${WORK}/a/fp.h" _before "%s")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
fingerprint_of("${WORK}/a" "gcc 14" fp_again)
file(TIMESTAMP "${WORK}/a/fp.h" _after "%s")
if(NOT _before STREQUAL _after)
    message(FATAL_ERROR "fingerprint header was rewritten although its content is unchanged")
endif()
file(REMOVE_RECURSE "${WORK}")
