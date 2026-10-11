# Runs at CTest load time, once per GoogleTest target, after the target's discovery include has
# created its cases. Inputs (set by the generated stub): VIXEN_CLASS_TARGET, VIXEN_CLASS_LINK (the
# target's link-closure class), VIXEN_CLASS_FINGERPRINT (hash of its sources), VIXEN_CLASS_TESTS
# (the discovered case names) and VIXEN_CLASS_MANIFEST.
#
# Each case takes the manifest class when the manifest has an entry for it written against the same
# target sources; otherwise it keeps the link-closure class. CTest property commands add to
# LABELS, ENVIRONMENT_MODIFICATION and RESOURCE_LOCK, and ignore names that do not exist.
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
            ENVIRONMENT_MODIFICATION "VIXEN_TEST_CLASS=set:${_vixen_class}")
        if(_vixen_class STREQUAL "gpu")
            set_tests_properties("${_vixen_test}" PROPERTIES RESOURCE_LOCK vixen_gpu_device)
        endif()
    endforeach()
endif()
