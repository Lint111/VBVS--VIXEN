cmake_minimum_required(VERSION 3.15)
project(AppFlowTests)

set(APPFLOW_GEN_HDR "${CMAKE_CURRENT_SOURCE_DIR}/../include/generated/AppFlow.g.h")

foreach(t test_appflow_golden test_action_stack test_flow_state_machine test_binding_store test_appflow_loader test_appflow_data_seam test_layer_controller test_snapshot_undo test_input_profile test_keychord test_binding_pattern test_flow_return test_return_dispatch)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE GTest::gtest_main AppFlow)
    set_target_properties(${t} PROPERTIES FOLDER "Tests/AppFlow Tests")
endforeach()

# Golden test needs the on-disk header path.
target_compile_definitions(test_appflow_golden PRIVATE
    APPFLOW_GENERATED_HEADER_PATH="${APPFLOW_GEN_HDR}")

# Inc-4 reframe R5a: test_keychord also covers KeyMap.h's GLFW-keycode -> KeyId completeness
# (application/editor/include/KeyMap.h is header-only, depends only on the already-public
# AppFlow.g.h) -- give it that one extra include dir rather than link the whole editor app.
target_include_directories(test_keychord PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../application/editor/include")

if(COMMAND gtest_discover_tests)
    foreach(t test_appflow_golden test_action_stack test_flow_state_machine test_binding_store test_appflow_loader test_appflow_data_seam test_layer_controller test_snapshot_undo test_input_profile test_keychord test_binding_pattern test_flow_return test_return_dispatch)
        gtest_discover_tests(${t})
    endforeach()
endif()
