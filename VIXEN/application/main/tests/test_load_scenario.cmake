if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp)
    # Deterministic heavy dynamic load scenario (sn-loadscene1): a benchmark executable, not a CTest
    # case. It is run by tools/run-load-scenario.bat on the native Windows build.
    add_executable(vixen_load_scenario ${CMAKE_CURRENT_LIST_DIR}/test_load_scenario.cpp)
    vixen_whole_archive_link_vixen_app(vixen_load_scenario PRIVATE)
    set_target_properties(vixen_load_scenario PROPERTIES FOLDER "Tests/Application")
endif()
