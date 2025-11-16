cmake_minimum_required(VERSION 3.15)
project(LoggerTests)

add_executable(test_logger_basic test_logger_basic.cpp)
target_include_directories(test_logger_basic PRIVATE ${CMAKE_SOURCE_DIR})
if(TARGET Logger)
    target_link_libraries(test_logger_basic PRIVATE GTest::gtest_main Logger)
else()
    target_link_libraries(test_logger_basic PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_logger_basic)
