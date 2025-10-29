cmake_minimum_required(VERSION 3.15)
project(EventBusTests)

add_executable(test_eventbus_basic test_eventbus_basic.cpp)
if(TARGET EventBus)
    target_link_libraries(test_eventbus_basic PRIVATE GTest::gtest_main EventBus)
else()
    target_link_libraries(test_eventbus_basic PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_eventbus_basic)
