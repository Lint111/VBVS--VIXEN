if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_upscaler_selection
        ${CMAKE_CURRENT_LIST_DIR}/test_upscaler_selection.cpp)
    target_include_directories(test_upscaler_selection PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include)
    target_link_libraries(test_upscaler_selection PRIVATE GTest::gtest_main)
    target_compile_features(test_upscaler_selection PRIVATE cxx_std_23)
    set_target_properties(test_upscaler_selection PROPERTIES FOLDER "Tests/Application")
    add_test(NAME Application.UpscalerSelection COMMAND test_upscaler_selection)
    set_tests_properties(Application.UpscalerSelection PROPERTIES
        LABELS "Application;Unit"
        TIMEOUT 60)
    message(STATUS "[Application Tests] Added: test_upscaler_selection (contract and fallback predicate)")
endif()
