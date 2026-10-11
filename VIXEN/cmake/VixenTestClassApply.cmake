# Runs at CTest load time (TEST_INCLUDE_FILES), after the directory's tests exist.
#
# Replaces the link-closure class of every test the runtime manifest knows with the class measured
# when the test last ran. An entry is used only while its executable's sources still match the
# fingerprint recorded with it; a stale entry is ignored, so that test keeps its link-closure class
# until tools/classify-tests-gpu.py runs again.
#
# Manifest columns: test, class, target, source-fingerprint, evidence.
# Executable table columns: target, file, link class, source-fingerprint, evidence.

cmake_policy(VERSION 3.28)

if(EXISTS "${VIXEN_CLASS_MANIFEST}" AND EXISTS "${VIXEN_CLASS_EXE_TABLE}")
    file(STRINGS "${VIXEN_CLASS_EXE_TABLE}" _vixen_exe_rows)
    foreach(_row IN LISTS _vixen_exe_rows)
        if(_row MATCHES "^([^\t#][^\t]*)\t[^\t]*\t[^\t]*\t([^\t]*)\t")
            set(_vixen_fingerprint_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
        endif()
    endforeach()

    file(STRINGS "${VIXEN_CLASS_MANIFEST}" _vixen_manifest_rows)
    foreach(_row IN LISTS _vixen_manifest_rows)
        if(NOT _row MATCHES "^([^\t#][^\t]*)\t(gpu|cpu)\t([^\t]*)\t([^\t]*)\t")
            continue()
        endif()
        set(_name "${CMAKE_MATCH_1}")
        set(_class "${CMAKE_MATCH_2}")
        set(_target "${CMAKE_MATCH_3}")
        set(_recorded "${CMAKE_MATCH_4}")
        if(NOT TEST "${_name}")
            continue()
        endif()
        if(NOT DEFINED _vixen_fingerprint_${_target} OR NOT _vixen_fingerprint_${_target} STREQUAL _recorded)
            continue()
        endif()

        get_test_property("${_name}" LABELS _labels)
        list(REMOVE_ITEM _labels gpu cpu classified-by-link classified-by-run)
        list(APPEND _labels "${_class}" classified-by-run)
        set_tests_properties("${_name}" PROPERTIES LABELS "${_labels}")

        get_test_property("${_name}" ENVIRONMENT_MODIFICATION _modification)
        if(_modification STREQUAL "NOTFOUND")
            set(_modification)
        endif()
        list(APPEND _modification "VIXEN_TEST_CLASS=set:${_class}")
        set_tests_properties("${_name}" PROPERTIES ENVIRONMENT_MODIFICATION "${_modification}")

        if(_class STREQUAL "cpu")
            get_test_property("${_name}" RESOURCE_LOCK _locks)
            if(NOT _locks STREQUAL "NOTFOUND")
                list(REMOVE_ITEM _locks vixen_gpu_device)
                set_tests_properties("${_name}" PROPERTIES RESOURCE_LOCK "${_locks}")
            endif()
        else()
            get_test_property("${_name}" RESOURCE_LOCK _locks)
            if(_locks STREQUAL "NOTFOUND")
                set(_locks)
            endif()
            list(APPEND _locks vixen_gpu_device)
            list(REMOVE_DUPLICATES _locks)
            set_tests_properties("${_name}" PROPERTIES RESOURCE_LOCK "${_locks}")
        endif()
    endforeach()
endif()
