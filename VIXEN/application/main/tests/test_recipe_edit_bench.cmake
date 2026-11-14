if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp)
    # Recipe edit-to-visible latency per edit class (op-recipeedit1): a benchmark executable, not a
    # CTest case. Run by tools/run-load-scenario.bat with SCENARIO_EXE:vixen_recipe_edit_bench.
    add_executable(vixen_recipe_edit_bench ${CMAKE_CURRENT_LIST_DIR}/test_recipe_edit_bench.cpp)
    vixen_whole_archive_link_vixen_app(vixen_recipe_edit_bench PRIVATE)
    set_target_properties(vixen_recipe_edit_bench PROPERTIES FOLDER "Tests/Application")
endif()
