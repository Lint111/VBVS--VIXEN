if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_recipe_preview_fence
        ${CMAKE_CURRENT_LIST_DIR}/test_recipe_preview_fence.cpp)
    target_include_directories(test_recipe_preview_fence PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include)
    target_link_libraries(test_recipe_preview_fence PRIVATE GTest::gtest_main)
    target_compile_features(test_recipe_preview_fence PRIVATE cxx_std_23)
    set_target_properties(test_recipe_preview_fence PROPERTIES FOLDER "Tests/Application")
    vixen_gtest_discover_tests(Application test_recipe_preview_fence)
    message(STATUS "[Application Tests] Added: test_recipe_preview_fence (serial ordering and failure completion)")
endif()
