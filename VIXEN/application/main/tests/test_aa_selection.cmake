if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_aa_selection
        ${CMAKE_CURRENT_LIST_DIR}/test_aa_selection.cpp
        ${CMAKE_CURRENT_LIST_DIR}/test_temporal_resolve.cpp)
    # The resolve feedback test reads the generated C++ face of codegen/aa-callables.
    if(TARGET temporal_resolve_cpp_check)
        add_dependencies(test_aa_selection temporal_resolve_cpp_check)
    endif()
    target_include_directories(test_aa_selection PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include)
    target_link_libraries(test_aa_selection PRIVATE GTest::gtest_main)
    target_compile_features(test_aa_selection PRIVATE cxx_std_23)
    set_target_properties(test_aa_selection PROPERTIES FOLDER "Tests/Application")
    add_test(NAME Application.AntiAliasingSelection COMMAND test_aa_selection)
    set_tests_properties(Application.AntiAliasingSelection PROPERTIES
        LABELS "Application;Unit"
        TIMEOUT 60)
    message(STATUS "[Application Tests] Added: test_aa_selection (AA module contract, the shared selection predicate and the TAA resolve feedback)")
endif()
