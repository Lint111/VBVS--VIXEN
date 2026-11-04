# T-4950 -- header-only RunDataPaths.h; no VixenApp link needed.
if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET GTest::gtest_main)
    add_executable(test_run_data_paths ${CMAKE_CURRENT_LIST_DIR}/test_run_data_paths.cpp)
    target_link_libraries(test_run_data_paths PRIVATE GTest::gtest_main)
    target_include_directories(test_run_data_paths PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
    set_target_properties(test_run_data_paths PROPERTIES FOLDER "Tests/Application")
    gtest_discover_tests(test_run_data_paths)
    message(STATUS "[Application Tests] Added: test_run_data_paths (T-4950: run-data dir for log + calibration)")
endif()
