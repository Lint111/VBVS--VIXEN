# Runs at CTest load time, once per GoogleTest target, after the target's discovery include has
# created its cases. Inputs (set by the generated stub): VIXEN_CLASS_TARGET, VIXEN_CLASS_LINK (the
# target's link-closure class), VIXEN_CLASS_FINGERPRINT (hash of its sources), VIXEN_CLASS_TESTS
# (the discovered case names), VIXEN_CLASS_MANIFEST, VIXEN_GPU_RESOURCE_GROUPS and the cases the
# registration declared GPU: VIXEN_CLASS_DECLARED_GPU (all) / VIXEN_CLASS_DECLARED_GPU_PATTERNS (regexes).
#
# Each case takes the manifest class when the manifest has an entry for it written against the same
# target sources; otherwise it keeps the link-closure class. CTest property commands add to
# LABELS, ENVIRONMENT_MODIFICATION and RESOURCE_GROUPS (once per GPU case), and ignore names that do not exist.
#
# Manifest columns: test, class, target, source-fingerprint, evidence.

cmake_policy(VERSION 3.28)

if(VIXEN_CLASS_TESTS)
    set(_vixen_manifest "\n")
    if(EXISTS "${VIXEN_CLASS_MANIFEST}")
        file(READ "${VIXEN_CLASS_MANIFEST}" _vixen_manifest_text)
        string(APPEND _vixen_manifest "${_vixen_manifest_text}")
    endif()
    set(_vixen_link_class "${VIXEN_CLASS_LINK}")
    if(NOT _vixen_link_class STREQUAL "cpu")
        set(_vixen_link_class gpu)
    endif()

    foreach(_vixen_test IN LISTS VIXEN_CLASS_TESTS)
        set(_vixen_class "${_vixen_link_class}")
        set(_vixen_source classified-by-link)
        string(FIND "${_vixen_manifest}" "\n${_vixen_test}\t" _vixen_position)
        if(NOT _vixen_position EQUAL -1)
            math(EXPR _vixen_position "${_vixen_position} + 1")
            string(SUBSTRING "${_vixen_manifest}" ${_vixen_position} 4096 _vixen_tail)
            if(_vixen_tail MATCHES "^[^\t\n]*\t(gpu|cpu)\t([^\t\n]*)\t([^\t\n]*)\t")
                if("${CMAKE_MATCH_2}" STREQUAL "${VIXEN_CLASS_TARGET}"
                   AND "${CMAKE_MATCH_3}" STREQUAL "${VIXEN_CLASS_FINGERPRINT}")
                    set(_vixen_class "${CMAKE_MATCH_1}")
                    set(_vixen_source classified-by-run)
                endif()
            endif()
        endif()

        set_tests_properties("${_vixen_test}" PROPERTIES
            LABELS "${_vixen_class};${_vixen_source}"
            ENVIRONMENT_MODIFICATION "VIXEN_TEST_CLASS=set:${_vixen_class};VIXEN_TEST_NAME=set:${_vixen_test}")
        set(_vixen_gpu_case OFF)
        if(_vixen_class STREQUAL "gpu" OR VIXEN_CLASS_DECLARED_GPU)
            set(_vixen_gpu_case ON)
        endif()
        foreach(_vixen_pattern IN LISTS VIXEN_CLASS_DECLARED_GPU_PATTERNS)
            if(_vixen_test MATCHES "${_vixen_pattern}")
                set(_vixen_gpu_case ON)
            endif()
        endforeach()
        # One request per case: every CTest-time set_tests_properties appends another resource group,
        # also when two targets of this directory list the same case name.
        if(_vixen_gpu_case AND NOT "${_vixen_test}" IN_LIST _VIXEN_GPU_REQUESTED)
            list(APPEND _VIXEN_GPU_REQUESTED "${_vixen_test}")
            set_tests_properties("${_vixen_test}" PROPERTIES RESOURCE_GROUPS "${VIXEN_GPU_RESOURCE_GROUPS}")
        endif()
    endforeach()
endif()
