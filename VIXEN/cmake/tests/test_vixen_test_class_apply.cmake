cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED VIXEN_TEST_CLASS_APPLY OR NOT EXISTS "${VIXEN_TEST_CLASS_APPLY}")
    message(FATAL_ERROR "VIXEN_TEST_CLASS_APPLY must name VixenTestClassApply.cmake")
endif()
if(NOT DEFINED VIXEN_TEST_APPLY_ROOT)
    message(FATAL_ERROR "VIXEN_TEST_APPLY_ROOT must name the scratch CTest tree")
endif()

file(REMOVE_RECURSE "${VIXEN_TEST_APPLY_ROOT}")
file(MAKE_DIRECTORY "${VIXEN_TEST_APPLY_ROOT}")
set(_manifest "${VIXEN_TEST_APPLY_ROOT}/gpu-class-manifest.tsv")
file(WRITE "${_manifest}"
    "# test<TAB>class<TAB>target<TAB>source-fingerprint<TAB>evidence\n"
    "ApplyCpu\tcpu\tapply_target\tapply-fingerprint\tfixture\n"
    "ApplyGpu\tgpu\tapply_target\tapply-fingerprint\tdevice\n")

# This is the shape of the generated directory load file: discovery supplies suite/exe labels,
# then the class hook writes the complete per-case label set in CTest's load context.
set(_testfile "${VIXEN_TEST_APPLY_ROOT}/CTestTestfile.cmake")
file(WRITE "${_testfile}"
    "add_test(ApplyCpu \"/bin/true\")\n"
    "add_test(ApplyGpu \"/bin/true\")\n"
    "set_tests_properties(ApplyCpu ApplyGpu PROPERTIES LABELS \"Suite;exe:apply_target\")\n"
    "set(VIXEN_CLASS_TARGET apply_target)\n"
    "set(VIXEN_CLASS_SUITE Suite)\n"
    "set(VIXEN_CLASS_LINK cpu)\n"
    "set(VIXEN_CLASS_FINGERPRINT apply-fingerprint)\n"
    "set(VIXEN_CLASS_TESTS \"ApplyCpu;ApplyGpu\")\n"
    "set(VIXEN_CLASS_MANIFEST [==[${_manifest}]==])\n"
    "include([==[${VIXEN_TEST_CLASS_APPLY}]==])\n")

execute_process(
    COMMAND ctest --test-dir "${VIXEN_TEST_APPLY_ROOT}" --show-only=json-v1
    RESULT_VARIABLE _ctest_status
    OUTPUT_VARIABLE _ctest_json
    ERROR_VARIABLE _ctest_error)
if(NOT _ctest_status EQUAL 0)
    message(FATAL_ERROR "CTest load-context probe failed (${_ctest_status}): ${_ctest_error}")
endif()

string(JSON _test_count LENGTH "${_ctest_json}" tests)
if(NOT _test_count EQUAL 2)
    message(FATAL_ERROR "expected two load-context tests, got ${_test_count}: ${_ctest_json}")
endif()

foreach(_expected IN ITEMS "ApplyCpu|cpu|classified-by-run" "ApplyGpu|gpu|classified-by-run")
    string(REPLACE "|" ";" _expected_fields "${_expected}")
    list(GET _expected_fields 0 _expected_name)
    list(GET _expected_fields 1 _expected_class)
    list(GET _expected_fields 2 _expected_source)
    set(_found FALSE)
    foreach(_test_index RANGE 0 1)
        string(JSON _test_name GET "${_ctest_json}" tests ${_test_index} name)
        if(NOT _test_name STREQUAL _expected_name)
            continue()
        endif()
        set(_found TRUE)
        set(_labels)
        string(JSON _property_count LENGTH "${_ctest_json}" tests ${_test_index} properties)
        math(EXPR _last_property "${_property_count} - 1")
        foreach(_property_index RANGE 0 ${_last_property})
            string(JSON _property_name GET "${_ctest_json}" tests ${_test_index} properties ${_property_index} name)
            if(NOT _property_name STREQUAL "LABELS")
                continue()
            endif()
            string(JSON _label_type TYPE "${_ctest_json}" tests ${_test_index} properties ${_property_index} value)
            if(_label_type STREQUAL "ARRAY")
                string(JSON _label_count LENGTH "${_ctest_json}" tests ${_test_index} properties ${_property_index} value)
                math(EXPR _last_label "${_label_count} - 1")
                foreach(_label_index RANGE 0 ${_last_label})
                    string(JSON _label GET "${_ctest_json}" tests ${_test_index} properties ${_property_index} value ${_label_index})
                    list(APPEND _labels "${_label}")
                endforeach()
            else()
                string(JSON _label_text GET "${_ctest_json}" tests ${_test_index} properties ${_property_index} value)
                set(_labels ${_label_text})
            endif()
        endforeach()
        set(_expected_labels "Suite;${_expected_class};${_expected_source};exe:apply_target")
        list(SORT _labels)
        list(SORT _expected_labels)
        if(NOT _labels STREQUAL _expected_labels)
            message(FATAL_ERROR "${_expected_name} labels were [${_labels}], expected [${_expected_labels}]")
        endif()
    endforeach()
    if(NOT _found)
        message(FATAL_ERROR "CTest load-context output did not contain ${_expected_name}")
    endif()
endforeach()

message(STATUS "CTest load-context class labels are exact for ApplyCpu and ApplyGpu")
